/*
 * Controls, sampled once per 60 Hz frame. Defender has one control panel
 * (the players take turns): a 2-way up/down joystick and the Thrust,
 * Reverse, Fire, Smart Bomb and Hyperspace buttons. Pads go through
 * SDL_GameController (SDL_GAMECONTROLLERCONFIG / gamecontrollerdb.txt);
 * every connected pad drives the panel.
 *
 *              pad                          keyboard
 *   up/down    left stick, D-pad            Up Down
 *   thrust     B or R1                      X or Alt
 *   reverse    L1                           Z
 *   fire       A                            Space or Ctrl
 *   smart bomb X                            C
 *   hyperspace Y                            V or Shift
 *   coin       Select                       5
 *   start      Start (1P), L1+Start (2P)    1 or Enter / 2
 *   menu       Select+R1                    F2
 *   graphics   Select+L1                    F3 (F4 glow, F5 scanlines)
 *   quit       hold Select+Start 1 s        Esc
 */
#ifndef DEFENDER_INPUT_H
#define DEFENDER_INPUT_H

#include <stdbool.h>

/* Frames Select+Start must be held to quit (1 s). */
#define QUIT_HOLD_FRAMES 60
#define MAX_PADS 2

typedef struct {
    int move_y;          /* -1 up, 0, 1 down */
    bool thrust, reverse, fire, smart_bomb, hyperspace;
    bool coin;
    bool start;          /* 1-player start */
    bool start2;         /* 2-player start */
    bool operator_menu;  /* Select+R1 or F2, edge-triggered */
    bool menu_held;      /* the same, while held */
    bool quit;           /* Select+Start held QUIT_HOLD_FRAMES, Esc, or window close */
    bool gfx_next;       /* Select+L1 or F3, edge-triggered: next graphics preset */
    bool gfx_glow;       /* F4, edge-triggered */
    bool gfx_scanlines;  /* F5, edge-triggered */
    int pads;            /* game controllers connected */
} InputFrame;

void input_init(void);
/* Pump SDL events and build this frame's input. */
void input_poll(InputFrame *out);
void input_shutdown(void);

#endif
