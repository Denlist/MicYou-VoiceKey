/*
 * savepng.c — 用 GDI+ 把一个 HBITMAP 存成 PNG。
 *
 * 关键坑（zig gnu 目标）：静态链接 -lgdiplus 时 GdiplusStartup 返回 status=2
 * (Win32Error)。可行解法是运行时 LoadLibraryW("gdiplus.dll") + GetProcAddress，
 * 并把 GdiplusStartupInput 清零后显式设置 GdiplusVersion = 1。
 * 链接时只连 gdi32，不要连 gdiplus。
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* GDI+ 最小 ABI 声明（避免依赖 gdiplus headers） */
typedef struct {
    UINT32 GdiplusVersion;
    void  *DebugEventCallback;
    BOOL   SuppressBackgroundThread;
    BOOL   SuppressExternalCodecs;
} GdiplusStartupInput;

typedef struct {
    CLSID Clsid;
    GUID  FormatID;
    const wchar_t *CodecName;
    const wchar_t *DllName;
    const wchar_t *FormatDescription;
    const wchar_t *FilenameExtension;
    const wchar_t *MimeType;
    DWORD Flags;
    DWORD Version;
    DWORD SigCount;
    DWORD SigSize;
    const BYTE *SigPattern;
    const BYTE *SigMask;
} ImageCodecInfo;

typedef int (WINAPI *pGdiplusStartup)(ULONG_PTR *, const GdiplusStartupInput *, void *);
typedef void (WINAPI *pGdiplusShutdown)(ULONG_PTR);
typedef int (WINAPI *pGdipCreateBitmapFromHBITMAP)(HBITMAP, HPALETTE, void **);
typedef int (WINAPI *pGdipSaveImageToFile)(void *, const wchar_t *, const CLSID *, void *);
typedef int (WINAPI *pGdipDisposeImage)(void *);

int save_png_gdiplus(HBITMAP hbmp, int w, int h, const wchar_t *path) {
    (void)w; (void)h;
    HMODULE lib = LoadLibraryW(L"gdiplus.dll");
    if (!lib) return -1;

    pGdiplusStartup              Startup   = (pGdiplusStartup)GetProcAddress(lib, "GdiplusStartup");
    pGdiplusShutdown             Shutdown  = (pGdiplusShutdown)GetProcAddress(lib, "GdiplusShutdown");
    pGdipCreateBitmapFromHBITMAP Create    = (pGdipCreateBitmapFromHBITMAP)GetProcAddress(lib, "GdipCreateBitmapFromHBITMAP");
    pGdipSaveImageToFile         SaveFile  = (pGdipSaveImageToFile)GetProcAddress(lib, "GdipSaveImageToFile");
    pGdipDisposeImage            Dispose   = (pGdipDisposeImage)GetProcAddress(lib, "GdipDisposeImage");

    if (!Startup || !Shutdown || !Create || !SaveFile || !Dispose) {
        FreeLibrary(lib);
        return -2;
    }

    GdiplusStartupInput gsi;
    ZeroMemory(&gsi, sizeof(gsi));
    gsi.GdiplusVersion = 1;              /* 必须显式置 1，否则 status=2 */

    ULONG_PTR token = 0;
    int st = Startup(&token, &gsi, NULL);
    if (st != 0) { FreeLibrary(lib); return st; }

    void *bmp = NULL;
    st = Create(hbmp, NULL, &bmp);
    if (st != 0) { Shutdown(token); FreeLibrary(lib); return 100 + st; }

    /* PNG 编码器 CLSID */
    CLSID pngClsid = { 0x557CF406, 0x1A04, 0x11D3, { 0x9A, 0x73, 0x00, 0x00, 0xF8, 0x1E, 0xF3, 0x2E } };

    st = SaveFile(bmp, path, &pngClsid, NULL);
    Dispose(bmp);
    Shutdown(token);
    FreeLibrary(lib);
    return (int)st;
}
