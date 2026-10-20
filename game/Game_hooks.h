/* What SR-SDK's runtime asks of this game.  Every hook is optional: see the
 * #ifndef defaults in SR-SDK/runtime/ for the full list. */

#ifndef _GAME_HOOKS_H_INCLUDED_
#define _GAME_HOOKS_H_INCLUDED_

/* the original's file name (argv[0] of the guest) */
#define GAME_MAIN_FILENAME "COMIX.EXE"

/* the game's name in the port's messages */
#define GAME_NAME "comix zone"
/* the port's environment variables: GAME_ENV("CD_IMAGE") is $CZ_CD_IMAGE */
#define GAME_ENV(name) "CZ_" name

#endif /* _GAME_HOOKS_H_INCLUDED_ */
