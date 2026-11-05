/* What SR-SDK's runtime asks of this game.  Every hook is optional: see the
 * #ifndef defaults in SR-SDK/runtime/ for the full list. */

#ifndef _GAME_HOOKS_H_INCLUDED_
#define _GAME_HOOKS_H_INCLUDED_

/* the original's file name (argv[0] of the guest) */
#define GAME_MAIN_FILENAME "COMIX.EXE"

/* the report of a crash (game/main.c) */
#ifdef __cplusplus
extern "C"
#endif
void Comix_Start(void);
#define GAME_WIN_START() Comix_Start()

/* the game's name in the port's messages */
#define GAME_NAME "comix zone"
/* the game plays its music by MIDI and needs no disc: no "no CD" message */
#define GAME_CD_OPTIONAL

/* the port's environment variables: GAME_ENV("CD_IMAGE") is $CZ_CD_IMAGE */
#define GAME_ENV(name) "CZ_" name

/* The game asks with a message box whether to continue when the display has
 * less than 256 colors, and waits for the answer. The port answers yes; only
 * F2's question (caption "Start Game", from the window) is shown, as a dialog. */
#define GAME_WINAPI_MESSAGEBOX_ANSWER(text, caption) \
    (((caption) != NULL && strcmp((caption), "Start Game") == 0) ? 0 : 6 /* IDYES */)

/* The game draws its picture in its window procedure, in the handler of the
 * repaint request, and waits for that request: the port asks for it when the
 * program has no other message. */
#define GAME_WINAPI_PAINT_WHEN_IDLE 0

/* The game makes its picture buffer when Windows tells it that the program
 * is not active, and draws the scene when the program becomes active again.
 * The port sends those two messages when the program has no other message. */
#define GAME_WINAPI_ACTIVATE_APP_ON_IDLE 1

/* The game reads its keys with GetAsyncKeyState; on a touch screen the buttons
 * of game/Game_touchlayout.h press them. */
#define GAME_WINAPI_TOUCH_GAMEPAD 1

/* The settings of the window menu (Setup Keyboard, Setup Gamepad) and About are
 * dialogs of the EXE: the port shows them over the picture (WinApi-dialog.c). */
#define GAME_WINAPI_DIALOGS 1

/* Game > Exit of the menu: the window procedure answers WM_CLOSE with
 * DestroyWindow and WM_DESTROY with PostQuitMessage; after its message loop
 * the game writes COMIX.INI. */
#define GAME_WINAPI_DESTROY_MESSAGE 1
/* the close button of the window: WM_CLOSE, as Exit of the menu (the original
 * gets it from DefWindowProc's SC_CLOSE), so COMIX.INI is written then too */
#ifdef __cplusplus
extern "C"
#endif
int Winapi_CloseRequest(void);
#define GAME_CLOSE_REQUEST() Winapi_CloseRequest()

#endif /* _GAME_HOOKS_H_INCLUDED_ */
