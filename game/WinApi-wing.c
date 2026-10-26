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

#include <SDL.h>

#include "Game_defs.h"
#include "Game_hooks.h"
#include "Game_memory.h"
#include "WinApi.h"
#include "WinApi-wing.h"

/* the guest's addresses are 32 bit; on a 64 bit host they are offsets into the
 * guest's memory */
extern uint64_t pointer_offset;
#define GUEST_MEM(a) ((void *)(uintptr_t)((a) ? ((uint64_t)(a) + pointer_offset) : 0))
#define GUEST_ADDR(p) ((uint32_t)((uintptr_t)(p) - pointer_offset))

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
    SDL_Renderer *renderer;
    SDL_Texture *texture;

    uint8_t *pixels;          /* the picture, palette indexed */
    int width, height;

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
     * DirectDraw layer does), and the thread that draws owns the renderer. */
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
    {
        fprintf(stderr, "wing: SDL video initialization failed: %s\n", SDL_GetError());
        return;
    }

    wing.window = SDL_CreateWindow(GAME_NAME, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                   WING_WIDTH * 2, WING_HEIGHT * 2, SDL_WINDOW_RESIZABLE);
    if (wing.window == NULL)
    {
        fprintf(stderr, "wing: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return;
    }

    wing.renderer = SDL_CreateRenderer(wing.window, -1, SDL_RENDERER_ACCELERATED);
    if (wing.renderer == NULL)
    {
        wing.renderer = SDL_CreateRenderer(wing.window, -1, 0);
    }
    if (wing.renderer == NULL)
    {
        fprintf(stderr, "wing: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return;
    }

    wing.texture = SDL_CreateTexture(wing.renderer, SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING, WING_WIDTH, WING_HEIGHT);
    if (wing.texture == NULL)
    {
        fprintf(stderr, "wing: SDL_CreateTexture failed: %s\n", SDL_GetError());
        return;
    }

    SDL_SetTextureScaleMode(wing.texture, SDL_ScaleModeNearest);
    SDL_ShowWindow(wing.window);

    /* the picture of the game is 320x240, the window may be bigger */
    SDL_GetWindowSize(wing.window, &w, &h);
    WING_LOG("window %ix%i\n", w, h);
}

static void wing_alloc_pixels(int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return;
    }

    if (wing.pixels != NULL && width == wing.width && height == wing.height)
    {
        return;
    }

    free(wing.pixels);
    wing.pixels = (uint8_t *)malloc((size_t)width * height);
    if (wing.pixels == NULL)
    {
        fprintf(stderr, "wing: out of memory for the picture\n");
        wing.width = wing.height = 0;
        return;
    }

    memset(wing.pixels, 0, (size_t)width * height);
    wing.width = width;
    wing.height = height;

    WING_LOG("picture buffer %ix%i\n", width, height);
}

