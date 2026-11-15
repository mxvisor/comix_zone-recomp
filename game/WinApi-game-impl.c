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

/* The mod Additional_Buttons of the port's settings (off by default; the item
 * "Additional buttons" of its panel).
 *
 * The game reads the joystick with joyGetPos (0x401C28, in the input routine at
 * 0x401A8D) and gives its buttons 1 and 2 the actions [JoyStick] button1 and
 * button2 of COMIX.INI: the actions 0-3, the bits 0x40, 0x10, 0x20 and 0x02 of
 * the word of the pad, through the table of shifts at 0x4318F8 {6, 4, 5, 65
 * (= 1)}: 0 Action, 1 Jump, 2 Block, 3 down.  Its Set up Gamepad names them
 * Action, Jump, Select, Block: the names of 2 and 3 are swapped (2 blocks in
 * the game, 3 is the bit of the down key).  The keys give 0x40 (action), 0x10
 * (jump) and 0x02 (down), never 0x20: with the menu's two buttons, Block and a
 * button for each of attack and jump do not go together.
 *
 * With the mod, button 3 (X of the pad) has the action [JoyStick] button3 of
 * COMIX.INI, Block (2) when there is none; the key is written there so that it
 * can be changed.  The game's Setup Gamepad dialog (SETUPJOYPAD_xx, procedure
 * 0x402104: the lists 1002 and 1003 of buttons 1 and 2, 1004 of the use of the
 * joystick) then has a third list, Button 3, and in its lists the names of the
 * actions 2 and 3 swapped back; OK writes button 3 to COMIX.INI, Default sets
 * Block.
 *
 * The mod also gives Block a key: [Keys] block of COMIX.INI, a name of the
 * game's table of keys (C when there is none), checked where the game has read
 * its keys (0x401C0D), and a sixth row in the game's Set up Keyboard
 * (SETUPKEYBOARD_xx, procedure 0x401EC0: the lists 1010-1018 of the nine keys),
 * named as Block of Set up Gamepad. */

#include "Game_hooks.h"
#include "WinApi-game.h"
#include "WinApi-kernel32.h"
#include "WinApi-ui.h"
#include "WinApi-user32.h"
#include "Game_memory.h"
#include "Game_midi.h"
#include "Game_osd.h"
#include "llasm_cpu.h"
#include "touchgamepad.h"

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
#define COMIX_JOY_BLOCK 2
#define JOY_BUTTON3 0x0004u
#define COMIX_LANGUAGE 0x4318C8u        /* the language of the menus, 0-4 */
#define COMIX_KEYS 0x431468u            /* the table of keys: name, virtual key */
#define COMIX_KEY_COUNT 89
#define COMIX_KEY_NAMES 0x431730u       /* the names of the last 13 keys, 13 for each language */
#define COMIX_KEY_LOCAL 76              /* the first key named by language */
#define KEY_DEFAULT 2                   /* C */

static int buttons_mod;                 /* Additional_Buttons */
static int button3_action = -1;         /* [JoyStick] button3, read at the first use */
static int block_key = -1;              /* [Keys] block: the index in the table of keys */

int Game_ConfigKey(const char *key, const char *value)
{
    if (strcasecmp(key, "Additional_Buttons") != 0) return 0;
    if (strcasecmp(value, "on") == 0) buttons_mod = 1;
    else if (strcasecmp(value, "off") == 0) buttons_mod = 0;
    else fprintf(stderr, GAME_NAME ": unknown Additional_Buttons: %s\n", value);
    TouchGamepad_Relayout();
    return 1;
}

/* before `or [ebp-0x1C], eax` (0x401CAA), when the game has read the joystick:
 * eax, the bits of its buttons 1, 2 and of the stick; [ebp-0x8], the wButtons
 * of its JOYINFO */
static void button3_save(int action)
{
    char value[8];

    button3_action = action;
    snprintf(value, sizeof(value), "%d", action);
    WritePrivateProfileStringA_c("JoyStick", "button3", value, COMIX_INI);
}

static void button3_load(void)
{
    int action;

    if (button3_action >= 0) return;
    action = (int)GetPrivateProfileIntA_c("JoyStick", "button3", COMIX_JOY_BLOCK, COMIX_INI);
    if (action < 0 || action >= COMIX_JOY_ACTIONS) action = COMIX_JOY_BLOCK;
    button3_save(action);
}

static const char *guest_string(uint32_t address)
{
    return (const char *)GAME_GUEST32_TO_PTR(address);
}

static const char *key_name(int key)
{
    return guest_string(*(const uint32_t *)GAME_GUEST32_TO_PTR(COMIX_KEYS + 8 * (uint32_t)key));
}

