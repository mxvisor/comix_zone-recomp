/* The on-screen gamepad (see touchgamepad.c).  The keys are the game's
 * defaults, the data at 0x431860 of COMIX.EXE: the arrows, Z jump, X action,
 * 1-3 the items; F2 is a new game and F3 the pause (its window procedure,
 * WM_KEYDOWN); the window menu (WinApi-menubar.c) opens with a touch in the
 * strip at the top of the screen, beside NEW and PAUSE.  The buttons sit in the black
 * bars beside the 4:3 picture.  With the mod Additional_Buttons the pad has
 * BLOCK too (C, the default key of [Keys] block; game/WinApi-game-impl.c, which
 * calls TouchGamepad_Relayout when the mod changes): BLOCK above ACT, the
 * items above them. */

#define GP_NBUTTONS 12
#define CZ_GREEN 70, 160, 90

/* the mod Additional_Buttons is on (game/WinApi-game-impl.c, C) */
#ifdef __cplusplus
extern "C"
#endif
int comix_buttons_mod(void);

static GPButton gp_buttons[GP_NBUTTONS] = {
    {0,0,0,0, SDLK_UP,    1, NULL,    GP_LIGHT,  -1},
    {0,0,0,0, SDLK_DOWN,  2, NULL,    GP_LIGHT,  -1},
    {0,0,0,0, SDLK_LEFT,  3, NULL,    GP_LIGHT,  -1},
    {0,0,0,0, SDLK_RIGHT, 4, NULL,    GP_LIGHT,  -1},
    {0,0,0,0, SDLK_x,     0, "ACT",   GP_RED,    -1},
    {0,0,0,0, SDLK_z,     0, "JUMP",  GP_BLUE,   -1},
    {0,0,0,0, SDLK_1,     0, "1",     GP_ORANGE, -1},
    {0,0,0,0, SDLK_2,     0, "2",     GP_ORANGE, -1},
    {0,0,0,0, SDLK_3,     0, "3",     GP_ORANGE, -1},
    {0,0,0,0, SDLK_F2,    0, "NEW",   GP_GREY,   -1},
    {0,0,0,0, SDLK_F3,    0, "PAUSE", GP_GREY,   -1},
    {0,0,0,0, SDLK_c,     0, "BLOCK", CZ_GREEN,  -1},
};

static void gp_place(int index, int x, int y, int w, int h)
{
    gp_buttons[index].x = x;
    gp_buttons[index].y = y;
    gp_buttons[index].w = w;
    gp_buttons[index].h = h;
}

static void gp_layout(int W, int H)
{
    int bar = (W > H * 4 / 3) ? (W - H * 4 / 3) / 2 : 0;
    int S = bar * 2 / 7;
    int gap, m, small, pw, aw, ah, room, x, y, i;

    /* the D-pad is 3.5 cells wide with its margins and must fit the bar; only a
     * screen without bars (4:3 or narrower) gets the buttons over the picture */
    if (bar == 0) S = H / 7;
    if (S < H / 10) S = H / 10;
    if (S > H / 5) S = H / 5;
    gap = S / 8;
    m = S / 4;
    small = S * 2 / 3;
    pw = 3 * S + 2 * gap;

    /* the D-pad at the bottom left */
    x = m;
    y = H - m - pw;
    gp_place(0, x + S + gap, y, S, S);
    gp_place(1, x + S + gap, y + 2 * (S + gap), S, S);
    gp_place(2, x, y + S + gap, S, S);
    gp_place(3, x + 2 * (S + gap), y + S + gap, S, S);

    /* the actions at the bottom right, the items above them: the block is as wide
     * as the D-pad, so it fills the right bar as the D-pad fills the left one */
    x = W - m - pw;
    aw = (pw - gap) / 2;
    ah = S * 3 / 2;
    if (comix_buttons_mod())
    {
        /* two rows of actions: they give up height when the items would reach
         * the commands at the top */
        room = (H - m - (m + small + gap) - (small + gap) - gap) / 2;
        if (ah > room) ah = room;
        y = H - m - ah;
        gp_place(4, x, y, aw, ah);
        gp_place(5, x + aw + gap, y, aw, ah);
        y -= gap + ah;
        gp_place(11, x, y, aw, ah);
    }
    else
    {
        y = H - m - ah;
        gp_place(4, x, y, aw, ah);
        gp_place(5, x + aw + gap, y, aw, ah);
        gp_place(11, 0, 0, 0, 0);
    }
    y -= gap + small;
    for (i = 0; i < 3; i++)
    {
        gp_place(6 + i, x + i * (S + gap), y, S, small);
    }

    /* the commands in the top corners, away from the thumbs */
    gp_place(9, m, m, S, small);
    gp_place(10, W - m - S, m, S, small);
}
