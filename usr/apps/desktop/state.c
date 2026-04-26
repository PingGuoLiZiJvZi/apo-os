#include "desktop.h"

const AppEntry apps[APP_COUNT] = {
    {"P", "PAL",          "/bin/pal",   NULL,                        0x00e06060, 640, 400, 1},
    {"B", "Flappy Bird",  "/bin/bird",  NULL,                        0x0060c060, 287, 300, 1},
    {"M", "Mario",        "/bin/fceux", "/share/games/nes/mario.nes", 0x006080e0, 256, 240, 1},
    {"S", "Snake",        "/bin/snake", NULL,                        0x00c0c040, 256, 192, 0},
};
int screen_w, screen_h;
uint32_t *real_fb;
int fb_pitch;
uint64_t fb_size;
int evtdev = -1;
int fbsyncdev = -1;
int inputdev = -1;

Window windows[MAX_WINDOWS];
int num_windows = 0;
int focus_pid = 1;

int mouse_x, mouse_y;
int mouse_btn_left = 0;

uint32_t cursor_save[CURSOR_W * CURSOR_H];
int cursor_saved_x = 0;
int cursor_saved_y = 0;
int cursor_drawn = 0;

/* Cursor bitmap (1 = white, 2 = black, 0 = transparent) */
const uint8_t cursor_bitmap[CURSOR_H][CURSOR_W] = {
    {2,0,0,0,0,0,0,0,0,0,0,0},
    {2,2,0,0,0,0,0,0,0,0,0,0},
    {2,1,2,0,0,0,0,0,0,0,0,0},
    {2,1,1,2,0,0,0,0,0,0,0,0},
    {2,1,1,1,2,0,0,0,0,0,0,0},
    {2,1,1,1,1,2,0,0,0,0,0,0},
    {2,1,1,1,1,1,2,0,0,0,0,0},
    {2,1,1,1,1,1,1,2,0,0,0,0},
    {2,1,1,1,1,1,1,1,2,0,0,0},
    {2,1,1,1,1,1,1,1,1,2,0,0},
    {2,1,1,1,1,1,2,2,2,2,0,0},
    {2,1,1,2,1,1,2,0,0,0,0,0},
    {2,1,2,0,2,1,1,2,0,0,0,0},
    {2,2,0,0,2,1,1,2,0,0,0,0},
    {2,0,0,0,0,2,1,1,2,0,0,0},
    {0,0,0,0,0,2,1,1,2,0,0,0},
    {0,0,0,0,0,0,2,1,2,0,0,0},
    {0,0,0,0,0,0,0,2,0,0,0,0},
};