/* the key of a name of COMIX.INI: the table's, or that of another language */
static int key_by_name(const char *name)
{
    int key, language;

    for (key = 0; key < COMIX_KEY_COUNT; key++)
    {
        if (strcasecmp(key_name(key), name) == 0) return key;
    }
    for (language = 0; language < 5; language++)
    {
        for (key = COMIX_KEY_LOCAL; key < COMIX_KEY_COUNT; key++)
        {
            uint32_t at = COMIX_KEY_NAMES + 4 * (uint32_t)(language * 13 + key - COMIX_KEY_LOCAL);
            if (strcasecmp(guest_string(*(const uint32_t *)GAME_GUEST32_TO_PTR(at)), name) == 0) return key;
        }
    }
    return -1;
}

static void block_save(int key)
{
    block_key = key;
    WritePrivateProfileStringA_c("Keys", "block", key_name(key), COMIX_INI);
}

static void block_load(void)
{
    char name[64];

    if (block_key >= 0) return;
    GetPrivateProfileStringA_c("Keys", "block", "", name, sizeof(name), COMIX_INI);
    block_key = key_by_name(name);
    if (block_key < 0) block_save(KEY_DEFAULT);
}

/* at `mov [ebp-0x1C], ebx` (0x401C0D), when the game has read its keys: ebx,
 * their bits */
void CCALL comix_key_buttons(_cpu *cpu)
{
    uint32_t vk;

    if (!buttons_mod) return;
    block_load();
    vk = *(const uint32_t *)GAME_GUEST32_TO_PTR(COMIX_KEYS + 8 * (uint32_t)block_key + 4);
    if (GetAsyncKeyState_c((int32_t)vk) & 0x8000)
        cpu->_ebx |= 1u << (*(const uint8_t *)GAME_GUEST32_TO_PTR(COMIX_JOY_SHIFTS + 4 * COMIX_JOY_BLOCK) & 31);
}

void CCALL comix_joy_buttons(_cpu *cpu)
{
    uint32_t buttons, shift;

    if (!buttons_mod) return;
    button3_load();
    buttons = *(const uint32_t *)GAME_GUEST32_TO_PTR(cpu->_ebp - 8);
    if (!(buttons & JOY_BUTTON3)) return;
    /* the shift as the game's shl takes it: modulo 32 (65 is 1, down) */
    shift = *(const uint8_t *)GAME_GUEST32_TO_PTR(COMIX_JOY_SHIFTS + 4 * (uint32_t)button3_action) & 31;
    cpu->_eax |= 1u << shift;
}

/* Setup Gamepad: the row of button 2 (its label 1033 and list 1003) repeated
 * under it for button 3, the buttons OK, Cancel and Default moved down */
#define DLG_LABEL2 1033u
#define DLG_LIST1 1002u
#define DLG_LIST2 1003u
#define DLG_LABEL3 1034u
#define DLG_LIST3 1005u
#define DLG_DEFAULT 1000u
#define DLG_ROW 20

static int setup_gamepad;               /* the open dialog is Setup Gamepad, with the third list */
static int setup_keyboard;              /* the open dialog is Setup Keyboard, with the key of Block */

/* the names of Set up Gamepad's actions 2 and 3, in each language (the table
 * of its procedure, 0x40211E): "Select" and "Block" */
static const uint32_t select_names[5] = { 0x431E28u, 0x431EB8u, 0x431F50u, 0x431FF4u, 0x432080u };
static const uint32_t block_names[5] = { 0x431E38u, 0x431EC8u, 0x431F6Cu, 0x43200Cu, 0x432090u };

static unsigned int language(void)
{
    unsigned int value = *(const uint8_t *)GAME_GUEST32_TO_PTR(COMIX_LANGUAGE);

    return (value > 4) ? 0 : value;
}

/* Setup Keyboard: under Item 3 (list 1018) a row for Block */
#define KBD_LIST1 1010u
#define KBD_LIST_LAST 1018u
#define KBD_LABEL 1020u
#define KBD_LIST 1019u
#define KBD_ROW 15
#define KBD_GROW 18

static void keyboard_created(void)
{
    static const uint32_t buttons[3] = { 1 /* IDOK */, 2 /* IDCANCEL */, DLG_DEFAULT };
    const char *label = guest_string(block_names[language()]);
    int x, y, w, h, i;

    if (!Winapi_DialogGetControl(KBD_LIST_LAST, &x, &y, &w, &h, NULL)) return;
    if (!Winapi_DialogAddControl("STATIC", 0x50020000u, KBD_LABEL, x - 26, y + KBD_ROW + 4, 25, 8, label)) return;
    /* as the game's lists: WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | CBS_DROPDOWNLIST */
    if (!Winapi_DialogAddControl("COMBOBOX", 0x50210003u, KBD_LIST, x, y + KBD_ROW, w, h, "")) return;
    for (i = 0; i < 3; i++) Winapi_DialogMoveControl(buttons[i], 0, KBD_GROW);
    Winapi_DialogGrow(0, KBD_GROW);
    setup_keyboard = 1;
}

