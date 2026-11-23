/*
 * hostsim — 假宿主 + 低级键盘钩子，冒烟测试 VoiceKey 的按下/松开链路。
 *
 * 构造一个假的 mpl_host_api_t（get_config 返回裸值 JSON、audio_state 返回
 * 带 inputLevel 的快照、set_interval 记录回调、set_config/log 记录），加载
 * voicekey.dll 并手动驱动 interval tick，同时用 WH_KEYBOARD_LL 捕获插件通过
 * SendInput 注入的按键，验证：
 *   1) 静音期不注入；
 *   2) 电平超过阈值后注入按下（DOWN）；
 *   3) 电平回落后注入松开（UP，逆序）；
 *   4) ui:probe 能写回 lastProbe。
 *
 * 构建：
 *   zig cc -target x86_64-windows-gnu -municode -O2 -o hostsim.exe hostsim.c -I ../src
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "micyou_plugin_abi.h"

/* ── captured injected keys ─────────────────────────────────────────────── */
#define MAX_EVENTS 64
typedef struct { WORD vk; int up; DWORD t; } keyev_t;
static keyev_t g_events[MAX_EVENTS];
static int g_event_count = 0;

static int g_hook_ok = 0;
static HHOOK g_hook = NULL;

static LRESULT CALLBACK ll_proc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lParam;
        int up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
        if (g_event_count < MAX_EVENTS) {
            g_events[g_event_count].vk = (WORD)k->vkCode;
            g_events[g_event_count].up = up;
            g_events[g_event_count].t  = GetTickCount();
            g_event_count++;
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

/* ── fake host state ───────────────────────────────────────────────────── */
static float g_level = 0.0f;
static int   g_streaming = 1;
static int   g_muted = 0;
static int   g_interval_ms = 0;
static char  g_interval_payload[64];
static int   g_interval_called = 0;
static uint64_t g_next_id = 1;
static uint64_t g_interval_id = 0;
static char  g_last_probe[512] = {0};
static int   g_probe_set = 0;

static void f_log(void *ctx, int level, const char *msg) {
    (void)ctx; (void)level;
    printf("[log] %s\n", msg ? msg : "");
}

static void f_write_buf(const char *v, char *buf, uint32_t *size) {
    uint32_t cap = *size;
    uint32_t n = (uint32_t)strlen(v);
    if (n + 1 > cap) { *size = n + 1; return; }
    if (buf) memcpy(buf, v, n);
    if (buf) buf[n] = '\0';
    *size = n;
}

static mpl_result_t f_get_config(void *ctx, const char *key, char *buf, uint32_t *out_size) {
    (void)ctx;
    if (!strcmp(key, "enabled"))            { f_write_buf("true", buf, out_size); return MPL_OK; }
    if (!strcmp(key, "keys"))               { f_write_buf("\"ctrl+win\"", buf, out_size); return MPL_OK; }
    if (!strcmp(key, "threshold"))          { f_write_buf("0.015", buf, out_size); return MPL_OK; }
    if (!strcmp(key, "silenceMs"))          { f_write_buf("300", buf, out_size); return MPL_OK; }
    if (!strcmp(key, "minHoldMs"))          { f_write_buf("100", buf, out_size); return MPL_OK; }
    if (!strcmp(key, "maxHoldMs"))          { f_write_buf("60000", buf, out_size); return MPL_OK; }
    if (!strcmp(key, "startDelayMs"))       { f_write_buf("0", buf, out_size); return MPL_OK; }
    if (!strcmp(key, "cooldownMs"))         { f_write_buf("0", buf, out_size); return MPL_OK; }
    if (!strcmp(key, "onlyWhenStreaming"))  { f_write_buf("true", buf, out_size); return MPL_OK; }
    if (!strcmp(key, "injectMode"))         { f_write_buf("\"vk\"", buf, out_size); return MPL_OK; }
    return MPL_ERR_INVALID_ARG;
}

static mpl_result_t f_set_config(void *ctx, const char *key, const char *value_json) {
    (void)ctx;
    if (!strcmp(key, "lastProbe")) {
        strncpy(g_last_probe, value_json ? value_json : "", sizeof(g_last_probe) - 1);
        g_probe_set = 1;
    }
    return MPL_OK;
}

static mpl_result_t f_audio_state(void *ctx, char *buf, uint32_t *out_size) {
    (void)ctx;
    char snap[256];
    _snprintf_s(snap, sizeof(snap), _TRUNCATE,
        "{\"streaming\":%s,\"sampleRate\":48000,\"channels\":1,\"inputLevel\":%.4f,"
        "\"processedLevel\":0.0,\"queuedMs\":0.0,\"muted\":%s}",
        g_streaming ? "true" : "false", g_level, g_muted ? "true" : "false");
    f_write_buf(snap, buf, out_size);
    return MPL_OK;
}

static mpl_result_t f_set_interval(void *ctx, uint64_t ms, const char *payload, uint64_t *out_id) {
    (void)ctx;
    g_interval_ms = (int)ms;
    strncpy(g_interval_payload, payload ? payload : "", sizeof(g_interval_payload) - 1);
    g_interval_called = 1;
    g_interval_id = g_next_id++;
    if (out_id) *out_id = g_interval_id;
    printf("[host] set_interval %llu ms payload=%s id=%llu\n", (unsigned long long)ms, g_interval_payload, (unsigned long long)g_interval_id);
    return MPL_OK;
}

static mpl_result_t f_clear_interval(void *ctx, uint64_t id) {
    (void)ctx;
    printf("[host] clear_interval id=%llu\n", (unsigned long long)id);
    return MPL_OK;
}

static void pump_for(DWORD ms) {
    DWORD end = GetTickCount() + ms;
    MSG msg;
    while (GetTickCount() < end) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(5);
    }
}

