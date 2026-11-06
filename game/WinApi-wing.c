/**
 *
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 *  the Software, and to permit persons to whom the Software is furnished to do so,
 *  subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 *  FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 *  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 *  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/* The WinG layer of Comix Zone: see WinApi-wing.h.
 *
 * The game never talks to the system directly: it asks WinG for a device
 * context, a bitmap and a palette, and it blits the picture with
 * WinGStretchBlt. The device context of the game is a pseudo handle, the
 * picture itself lives in a palette-indexed buffer, and the window that shows
 * it is an SDL window of its own. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <SDL.h>

#include "Game_defs.h"
#include "Game_hooks.h"
#include "Game_memory.h"
#include "Game_thread.h"
#include "WinApi.h"
#include "WinApi-wing.h"
#include "Game_ui.h"
#include "Game_osd.h"
#include "WinApi-ui.h"
#include "Game_gl.h"
#include "Game_vars.h"

#define WING_LOG(...) do { if (wing_log) fprintf(stderr, "wing: " __VA_ARGS__); } while (0)

static int wing_log = 0; /* $CZ_WING_LOG: every call of the layer to stderr */

/* --- the Win32 structures the game uses --- */

typedef struct {
    int32_t left, top, right, bottom;
} wing_rect;

typedef struct {
    uint32_t hdc;
    uint32_t fErase;
    wing_rect rcPaint;
    uint32_t fRestore;
    uint32_t fIncUpdate;
    uint8_t rgbReserved[32];
} wing_paintstruct;

typedef struct {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} wing_bmiheader;

typedef struct {
    uint8_t bBlue, bGreen, bRed, bFlags;
} wing_paletteentry;

typedef struct {
    uint16_t lVersion;
    uint16_t cEntries;
    wing_paletteentry aEntries[256];
} wing_logpalette;

typedef struct {
    uint32_t dwData;
    uint32_t cbData;
    uint32_t lpData;
} wing_copydatastruct;

/* --- the state of the layer --- */

typedef struct {
    SDL_Window *window;

    /* The picture goes through OpenGL, with the filter of Game_gl.h
     * (Game_GlFilter: Display_Shader, a mod, or the panel's) */
    SDL_GLContext glctx;
    GLuint gltex;

    int width, height;        /* the picture in the texture */

    /* the picture comes as the Microsoft Display Device's (WM_COPYDATA): the
     * game is in its mode "Full Screen" (Esc), and the window is too */
    int display_device;

    /* The picture lives in the memory of the guest: the game draws into it
     * itself (its own software renderer) and hands it over to the system
     * (the WinG bitmap or the display device).  WinGCreateBitmap is where
     * the game gets the address of that memory from. */
    uint32_t guest_bits;      /* the address of the picture in the guest */
    int guest_width, guest_height;

    uint8_t palette[256][4];  /* RGBA, the palette of the game */
    int has_palette;

    wing_bmiheader bmi;       /* the format the game asked for */
    int has_bmi;

    wing_logpalette logpal;   /* the palette the game made with CreatePalette */
    int has_logpal;

    int dc_created;
    int bitmap_created;
} wing_state;

static wing_state wing;

/* --- the window --- */

/* the picture of the game is there to draw into */
static int wing_has_texture(void)
{
    return wing.gltex != 0;
}

/* the picture at this size (a mode change of the game) */
static int wing_texture_size(int w, int h)
{
    wing.width = w;
    wing.height = h;
    glBindTexture(GL_TEXTURE_2D, wing.gltex);
    Game_GlTexImageBgra(0, w, h, NULL);
    return 1;
}

static void wing_texture_upload(const uint32_t *rgba, int w, int h)
{
    glBindTexture(GL_TEXTURE_2D, wing.gltex);
    Game_GlTexSubImageBgra(w, h, rgba);
}

static void wing_present(void);