static void keyboard_handled(unsigned int msg, unsigned int wParam)
{
    const char *item;
    int i;

    if (msg == 0x0110 /* WM_INITDIALOG */)
    {
        for (i = 0; (item = Winapi_DialogListItem(KBD_LIST1, i)) != NULL; i++) Winapi_DialogListAdd(KBD_LIST, item);
        block_load();
        Winapi_DialogListSelect(KBD_LIST, block_key);
    }
    else if (msg == 0x0111 /* WM_COMMAND */ && (wParam >> 16) == 0 /* BN_CLICKED */)
    {
        if ((wParam & 0xffff) == 1 /* IDOK */)
        {
            i = Winapi_DialogListSelection(KBD_LIST);
            if (i >= 0 && i < COMIX_KEY_COUNT) block_save(i);
        }
        else if ((wParam & 0xffff) == DLG_DEFAULT)
        {
            Winapi_DialogListSelect(KBD_LIST, KEY_DEFAULT);
        }
    }
}

void comix_dialog_created(const char *name)
{
    static const uint32_t buttons[3] = { 1 /* IDOK */, 2 /* IDCANCEL */, DLG_DEFAULT };
    char label[64];
    const char *text;
    int x, y, w, h, i;
    size_t n;

    setup_gamepad = 0;
    setup_keyboard = 0;
    if (!buttons_mod || name == NULL) return;
    if (strncasecmp(name, "SETUPKEYBOARD_", 14) == 0)
    {
        keyboard_created();
        return;
    }
    if (strncasecmp(name, "SETUPJOYPAD_", 12) != 0) return;
    /* "Button 2", "Taste 2", "Bouton 2", "Botón 2": the label of button 3 */
    if (!Winapi_DialogGetControl(DLG_LABEL2, &x, &y, &w, &h, &text)) return;
    snprintf(label, sizeof(label), "%s", text);
    n = strlen(label);
    if (n > 0 && label[n - 1] == '2') label[n - 1] = '3';
    if (!Winapi_DialogAddControl("STATIC", 0x50020000u, DLG_LABEL3, x, y + DLG_ROW, w, h, label)) return;
    if (!Winapi_DialogGetControl(DLG_LIST2, &x, &y, &w, &h, NULL)) return;
    /* WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, as the game's lists */
    if (!Winapi_DialogAddControl("COMBOBOX", 0x50010003u, DLG_LIST3, x, y + DLG_ROW, w, h, "")) return;
    for (i = 0; i < 3; i++) Winapi_DialogMoveControl(buttons[i], 0, DLG_ROW);
    Winapi_DialogGrow(0, DLG_ROW);
    setup_gamepad = 1;
}

void comix_dialog_handled(unsigned int hDlg, unsigned int msg, unsigned int wParam, unsigned int lParam)
{
    const char *item;
    int i;

    (void)hDlg; (void)lParam;
    if (setup_keyboard)
    {
        keyboard_handled(msg, wParam);
        return;
    }
    if (!setup_gamepad) return;
    if (msg == 0x0110 /* WM_INITDIALOG */)
    {
        /* the names of the actions 2 and 3 swapped back in the game's lists */
        for (i = 0; i < 2; i++)
        {
            Winapi_DialogListSetItem(i ? DLG_LIST2 : DLG_LIST1, COMIX_JOY_BLOCK, guest_string(block_names[language()]));
            Winapi_DialogListSetItem(i ? DLG_LIST2 : DLG_LIST1, COMIX_JOY_BLOCK + 1, guest_string(select_names[language()]));
        }
        /* the actions, in the game's language, as the list of button 1 has them */
        for (i = 0; i < COMIX_JOY_ACTIONS && (item = Winapi_DialogListItem(DLG_LIST1, i)) != NULL; i++)
            Winapi_DialogListAdd(DLG_LIST3, item);
        button3_load();
        Winapi_DialogListSelect(DLG_LIST3, button3_action);
    }
    else if (msg == 0x0111 /* WM_COMMAND */ && (wParam >> 16) == 0 /* BN_CLICKED */)
    {
        if ((wParam & 0xffff) == 1 /* IDOK */)
        {
            i = Winapi_DialogListSelection(DLG_LIST3);
            if (i >= 0 && i < COMIX_JOY_ACTIONS) button3_save(i);
        }
        else if ((wParam & 0xffff) == DLG_DEFAULT)
        {
            Winapi_DialogListSelect(DLG_LIST3, COMIX_JOY_BLOCK);
        }
    }
}

/* the item of the port's panel */
static void buttons_press(void)
{
    buttons_mod = !buttons_mod;
    TouchGamepad_Relayout();
}

/* the on-screen gamepad has BLOCK (game/Game_touchlayout.h) */
int comix_buttons_mod(void)
{
    return buttons_mod;
}

static const char *buttons_state(void)
{
    return buttons_mod ? "on" : "off";
}

static const Game_OsdItem buttons_item = { "Additional buttons", NULL, buttons_press, buttons_state, "Additional_Buttons", NULL, NULL, NULL, NULL };

void comix_osd_items(void)
{
    Game_OsdAddItem(&buttons_item);
    Game_MidiAddEffectsItem();
}

#ifdef __cplusplus
}
#endif
