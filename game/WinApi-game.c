/*
 *  Comix Zone Win32 import shim - link-correct stubs.
 *
 *  Every import resolves and every wrapper pops the right number of stdcall
 *  arguments, so the port links. Replace a stub by the real function.
 *
 *  Generated: 2026-10-21
 */

#include "WinApi-game.h"

#include <stdio.h>
#include <string.h>

#include "WinApi.h"

/* The guest's addresses are 32 bit; on a 64 bit host they are offsets into the
 * guest's memory (the file handles of the layer are such addresses). */
extern uint64_t pointer_offset;
#define GUEST_MEM(a) ((void *)(uintptr_t)((a) ? ((uint64_t)(a) + pointer_offset) : 0))

#ifdef __cplusplus
extern "C" {
#endif

/* USER32.dll (WinHelpA) */
uint32_t CCALL WinHelpA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    return 0;
}

/* USER32.dll (CheckMenuItem) */
uint32_t CCALL CheckMenuItem_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    return 0;
}

/* USER32.dll (EnableMenuItem) */
uint32_t CCALL EnableMenuItem_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    return 1;
}

/* USER32.dll (IsIconic) */
uint32_t CCALL IsIconic_c(uint32_t a0)
{
    (void)a0;
    return 0;
}

/* USER32.dll (GetLastActivePopup) */
uint32_t CCALL GetLastActivePopup_c(uint32_t a0)
{
    (void)a0;
    return 0;
}

/* USER32.dll (AdjustWindowRect) */
uint32_t CCALL AdjustWindowRect_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    return 0;
}

/* USER32.dll (GetDesktopWindow) */
uint32_t CCALL GetDesktopWindow_c(uint32_t a0)
{
    (void)a0;
    return 0;
}

/* USER32.dll (GetSubMenu) */
uint32_t CCALL GetSubMenu_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    return 0;
}

/* USER32.dll (RemoveMenu) */
uint32_t CCALL RemoveMenu_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    return 0;
}

/* USER32.dll (DeleteMenu) */
uint32_t CCALL DeleteMenu_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a1; (void)a2;
    return 0;
}

/* USER32.dll (InsertMenuA) */
uint32_t CCALL InsertMenuA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    (void)a0; (void)a1; (void)a2; (void)a3; (void)a4;
    return 0;
}

/* USER32.dll (DrawMenuBar) */
uint32_t CCALL DrawMenuBar_c(uint32_t a0)
{
    (void)a0;
    return 0;
}

/* The menu of the game.  The port has no window manager and no menu bar of
 * its own, so the menu is a handle the game can ask about (GetMenu) and change
 * (CheckMenuItem, EnableMenuItem); the items themselves are in the EXE. */
static uint32_t game_menu_handle = 0;

/* USER32.dll (LoadMenuA) */
uint32_t CCALL LoadMenuA_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    if (game_menu_handle == 0)
    {
        game_menu_handle = 1;
    }
    return game_menu_handle;
}

/* USER32.dll (SetMenu) */
uint32_t CCALL SetMenu_c(uint32_t a0, uint32_t a1, uint32_t a2)
{
    (void)a0; (void)a2;
    game_menu_handle = a1;
    return a1;
}

/* USER32.dll (GetMenu) */
uint32_t CCALL GetMenu_c(uint32_t a0)
{
    (void)a0;
    return game_menu_handle;
}

/* USER32.dll (DestroyMenu) */
uint32_t CCALL DestroyMenu_c(uint32_t a0)
{
    (void)a0;
    return 0;
}


/* WINMM.dll (joyGetPos) */
uint32_t CCALL joyGetPos_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    return 0;
}

/* WINMM.dll (midiOutGetNumDevs) */
uint32_t CCALL midiOutGetNumDevs_c(void)
{
    return 1;
}

/* WINMM.dll (waveOutGetNumDevs) */
uint32_t CCALL waveOutGetNumDevs_c(void)
{
    return 1;
}

/* KERNEL32.dll (GetPrivateProfileIntA) */
/* The game's settings (sound, music, language, keys, joystick) are read from
 * COMIX.INI with this function; the stub gave every setting the value 0,
 * which switched the sound off.  The layer's GetPrivateProfileStringA reads
 * the file; a key it does not find has the default value. */
extern uint32_t CCALL GetPrivateProfileStringA_c(const char *lpAppName, const char *lpKeyName, const char *lpDefault,
                                                 char *lpReturnedString, uint32_t nSize, const char *lpFileName);

uint32_t CCALL GetPrivateProfileIntA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    static const char missing[] = "\x01none";
    char value[64];
    const char *p;
    int number;

    GetPrivateProfileStringA_c((const char *)GUEST_MEM(a0), (const char *)GUEST_MEM(a1), missing, value,
                               sizeof(value), (const char *)GUEST_MEM(a3));
    if (strcmp(value, missing) == 0)
    {
        return a2;
    }

    /* like Windows: the digits at the start, no sign */
    p = value;
    number = 0;
    while (*p >= '0' && *p <= '9')
    {
        number = number * 10 + (*p - '0');
        p++;
    }

    return (uint32_t)number;
}

/* KERNEL32.dll (WritePrivateProfileStringA) */
uint32_t CCALL WritePrivateProfileStringA_c(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3)
{
    (void)a0; (void)a1; (void)a2; (void)a3;
    return 0;
}

/* KERNEL32.dll (GetFileSize)
 *
 * The game reads its pictures and its sequences from SPRITES.BIN and SEQS.95:
 * it asks for the size of the file and reads exactly that many bytes. With a
 * stub that returns 0 the files are read as empty, the game has no pictures
 * and draws nothing. The file handle is the one CreateFileA_c of the layer
 * has made; the guest's handle is the host pointer minus the guest's offset. */
uint32_t CCALL GetFileSize_c(uint32_t a0)
{
    handle h;
    FILE *f;
    off_t size, pos;

    if (a0 == 0) return 0xffffffffu;

    h = (handle)GUEST_MEM(a0);
    if (h == NULL || h->handle_type != HT_FILE || h->fh.f == NULL) return 0xffffffffu;

    f = (FILE *)h->fh.f;

    pos = ftello(f);
    if (fseeko(f, 0, SEEK_END) != 0) return 0xffffffffu;
    size = ftello(f);
    fseeko(f, pos, SEEK_SET);

    if (size < 0 || size > 0xffffffffll) return 0xffffffffu;

    return (uint32_t)size;
}

#ifdef __cplusplus
}
#endif