/* turns the palette-indexed picture into RGBA and shows it */
void Wing_ShowFrame(void)
{
    uint32_t *rgba;
    int i, n;

    if (wing.pixels == NULL || wing.texture == NULL)
    {
        return;
    }

    n = wing.width * wing.height;
    rgba = (uint32_t *)malloc((size_t)n * 4);
    if (rgba == NULL)
    {
        return;
    }

    for (i = 0; i < n; i++)
    {
        uint8_t c = wing.pixels[i];
        rgba[i] = ((uint32_t)wing.palette[c][3] << 24) | ((uint32_t)wing.palette[c][0] << 16) |
                  ((uint32_t)wing.palette[c][1] << 8) | (uint32_t)wing.palette[c][2];
    }

    SDL_UpdateTexture(wing.texture, NULL, rgba, wing.width * 4);
    free(rgba);

    SDL_RenderCopy(wing.renderer, wing.texture, NULL, NULL);
    SDL_RenderPresent(wing.renderer);
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

    /* The picture of the game is 320x240 with 8 bits per pixel; the game
     * fills in the format it got from WinGRecommendDIBFormat, so the size
     * of the buffer is the size of the screen. */
    wing_alloc_pixels(WING_WIDTH, WING_HEIGHT);
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
    int dw, dh, sw, sh, x, y;

    WING_LOG("WinGStretchBlt(%08x, %i, %i, %i, %i, %08x, %i, %i, %i, %i)\n",
             dest, xdest, ydest, wdest, hdest, src, xsrc, ysrc, wsrc, hsrc);

    if (wing.pixels == NULL)
    {
        return 0;
    }

    if (wing.texture == NULL)
    {
        return 0;
    }

    /* the picture of the game is the source of the blit */
    sw = (wsrc != 0) ? (int)wsrc : wing.width;
    sh = (hsrc != 0) ? (int)hsrc : wing.height;
    dw = (wdest != 0) ? (int)wdest : wing.width;
    dh = (hdest != 0) ? (int)hdest : wing.height;

    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0)
    {
        return 0;
    }

    /* the picture the game has drawn is in the memory of the guest */
    {
        const uint8_t *src_pixels = wing.pixels;
        int src_width = wing.width;
        int src_height = wing.height;

        if (wing.guest_bits != 0)
        {
            src_pixels = (const uint8_t *)GUEST_MEM(wing.guest_bits);
            src_width = wing.guest_width;
            src_height = wing.guest_height;
        }

        if (src_pixels == NULL)
        {
            return 0;
        }

        if (src_width < (int)xsrc + sw) sw = src_width - (int)xsrc;
        if (src_height < (int)ysrc + sh) sh = src_height - (int)ysrc;
        if (sw <= 0 || sh <= 0)
        {
            return 0;
        }

        /* The game stretches the picture to the size of its window; the port
         * keeps the picture at its own size and the renderer scales it into
         * the window (whatever size the window has). */
        {
            uint32_t *rgba;
            int n, tw = 0, th = 0;

            SDL_QueryTexture(wing.texture, NULL, NULL, &tw, &th);
            if (tw != sw || th != sh)
            {
                SDL_DestroyTexture(wing.texture);
                wing.texture = SDL_CreateTexture(wing.renderer, SDL_PIXELFORMAT_ARGB8888,
                                                 SDL_TEXTUREACCESS_STREAMING, sw, sh);
                if (wing.texture == NULL)
                {
                    return 0;
                }
                SDL_SetTextureScaleMode(wing.texture, SDL_ScaleModeNearest);
            }

            n = sw * sh;
            rgba = (uint32_t *)malloc((size_t)n * 4);
            if (rgba == NULL)
            {
                return 0;
            }

            for (y = 0; y < sh; y++)
            {
                const uint8_t *row = src_pixels + (ysrc + y) * src_width + xsrc;
                for (x = 0; x < sw; x++)
                {
                    uint8_t c = row[x];

                    rgba[y * sw + x] = ((uint32_t)wing.palette[c][3] << 24) |
                                       ((uint32_t)wing.palette[c][0] << 16) |
                                       ((uint32_t)wing.palette[c][1] << 8) |
                                       (uint32_t)wing.palette[c][2];
                }
            }

            SDL_UpdateTexture(wing.texture, NULL, rgba, sw * 4);
            free(rgba);
        }
    }

    SDL_RenderCopy(wing.renderer, wing.texture, NULL, NULL);
    SDL_RenderPresent(wing.renderer);

    return 1;
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
     * before WinGStretchBlt, and the buffer of Wing_ShowFrame is not the
     * picture of the game (a black frame between every two real ones). */
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

/* The game loads the Microsoft Display Device, the program that shows an
 * 8bpp picture in a window of its own.  The port shows the picture with
 * WinG, so there is no such module: the game falls back to the WinG path. */
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

/* the game sends its picture to the window as a WM_COPYDATA message */
uint32_t CCALL SendMessageA_c(uint32_t hwnd, uint32_t msg, uint32_t wparam, uint32_t lparam)
{
    WING_LOG("SendMessageA(%08x, %08x, %08x, %08x)\n", hwnd, msg, wparam, lparam);

    if (msg == 0x4a) /* WM_COPYDATA */
    {
        wing_copydatastruct *cd = (wing_copydatastruct *)GUEST_MEM(lparam);

        if (cd == NULL)
        {
            return 0;
        }

        WING_LOG("WM_COPYDATA: data %08x, %i bytes, ptr %08x\n", cd->dwData, cd->cbData, cd->lpData);

        /* The game sends the message to its own window (the window of the
         * class COMIXZONE it has made) and its window procedure draws the
         * picture.  The port has no such window, so the message is passed
         * to the window procedure of the game here. */
        Winapi_CallWndProc(hwnd, msg, wparam, lparam);

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
            /* the picture itself */
            const uint8_t *src = (const uint8_t *)GUEST_MEM(cd->lpData);

            if (src != NULL && wing.pixels != NULL)
            {
                uint32_t n = cd->cbData;

                if (n > (uint32_t)(wing.width * wing.height))
                {
                    n = (uint32_t)(wing.width * wing.height);
                }
                memcpy(wing.pixels, src, n);

                /* the game sends the picture to the display device, the port
                 * shows it in its own window */
                Wing_ShowFrame();
            }
        }

        return 1;
    }

    return 0;
}


/* The picture of the game reaches the window in WinGStretchBlt (the WinG
 * path of the game); the buffer of the display-device path (0x62a174) has a
 * stride of its own and is not shown. */
void Game_Wing_Tick(void)
{
}

#ifdef __cplusplus
}
#endif
