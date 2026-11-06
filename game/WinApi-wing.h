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

/* The WinG layer of Comix Zone.
 *
 * The game draws an 8bpp, palette-indexed picture and hands it over to the
 * system in one of two ways:
 *
 *   - it asks the WinG API for a device context (WinGCreateDC), a bitmap
 *     (WinGCreateBitmap), a palette (WinGSetDIBColorTable) and blits the
 *     picture to the screen (WinGStretchBlt);
 *   - it sends the picture to its own window as a WM_COPYDATA message, the
 *     way a program talks to the Microsoft Display Device.
 *
 * Both ways end up in the same place here: a palette-indexed buffer that is
 * converted to RGBA with the palette of the game and shown in an SDL window.
 */

#ifndef _WINAPI_WING_H_INCLUDED_
#define _WINAPI_WING_H_INCLUDED_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the WinG device context and the bitmap the game selects into it */
#define WING_DC_HANDLE     0x00000100u
/* the window of the game */
#define WING_MAIN_WINDOW   0x00000001u
#define WING_BITMAP_HANDLE 0x00000101u

/* the palette object CreatePalette hands out */
#define WING_PALETTE_HANDLE 0x00000102u

/* the picture the game draws into */
#define WING_WIDTH  320
#define WING_HEIGHT 240

#ifdef __cplusplus
}
#endif

#endif
