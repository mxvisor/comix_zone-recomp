/*
 *  Comix Zone Win32 import shim - link-correct stubs.
 *
 *  Every import resolves and every wrapper pops the right number of stdcall
 *  arguments, so the port links. Replace a stub by the real function.
 *
 *  Generated: 2026-10-21
 */

#include "WinApi-game.h"

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
uint32_t CCALL GetDesktopWindow_c(void)
{
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
uint32_t CCALL SetMenu_c(uint32_t a0, uint32_t a1)
{
    (void)a0;
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


/* WINMM.dll (joyGetPos)
 *
 * Without COMIX.INI the game polls the joystick every tick.  Success with the
 * JOYINFO left unwritten gave it stack garbage as the stick and the buttons
 * (the hero acted as if keys were held); there is no joystick: JOYERR_UNPLUGGED. */
uint32_t CCALL joyGetPos_c(uint32_t a0, uint32_t a1)
{
    (void)a0; (void)a1;
    return 167;
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

#ifdef __cplusplus
}
#endif
