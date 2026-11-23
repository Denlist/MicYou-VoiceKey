/*
 * loadprobe — 复刻 MicYou native.rs 的加载校验链，验证 DLL 能被宿主接受。
 *
 * 顺序与 native.rs 一致：
 *   LoadLibraryW -> micyou_plugin_info -> 校验 abi==1 / api in [1,2] / id 与
 *   manifest 一致 -> GetProcAddress(可选 process/process_node/handle_event/
 *   handle_message, 必需 init/deinit) -> 建假 host 表 -> init -> deinit -> Free。
 *
 * 构建：
 *   zig cc -target x86_64-windows-gnu -municode -O2 -o loadprobe.exe loadprobe.c -I ../src
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "micyou_plugin_abi.h"

static void f_log(void *ctx, int level, const char *msg) {
    (void)ctx; (void)level;
    printf("[log] %s\n", msg ? msg : "");
}
static void wbuf(const char *v, char *buf, uint32_t *size) {
    uint32_t cap = *size;
    uint32_t l = (uint32_t)strlen(v);
    if (l + 1 > cap) { *size = l + 1; return; }
    memcpy(buf, v, l); buf[l] = '\0'; *size = l;
}
static mpl_result_t f_get_config(void *ctx, const char *k, char *b, uint32_t *o) {
    (void)ctx;
    if (!strcmp(k, "keys")) { wbuf("\"ctrl+win\"", b, o); return MPL_OK; }
    return MPL_ERR_INVALID_ARG;
}
static mpl_result_t f_set_config(void *ctx, const char *k, const char *v) { (void)ctx;(void)k;(void)v; return MPL_OK; }
static mpl_result_t f_audio_state(void *ctx, char *b, uint32_t *o) {
    (void)ctx;
    wbuf("{\"streaming\":false,\"inputLevel\":0.0,\"muted\":false}", b, o);
    return MPL_OK;
}
static mpl_result_t f_set_interval(void *ctx, uint64_t ms, const char *p, uint64_t *id) {
    (void)ctx;
    printf("[host] set_interval %llu ms payload=%s\n", (unsigned long long)ms, p ? p : "");
    if (id) *id = 7;
    return MPL_OK;
}
static mpl_result_t f_clear_interval(void *ctx, uint64_t id) {
    (void)ctx;
    printf("[host] clear_interval id=%llu\n", (unsigned long long)id);
    return MPL_OK;
}

int wmain(int argc, wchar_t **argv) {
    const wchar_t *path = (argc > 1) ? argv[1] : L"voicekey.dll";
    const char *expect_id = "dev.denlist.voicekey";

    printf("=== VoiceKey loadprobe ===\n");
    printf("loading: %ls\n", path);

    HMODULE dll = LoadLibraryW(path);
    if (!dll) { printf("FAIL: LoadLibrary failed (err=%lu)\n", GetLastError()); return 2; }

    typedef const mpl_plugin_info_t *(*info_fn)(void);
    info_fn p_info = (info_fn)GetProcAddress(dll, "micyou_plugin_info");
    if (!p_info) { printf("FAIL: missing micyou_plugin_info\n"); FreeLibrary(dll); return 2; }

    const mpl_plugin_info_t *info = p_info();
    printf("info: abi=%u api=%u id=%s version=%s\n",
           info->abi_version, info->api_version, info->id, info->version);

    int ok = 1;
    if (info->abi_version != 1) { printf("FAIL: abi_version != 1\n"); ok = 0; }
    if (info->api_version < 1 || info->api_version > 2) { printf("FAIL: api_version out of [1,2]\n"); ok = 0; }
    if (strcmp(info->id, expect_id) != 0) { printf("FAIL: id mismatch (got %s, want %s)\n", info->id, expect_id); ok = 0; }
    if (!ok) { FreeLibrary(dll); printf("RESULT: FAIL\n"); return 1; }

    /* optional symbols */
    void *p_process      = (void *)GetProcAddress(dll, "micyou_plugin_process");
    void *p_process_node = (void *)GetProcAddress(dll, "micyou_plugin_process_node");
    void *p_handle_event = (void *)GetProcAddress(dll, "micyou_plugin_handle_event");
    void *p_handle_msg   = (void *)GetProcAddress(dll, "micyou_plugin_handle_message");
    printf("optional: process=%p process_node=%p handle_event=%p handle_message=%p\n",
           p_process, p_process_node, p_handle_event, p_handle_msg);

    typedef mpl_result_t (*init_fn)(const mpl_host_api_t *);
    typedef void (*deinit_fn)(void);
    init_fn	p_init   = (init_fn)GetProcAddress(dll, "micyou_plugin_init");
    deinit_fn	p_deinit = (deinit_fn)GetProcAddress(dll, "micyou_plugin_deinit");
    if (!p_init || !p_deinit) { printf("FAIL: missing init/deinit\n"); FreeLibrary(dll); printf("RESULT: FAIL\n"); return 1; }

    mpl_host_api_t host;
    memset(&host, 0, sizeof(host));
    host.log = f_log;
    host.get_config = f_get_config;
    host.set_config = f_set_config;
    host.audio_state = f_audio_state;
    host.set_interval = f_set_interval;
    host.clear_interval = f_clear_interval;

    mpl_result_t r = p_init(&host);
    printf("init -> %d %s\n", r, r == MPL_OK ? "(MPL_OK)" : "(ERROR)");
    if (r != MPL_OK) { FreeLibrary(dll); printf("RESULT: FAIL\n"); return 1; }

    p_deinit();
    FreeLibrary(dll);
    printf("RESULT: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