static void wing_create_window(void)
{
    int w, h;

    if (wing.window != NULL)
    {
        return;
    }

    if (getenv("CZ_WING_LOG") != NULL)
    {
        wing_log = 1;
    }

    /* The window of the port stays hidden (Win32 games open their own, as the
     * DirectDraw layer does), and the thread that draws owns the OpenGL. */
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    {
        fprintf(stderr, "wing: SDL video initialization failed: %s\n", SDL_GetError());
        return;
    }

    /* the picture's size, 320x240 at 4:3; the player makes it bigger */
    wing.window = Game_WindowCreate(WING_WIDTH, WING_HEIGHT, SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    if (wing.window == NULL)
    {
        fprintf(stderr, "wing: the window: %s\n", SDL_GetError());
        return;
    }
    /* the window menu holds the game while it is open and shows the picture again */
    Game_UiSetRepaint(wing_present);

    /* without OpenGL it says so and ends the program */
    wing.glctx = Game_GlCreateContext(wing.window, 1);
    glGenTextures(1, &wing.gltex);
    glBindTexture(GL_TEXTURE_2D, wing.gltex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    wing.width = WING_WIDTH;
    wing.height = WING_HEIGHT;
    Game_GlTexImageBgra(0, wing.width, wing.height, NULL);
    Game_ShowWindow(wing.window);

    /* the picture of the game is 320x240, the window may be bigger */
    SDL_GetWindowSize(wing.window, &w, &h);
    WING_LOG("window %ix%i\n", w, h);
}

/* The picture fills a 4:3 display, whatever its size in pixels (the game
 * stretches 320x224 to that too); the rest of the window stays black. */
static void wing_present(void)
{
    if (wing.gltex == 0)
    {
        return;
    }
    Game_OsdFrame(wing.width, wing.height, "OpenGL");
    Game_GlPresentAspect(wing.window, wing.gltex, wing.width, wing.height, 4, 3, 1, 0, Game_GlFilter());
}

/* shows w x h pixels of a palette-indexed picture with rows of stride bytes */
static int wing_show(const uint8_t *pixels, int stride, int w, int h)
{
    uint32_t *rgba;
    int x, y;

    if (!wing_has_texture() || !wing_texture_size(w, h))
    {
        return 0;
    }

    rgba = (uint32_t *)malloc((size_t)w * h * 4);
    if (rgba == NULL)
    {
        return 0;
    }

    for (y = 0; y < h; y++)
    {
        const uint8_t *row = pixels + (size_t)y * stride;
        for (x = 0; x < w; x++)
        {
            uint8_t c = row[x];

            rgba[y * w + x] = ((uint32_t)wing.palette[c][3] << 24) | ((uint32_t)wing.palette[c][0] << 16) |
                              ((uint32_t)wing.palette[c][1] << 8) | (uint32_t)wing.palette[c][2];
        }
    }

    wing_texture_upload(rgba, w, h);
    free(rgba);

    wing_present();
    return 1;
}

#ifdef __cplusplus
extern "C" {
#endif

/* --- the WinG API --- */

uint32_t CCALL WinGCreateDC_c(void)
{
    WING_LOG("WinGCreateDC\n");

    wing_create_window();
    wing.dc_created = 1;

    return WING_DC_HANDLE;
}

uint32_t CCALL WinGRecommendDIBFormat_c(uint32_t pbmi)
{
    wing_bmiheader *bmi = (wing_bmiheader *)GUEST_MEM(pbmi);

    WING_LOG("WinGRecommendDIBFormat(%08x)\n", pbmi);

    if (bmi == NULL)
    {
        return 0;
    }

    memset(bmi, 0, sizeof(*bmi));
    bmi->biSize = sizeof(wing_bmiheader);
    bmi->biWidth = WING_WIDTH;
    bmi->biHeight = -WING_HEIGHT;
    bmi->biPlanes = 1;
    bmi->biBitCount = 8;
    bmi->biCompression = 0;
    bmi->biSizeImage = WING_WIDTH * WING_HEIGHT;

    memcpy(&wing.bmi, bmi, sizeof(wing.bmi));
    wing.has_bmi = 1;

    /* the game takes the recommended format and uses it as it is */
    return 1;
}

uint32_t CCALL WinGCreateBitmap_c(uint32_t dc, uint32_t pbmi, uint32_t pcolors)
{
    wing_bmiheader bmi;

    WING_LOG("WinGCreateBitmap(%08x, %08x, %08x)\n", dc, pbmi, pcolors);

    if (pbmi != 0)
    {
        memcpy(&bmi, GUEST_MEM(pbmi), sizeof(bmi));
    }
    else if (wing.has_bmi)
    {
        bmi = wing.bmi;
    }
    else
    {
        bmi.biWidth = WING_WIDTH;
        bmi.biHeight = -WING_HEIGHT;
    }

    if (bmi.biWidth <= 0)
    {
        bmi.biWidth = WING_WIDTH;
    }
    if (bmi.biHeight == 0)
    {
        bmi.biHeight = -WING_HEIGHT;
    }
    if (bmi.biHeight > 0)
    {
        bmi.biHeight = -bmi.biHeight;
    }

    wing.bitmap_created = 1;

    /* The picture itself is drawn by the game, into the memory of the
     * guest: the third argument of WinGCreateBitmap is where the game
     * expects the address of that memory, and the game copies it into the
     * variable of its "picture buffer" (0x62a174).  Without it the game
     * has no picture at all and never draws anything. */
    if (pcolors != 0)
    {
        uint32_t *out = (uint32_t *)GUEST_MEM(pcolors);

        if (out != NULL)
        {
            int width = (bmi.biWidth > 0) ? bmi.biWidth : WING_WIDTH;
            int height = (bmi.biHeight < 0) ? -bmi.biHeight : bmi.biHeight;
            uint32_t size;
            void *bits;

            if (width <= 0 || height <= 0) { width = WING_WIDTH; height = WING_HEIGHT; }
            size = (uint32_t)width * (uint32_t)height;

            /* The game keeps five screens of picture in this buffer (it clears
             * 384000 bytes at the offset 8 of it), so it must be that big. */
            if (size < 384016u) size = 384016u;

            bits = x86_malloc(size);
            if (bits != NULL)
            {
                memset(bits, 0, size);
                wing.guest_bits = GUEST_ADDR(bits);
                wing.guest_width = width;
                wing.guest_height = height;
                *out = wing.guest_bits;

                WING_LOG("picture buffer %08x, %ix%i\n", wing.guest_bits, width, height);
            }
        }
    }

    return WING_BITMAP_HANDLE;
}

uint32_t CCALL WinGSetDIBColorTable_c(uint32_t dc, uint32_t start, uint32_t count, uint32_t table)
{
    const uint8_t *entries = (const uint8_t *)GUEST_MEM(table);
    uint32_t i;

    WING_LOG("WinGSetDIBColorTable(%08x, %i, %i, %08x)\n", dc, start, count, table);

    if (entries == NULL || count == 0 || count > 256)
    {
        return 0;
    }

    for (i = 0; i < count; i++)
    {
        uint32_t c = start + i;
        if (c > 255)
        {
            break;
        }
        /* the entries of the game are BGRX */
        wing.palette[c][0] = entries[i * 4 + 2];
        wing.palette[c][1] = entries[i * 4 + 1];
        wing.palette[c][2] = entries[i * 4 + 0];
        wing.palette[c][3] = 255;
    }

    wing.has_palette = 1;

    return 1;
}

uint32_t CCALL WinGStretchBlt_c(uint32_t dest, uint32_t xdest, uint32_t ydest,
                                uint32_t wdest, uint32_t hdest, uint32_t src,
                                uint32_t xsrc, uint32_t ysrc, uint32_t wsrc, uint32_t hsrc)
{
    const uint8_t *src_pixels;
    int sw, sh;

    WING_LOG("WinGStretchBlt(%08x, %i, %i, %i, %i, %08x, %i, %i, %i, %i)\n",
             dest, xdest, ydest, wdest, hdest, src, xsrc, ysrc, wsrc, hsrc);
    (void)dest; (void)xdest; (void)ydest; (void)wdest; (void)hdest; (void)src;

    /* the game is back in its window from the mode "Full Screen" (Esc) */
    if (wing.display_device)
    {
        wing.display_device = 0;
        Game_WindowSetFullscreen(0);
    }

    /* the picture the game has drawn is in the memory of the guest */
    src_pixels = (wing.guest_bits != 0) ? (const uint8_t *)GUEST_MEM(wing.guest_bits) : NULL;
    if (src_pixels == NULL)
    {
        return 0;
    }

    sw = (wsrc != 0) ? (int)wsrc : wing.guest_width;
    sh = (hsrc != 0) ? (int)hsrc : wing.guest_height;
    if (wing.guest_width < (int)xsrc + sw) sw = wing.guest_width - (int)xsrc;
    if (wing.guest_height < (int)ysrc + sh) sh = wing.guest_height - (int)ysrc;
    if (sw <= 0 || sh <= 0)
    {
        return 0;
    }

    /* The game stretches the picture to the size of its window; the port
     * keeps the picture at its own size and scales it into the window
     * (whatever size the window has). */
    return wing_show(src_pixels + (size_t)ysrc * wing.guest_width + xsrc, wing.guest_width, sw, sh);
}

/* --- the device context and the window --- */

uint32_t CCALL GetDC_c(uint32_t hwnd)
{
    WING_LOG("GetDC(%08x)\n", hwnd);

    wing_create_window();

    return WING_DC_HANDLE;
}

uint32_t CCALL ReleaseDC_c(uint32_t hwnd, uint32_t hdc)
{
    WING_LOG("ReleaseDC(%08x, %08x)\n", hwnd, hdc);

    return 1;
}



uint32_t CCALL GetClientRect_c(uint32_t hwnd, uint32_t rect)
{
    (void)hwnd;
    wing_rect *r = (wing_rect *)GUEST_MEM(rect);
    int w = WING_WIDTH, h = WING_HEIGHT;

    if (wing.window != NULL)
    {
        SDL_GetWindowSize(wing.window, &w, &h);
    }

    if (r != NULL)
    {
        r->left = 0;
        r->top = 0;
        r->right = w;
        r->bottom = h;
    }

    return 1;
}

uint32_t CCALL GetWindowRect_c(uint32_t hwnd, uint32_t rect)
{
    (void)hwnd;
    wing_rect *r = (wing_rect *)GUEST_MEM(rect);
    int w = WING_WIDTH, h = WING_HEIGHT, x = 0, y = 0;

    if (wing.window != NULL)
    {
        SDL_GetWindowSize(wing.window, &w, &h);
        SDL_GetWindowPosition(wing.window, &x, &y);
    }

    if (r != NULL)
    {
        r->left = x;
        r->top = y;
        r->right = x + w;
        r->bottom = y + h;
    }

    return 1;
}

static int wing_painting = 0;

uint32_t CCALL BeginPaint_c(uint32_t hwnd, uint32_t ps)
{
    wing_paintstruct *p = (wing_paintstruct *)GUEST_MEM(ps);

    WING_LOG("BeginPaint(%08x, %08x)\n", hwnd, ps);

    wing_create_window();

    wing_painting = 1;

    if (p != NULL)
    {
        memset(p, 0, sizeof(*p));
        p->hdc = WING_DC_HANDLE;
        p->fErase = 1;
        p->rcPaint.left = 0;
        p->rcPaint.top = 0;
        p->rcPaint.right = WING_WIDTH;
        p->rcPaint.bottom = WING_HEIGHT;
        p->fRestore = 0;
        p->fIncUpdate = 0;
    }

    return WING_DC_HANDLE;
}

uint32_t CCALL EndPaint_c(uint32_t hwnd, uint32_t ps)
{
    WING_LOG("EndPaint(%08x, %08x)\n", hwnd, ps);

    wing_painting = 0;

    return 1;
}

uint32_t CCALL InvalidateRect_c(uint32_t hwnd, uint32_t rect, uint32_t erase)
{
    (void)rect;
    (void)erase;

    WING_LOG("InvalidateRect(%08x)\n", hwnd);

    if (wing_painting)
    {
        return 1;
    }

    Winapi_CallWndProc(hwnd, 0x000f /* WM_PAINT */, 0, 0);

    return 1;
}

/* --- the palettes --- */

uint32_t CCALL CreatePalette_c(uint32_t logpal)
{
    const wing_logpalette *p = (const wing_logpalette *)GUEST_MEM(logpal);
    uint32_t i, n;

    WING_LOG("CreatePalette(%08x)\n", logpal);

    if (p == NULL)
    {
        return 0;
    }

    n = p->cEntries;
    if (n > 256)
    {
        n = 256;
    }

    for (i = 0; i < n; i++)
    {
        wing.palette[i][0] = p->aEntries[i].bRed;
        wing.palette[i][1] = p->aEntries[i].bGreen;
        wing.palette[i][2] = p->aEntries[i].bBlue;
        wing.palette[i][3] = 255;
    }
    wing.has_palette = 1;

    memcpy(&wing.logpal, p, sizeof(wing.logpal));
    wing.has_logpal = 1;

    return WING_PALETTE_HANDLE;
}

uint32_t CCALL SelectPalette_c(uint32_t hdc, uint32_t hpal, uint32_t realize)
{
    (void)hdc;
    (void)hpal;
    (void)realize;

    return 1;
}

uint32_t CCALL RealizePalette_c(uint32_t hdc)
{
    (void)hdc;

    return 1;
}

uint32_t CCALL AnimatePalette_c(uint32_t hpal, uint32_t start, uint32_t count, uint32_t entries)
{
    const uint8_t *e = (const uint8_t *)GUEST_MEM(entries);
    uint32_t i;

    WING_LOG("AnimatePalette(%08x, %i, %i, %08x)\n", hpal, start, count, entries);

    if (e == NULL || count == 0 || count > 256)
    {
        return 0;
    }

    for (i = 0; i < count; i++)
    {
        uint32_t c = start + i;
        if (c > 255)
        {
            break;
        }
        /* PALETTEENTRY: red, green, blue, flags (not the BGRX of the DIB table) */
        wing.palette[c][0] = e[i * 4 + 0];
        wing.palette[c][1] = e[i * 4 + 1];
        wing.palette[c][2] = e[i * 4 + 2];
        wing.palette[c][3] = 255;
    }

    /* No frame is shown here: the game sets this palette in every repaint,
     * before it hands the picture over */
    return 1;
}

uint32_t CCALL GetSystemPaletteEntries_c(uint32_t hdc, uint32_t start, uint32_t count, uint32_t entries)
{
    uint8_t *e = (uint8_t *)GUEST_MEM(entries);
    const SDL_Palette *pal = NULL;
    uint32_t i, n = 0;

    (void)hdc;

    if (e == NULL)
    {
        return 0;
    }

    (void)pal;
    n = 0;

    for (i = 0; i < count; i++)
    {
        uint32_t c = start + i;
        uint8_t r = 0, g = 0, b = 0;

        if (pal != NULL && c < n)
        {
            r = pal->colors[c].r;
            g = pal->colors[c].g;
            b = pal->colors[c].b;
        }

        e[i * 4 + 0] = b;
        e[i * 4 + 1] = g;
        e[i * 4 + 2] = r;
        e[i * 4 + 3] = 0;
    }

    return count;
}

/* --- the display --- */

uint32_t CCALL GetDeviceCaps_c(uint32_t hdc, uint32_t index)
{
    (void)hdc;

    switch (index)
    {
        case 0:   /* HORZRES */
            return 320;
        case 1:   /* VERTRES */
            return 240;
        case 4:   /* HORZSIZE (in mm) */
            return 100;
        case 5:   /* VERTSIZE (in mm) */
            return 75;
        case 6:   /* COLORRES */
            return 8;
        case 8:   /* LOGPIXELSX */
        case 10:  /* PHYSICALOFFSETX */
        case 12:  /* PHYSICALOFFSETY */
            return 96;
        case 9:   /* LOGPIXELSY */
            return 96;
        case 14:  /* SCALEFACTOR (in percent) */
            return 100;
        case 16:  /* BITSPIXEL */
            return 8;
        case 18:  /* PLANES */
            return 1;
        case 24:  /* COLORS */
            return 256;
        case 38:  /* RASTERCAPS: RC_BITBLT (0x100) */
            return 0x100 | 0x200 | 0x400;
        case 40:  /* CURVECAPS */
            return 0;
        case 42:  /* LINECAPS */
            return 0;
        case 44:  /* POLYGONCAPS */
            return 0;
        case 46:  /* POLYFILLCAPS */
            return 0;
        default:
            break;
    }

    return 0;
}

/* --- the modules the game loads --- */

/* The game loads the Microsoft Display Device (DISPDIB.DLL), which shows an
 * 8bpp picture on the whole screen in a window of its own, for its mode "Full
 * Screen" (Esc).  There is no such module and no such window: the game sends
 * that mode's picture all the same (WM_COPYDATA, SendMessageA_c), and the
 * port shows it in its own window, on the whole screen. */
uint32_t CCALL LoadModule_c(uint32_t modulename, uint32_t phmodule)
{
    uint32_t *out = (uint32_t *)GUEST_MEM(phmodule);

    WING_LOG("LoadModule(%08x)\n", modulename);

    if (out != NULL)
    {
        *out = 0;
    }

    return 0;
}

/* The game looks for a window of its own class name: there is none, so the
 * game keeps going with the display device it has got. */
uint32_t CCALL FindWindowA_c(uint32_t classname, uint32_t windowname)
{
    (void)classname; (void)windowname;

    WING_LOG("FindWindowA(%08x, %08x)\n", classname, windowname);

    return 0;
}

/* --- the messages --- */

/* the game sends the picture of its mode "Full Screen" to the window of the
 * display device as a WM_COPYDATA message, and fills the lists of its dialogs */
uint32_t CCALL SendMessageA_c(uint32_t hwnd, uint32_t msg, uint32_t wparam, uint32_t lparam)
{
    uint32_t result;

    /* the lists of the settings dialogs (WinApi-dialog.c) */
    if (Winapi_DialogMessage(hwnd, msg, wparam, lparam, &result)) return result;
    WING_LOG("SendMessageA(%08x, %08x, %08x, %08x)\n", hwnd, msg, wparam, lparam);

    if (msg == 0x4a) /* WM_COPYDATA */
    {
        wing_copydatastruct *cd = (wing_copydatastruct *)GUEST_MEM(lparam);

        if (cd == NULL)
        {
            return 0;
        }

        WING_LOG("WM_COPYDATA: data %08x, %i bytes, ptr %08x\n", cd->dwData, cd->cbData, cd->lpData);

        if (cd->dwData == 0x400)
        {
            /* the format of the picture: the header and the palette */
            const uint8_t *src = (const uint8_t *)GUEST_MEM(cd->lpData);

            if (src != NULL && cd->cbData >= sizeof(wing_bmiheader))
            {
                memcpy(&wing.bmi, src, sizeof(wing.bmi));
                wing.has_bmi = 1;

                if (cd->cbData > sizeof(wing_bmiheader))
                {
                    uint32_t n = (cd->cbData - sizeof(wing_bmiheader)) / 4;
                    uint32_t i;

                    if (n > 256)
                    {
                        n = 256;
                    }
                    for (i = 0; i < n; i++)
                    {
                        wing.palette[i][0] = src[sizeof(wing_bmiheader) + i * 4 + 2];
                        wing.palette[i][1] = src[sizeof(wing_bmiheader) + i * 4 + 1];
                        wing.palette[i][2] = src[sizeof(wing_bmiheader) + i * 4 + 0];
                        wing.palette[i][3] = 255;
                    }
                    wing.has_palette = 1;
                }
            }
        }
        else if (cd->dwData == 0x401)
        {
            /* the picture itself: 320x240, the game's 320x224 at the top and
             * black under it; shown as the WinG path shows it */
            const uint8_t *src = (const uint8_t *)GUEST_MEM(cd->lpData);

            if (src != NULL && cd->cbData >= WING_WIDTH * 224)
            {
                /* the game has gone to its mode "Full Screen" (Esc) */
                if (!wing.display_device)
                {
                    wing.display_device = 1;
                    Game_WindowSetFullscreen(1);
                }
                wing_show(src, WING_WIDTH, WING_WIDTH, 224);
            }
        }

        return 1;
    }

    return 0;
}


/* The picture of the game reaches the window in WinGStretchBlt (the window)
 * and in WM_COPYDATA (the mode "Full Screen"): nothing to do once per tick. */
void Game_Wing_Tick(void)
{
}

#ifdef __cplusplus
}
#endif
