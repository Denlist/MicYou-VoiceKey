/*
 * preview.c — 生成 640×360 的插件封面 preview.png（纯 Win32 GDI 绘制）。
 *
 * 构建（与 savepng.c 一起，链接 gdi32，不要链接 gdiplus）：
 *   zig cc -target x86_64-windows-gnu -municode -O2 -o mkpreview.exe preview.c savepng.c -lgdi32
 * 运行：
 *   mkpreview.exe preview.png
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

extern int save_png_gdiplus(HBITMAP hbmp, int w, int h, const wchar_t *path);

#define W 640
#define H 360

static void fill_gradient(HDC dc) {
    /* 深蓝黑渐变，从上到下略有明暗 */
    for (int y = 0; y < H; y++) {
        int t = y * 100 / H;
        int r = 15 + t * 6 / 100;
        int g = 20 + t * 8 / 100;
        int b = 32 + t * 14 / 100;
        HBRUSH br = CreateSolidBrush(RGB(r, g, b));
        RECT rc = { 0, y, W, y + 1 };
        FillRect(dc, &rc, br);
        DeleteObject(br);
    }
}

static void draw_glow(HDC dc) {
    /* 右上角光晕 */
    HBRUSH br = CreateSolidBrush(RGB(28, 46, 78));
    HGDIOBJ old = SelectObject(dc, br);
    for (int i = 0; i < 60; i++) {
        int m = i * 2;
        Ellipse(dc, W - 240 - m, -120 - m, W + 40 + m, 120 + m);
    }
    SelectObject(dc, old);
    DeleteObject(br);
}

static void text(HDC dc, const wchar_t *s, int x, int y, int size, int bold, COLORREF color, const wchar_t *font) {
    HFONT f = CreateFontW(size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, font);
    HGDIOBJ of = SelectObject(dc, f);
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, x, y, s, (int)wcslen(s));
    SelectObject(dc, of);
    DeleteObject(f);
}

/* 画一个圆角卡片 */
static void card(HDC dc, int x, int y, int w, int h) {
    HBRUSH br = CreateSolidBrush(RGB(26, 33, 50));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(44, 54, 80));
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
    RoundRect(dc, x, y, x + w, y + h, 12, 12);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(br); DeleteObject(pen);
}

static void capsule(HDC dc, int x, int y, int w, int h, const wchar_t *label, int accent) {
    HBRUSH br = CreateSolidBrush(accent ? RGB(34, 43, 64) : RGB(34, 43, 64));
    HPEN pen = CreatePen(PS_SOLID, 1, accent ? RGB(79, 142, 247) : RGB(44, 54, 80));
    HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pen);
    RoundRect(dc, x, y, x + w, y + h, 8, 8);
    SelectObject(dc, ob); SelectObject(dc, op);
    DeleteObject(br); DeleteObject(pen);
    text(dc, label, x + 10, y + (h - 16) / 2, 15, 1, RGB(230, 235, 245), L"Segoe UI");
}

int wmain(int argc, wchar_t **argv) {
    const wchar_t *out = (argc > 1) ? argv[1] : L"preview.png";

    HDC screen = GetDC(NULL);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, W, H);
    HGDIOBJ old = SelectObject(dc, bmp);

    fill_gradient(dc);
    draw_glow(dc);

    /* 标题 */
    text(dc, L"VoiceKey", 48, 30, 40, 1, RGB(255, 255, 255), L"Consolas");
    text(dc, L"语音自动按键 · MicYou 原生插件", 50, 82, 16, 0, RGB(150, 165, 195), L"Microsoft YaHei");
    text(dc, L"对着手机说话 → 自动按住 Ctrl+Win → 微信输入法语音输入", 50, 110, 15, 0, RGB(90, 160, 255), L"Microsoft YaHei");

    /* 绑定按键卡片 */
    card(dc, 48, 150, 380, 74);
    text(dc, L"绑定按键", 64, 160, 13, 0, RGB(139, 150, 173), L"Microsoft YaHei");
    capsule(dc, 64, 184, 70, 30, L"Ctrl", 1);
    text(dc, L"+", 142, 190, 16, 1, RGB(139, 150, 173), L"Segoe UI");
    capsule(dc, 160, 184, 64, 30, L"Win", 1);
    text(dc, L"微信输入法默认键", 236, 191, 13, 0, RGB(139, 150, 173), L"Microsoft YaHei");

    /* 实时状态卡片 */
    card(dc, 48, 236, 380, 74);
    text(dc, L"实时状态", 64, 246, 13, 0, RGB(139, 150, 173), L"Microsoft YaHei");
    HBRUSH led = CreateSolidBrush(RGB(61, 220, 132));
    HGDIOBJ ol = SelectObject(dc, led);
    Ellipse(dc, 66, 274, 78, 286);
    SelectObject(dc, ol); DeleteObject(led);
    text(dc, L"正在按住 Ctrl+Win", 86, 270, 14, 0, RGB(230, 235, 245), L"Microsoft YaHei");
    /* 电平条 */
    HBRUSH track = CreateSolidBrush(RGB(34, 43, 64));
    RECT rt = { 66, 294, 410, 300 }; FillRect(dc, &rt, track); DeleteObject(track);
    HBRUSH fill = CreateSolidBrush(RGB(79, 142, 247));
    RECT rf = { 66, 294, 66 + 210, 300 }; FillRect(dc, &rf, fill); DeleteObject(fill);

    /* 右侧要点 */
    const wchar_t *points[] = {
        L"按住说话语义，真的按住再松开",
        L"按键可自定义，面板里按键录入",
        L"实时电平条 + 状态灯",
        L"静音超时 / 超长保险，绝不卡键",
        L"不改动音频，只读电平发按键",
    };
    int py = 152;
    for (int i = 0; i < 5; i++) {
        text(dc, L"\u2713", 452, py, 14, 1, RGB(61, 220, 132), L"Segoe UI");
        text(dc, points[i], 472, py, 13, 0, RGB(200, 210, 230), L"Microsoft YaHei");
        py += 28;
    }

    /* 底部 */
    text(dc, L"dev.denlist.voicekey", 48, 328, 12, 0, RGB(110, 125, 155), L"Consolas");
    text(dc, L"Windows · native", 508, 328, 12, 0, RGB(110, 125, 155), L"Consolas");

    SelectObject(dc, old);
    int ok = save_png_gdiplus(bmp, W, H, out);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(NULL, screen);

    /* GDI+ 成功时 Ok=0 */
    if (ok == 0 && GetFileAttributesW(out) != INVALID_FILE_ATTRIBUTES) {
        wprintf(L"preview saved: %ls\n", out);
        return 0;
    }
    wprintf(L"FAIL: save_png_gdiplus returned %d\n", ok);
    return 1;
}