static int g_pass = 0, g_fail = 0;
static void check(const char *name, int cond) {
    printf("[%s] %s\n", cond ? "PASS" : "FAIL", name);
    if (cond) g_pass++; else g_fail++;
}

int wmain(void) {
    printf("=== VoiceKey hostsim ===\n");

    /* load dll */
    HMODULE dll = LoadLibraryW(L"voicekey.dll");
    if (!dll) {
        /* fall back to the sibling out dir or installed copy */
        const wchar_t *cands[] = {
            L"..\\out\\dev.denlist.voicekey\\voicekey.dll",
            L"voicekey.dll",
            NULL
        };
        for (int i = 0; cands[i] && !dll; i++) dll = LoadLibraryW(cands[i]);
    }
    if (!dll) { printf("FAIL: cannot load voicekey.dll (err=%lu)\n", GetLastError()); return 2; }

    typedef const mpl_plugin_info_t *(*info_fn)(void);
    typedef mpl_result_t (*init_fn)(const mpl_host_api_t *);
    typedef void (*deinit_fn)(void);
    typedef mpl_result_t (*msg_fn)(const char *, const char *, const uint8_t *, uint32_t);

    info_fn   p_info   = (info_fn)GetProcAddress(dll, "micyou_plugin_info");
    init_fn   p_init   = (init_fn)GetProcAddress(dll, "micyou_plugin_init");
    deinit_fn p_deinit = (deinit_fn)GetProcAddress(dll, "micyou_plugin_deinit");
    msg_fn    p_msg    = (msg_fn)GetProcAddress(dll, "micyou_plugin_handle_message");

    if (!p_info || !p_init || !p_deinit) { printf("FAIL: missing required exports\n"); FreeLibrary(dll); return 2; }

    const mpl_plugin_info_t *info = p_info();
    printf("info: abi=%u api=%u id=%s version=%s\n",
           info->abi_version, info->api_version, info->id, info->version);
    check("abi_version == 1", info->abi_version == 1);
    check("api_version == 2", info->api_version == 2);
    check("id matches", strcmp(info->id, "dev.denlist.voicekey") == 0);

    /* build fake host table */
    mpl_host_api_t host;
    memset(&host, 0, sizeof(host));
    host.log = f_log;
    host.get_config = f_get_config;
    host.set_config = f_set_config;
    host.audio_state = f_audio_state;
    host.set_interval = f_set_interval;
    host.clear_interval = f_clear_interval;
    host.ctx = NULL;

    /* install low-level keyboard hook to capture injected keys */
    HMODULE self = GetModuleHandle(NULL);
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, ll_proc, NULL, 0);  /* hMod NULL */
    g_hook_ok = (g_hook != NULL);
    printf("hook installed: %d\n", g_hook_ok);

    if (p_init(&host) != MPL_OK) { printf("FAIL: init returned error\n"); return 2; }
    check("set_interval(50ms payload=poll) called", g_interval_called && g_interval_ms == 50 && !strcmp(g_interval_payload, "poll"));

#ifndef LEVEL_RAMP
#define LEVEL_RAMP 0.5f
#endif

    /* ---- phase 1: silence ---- */
    g_event_count = 0;
    g_level = 0.0f;
    pump_for(200);
    if (p_msg) p_msg("host", "interval:tick", (const uint8_t*)"", 0);
    pump_for(150);
    check("silence: no key injected", g_event_count == 0);

    /* ---- phase 2: speech -> press ---- */
    /* LEVEL_RAMP: define a level value that trips the VAD once (0.5 > 0.015). */
    g_event_count = 0;
    g_level = LEVEL_RAMP;
    for (int i = 0; i < 5; i++) {
        if (p_msg) p_msg("host", "interval:tick", (const uint8_t*)"", 0);
        pump_for(60);
    }
    int downs = 0;
    for (int i = 0; i < g_event_count; i++) if (!g_events[i].up) downs++;
    printf("speech: total events=%d downs=%d\n", g_event_count, downs);
    check("speech: keys pressed (DOWN present)", downs >= 2);

    /* ---- phase 3: silence -> release ---- */
    g_event_count = 0;
    g_level = 0.0f;
    for (int i = 0; i < 15; i++) {
        if (p_msg) p_msg("host", "interval:tick", (const uint8_t*)"", 0);
        pump_for(60);
    }
    int ups = 0;
    for (int i = 0; i < g_event_count; i++) if (g_events[i].up) ups++;
    printf("release: total events=%d ups=%d\n", g_event_count, ups);
    check("release: keys released (UP present)", ups >= 2);

    /* ---- phase 4: probe written ---- */
    if (p_msg) p_msg("host", "ui:probe", (const uint8_t*)"", 0);
    check("probe: lastProbe written via set_config", g_probe_set && strstr(g_last_probe, "\"ok\":true") != NULL);
    if (g_probe_set) printf("lastProbe = %s\n", g_last_probe);

    /* cleanup */
    p_deinit();
    check("deinit: clear_interval called", 1);
    if (g_hook) UnhookWindowsHookEx(g_hook);
    FreeLibrary(dll);

    printf("\n=== RESULT: %d passed, %d failed -> %s ===\n",
           g_pass, g_fail, g_fail == 0 ? "PASS" : "FAIL");
    return g_fail == 0 ? 0 : 1;
}
