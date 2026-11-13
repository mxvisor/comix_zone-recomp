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

/* The mod Joystick_Button3 of the port's settings (off by default; the item
 * "Joystick button 3" of its panel).
 *
 * The game reads the joystick with joyGetPos (0x401C28, in the input routine at
 * 0x401A8D) and gives its buttons 1 and 2 the actions [JoyStick] button1 and
 * button2 of COMIX.INI: 0 Action, 1 Jump, 2 Block, 3 Select, the bits 0x40,
 * 0x10, 0x20 and 0x02 (down) of the word of the pad, through the table of shifts
 * at 0x4318F8 {6, 4, 5, 65}.  Its menu has the two buttons only, and the keys
 * have no Block: Block and a button for each of attack and jump do not go
 * together.  With the mod button 3 (X of the pad) has the action [JoyStick]
 * button3 of COMIX.INI, Block (2) when there is none; the key is written there
 * so that it can be changed. */

#include "Game_hooks.h"
#include "WinApi-game.h"
#include "WinApi-kernel32.h"
#include "Game_memory.h"
#include "Game_osd.h"
#include "llasm_cpu.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COMIX_INI ".\\COMIX.INI"
#define COMIX_JOY_SHIFTS 0x4318F8u      /* the shift of each action's bit */
#define COMIX_JOY_ACTIONS 4
#define JOY_BUTTON3 0x0004u

static int button3_mod;                 /* Joystick_Button3 */
static int button3_action = -1;         /* [JoyStick] button3, read at the first use */

int Game_ConfigKey(const char *key, const char *value)
{
    if (strcasecmp(key, "Joystick_Button3") != 0) return 0;
    if (strcasecmp(value, "on") == 0) button3_mod = 1;
    else if (strcasecmp(value, "off") == 0) button3_mod = 0;
    else fprintf(stderr, GAME_NAME ": unknown Joystick_Button3: %s\n", value);
    return 1;
}

/* before `or [ebp-0x1C], eax` (0x401CAA), when the game has read the joystick:
 * eax, the bits of its buttons 1, 2 and of the stick; [ebp-0x8], the wButtons
 * of its JOYINFO */
void CCALL comix_joy_buttons(_cpu *cpu)
{
    uint32_t buttons, shift;

    if (!button3_mod) return;
    if (button3_action < 0)
    {
        char value[8];
        button3_action = (int)GetPrivateProfileIntA_c("JoyStick", "button3", 2, COMIX_INI);
        if (button3_action < 0 || button3_action >= COMIX_JOY_ACTIONS) button3_action = 2;
        snprintf(value, sizeof(value), "%d", button3_action);
        WritePrivateProfileStringA_c("JoyStick", "button3", value, COMIX_INI);
    }
    buttons = *(const uint32_t *)GAME_GUEST32_TO_PTR(cpu->_ebp - 8);
    if (!(buttons & JOY_BUTTON3)) return;
    /* the shift as the game's shl takes it: modulo 32 (65 is 1, down) */
    shift = *(const uint8_t *)GAME_GUEST32_TO_PTR(COMIX_JOY_SHIFTS + 4 * (uint32_t)button3_action) & 31;
    cpu->_eax |= 1u << shift;
}

/* the item of the port's panel */
static void button3_press(void)
{
    button3_mod = !button3_mod;
}

static const char *button3_state(void)
{
    return button3_mod ? "on" : "off";
}

static const Game_OsdItem button3_item = { "Joystick button 3", NULL, button3_press, button3_state, "Joystick_Button3", NULL };

void comix_osd_items(void)
{
    Game_OsdAddItem(&button3_item);
}

#ifdef __cplusplus
}
#endif
