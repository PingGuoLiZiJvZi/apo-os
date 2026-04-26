/*
 * Internal interfaces for the Desktop compositor.
 */

#ifndef DESKTOP_H
#define DESKTOP_H

#include <NDL.h>
#include <am.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

extern void sys_shutdown(void);
extern void sys_reboot(void);

#define TASKBAR_H      32
#define TITLEBAR_H     20
#define ICON_SIZE      24
#define ICON_GAP       12
#define MAX_WINDOWS    7
#define APP_COUNT      4
#define FBSYNC_MAX_PROCS 8
#define CURSOR_W       12
#define CURSOR_H       18
#define ABS_MAX        32767
#define COMPOSITOR_FRAME_MS 16
#define MAX_EVENT_BURST 64
#define MAX_DAMAGE_RECTS 16

#define KEY_ESC 1
#define KEY_MOUSE_LEFT 272

typedef struct {
    const char *label;
    const char *title;
    const char *path;
    const char *arg1;
    uint32_t color;
    int pref_w;
    int pref_h;
    int centered;
} AppEntry;

typedef struct {
    int active;
    int pid;
    int x, y;
    int cw, ch;
    int src_x, src_y;
    int content_valid;
    int pcb_idx;
    uint32_t *fb;
    const char *title;
    int dragging;
    int drag_ox, drag_oy;
} Window;

typedef struct {
    int x, y, w, h;
} Rect;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} GpuDirtyRect;

typedef struct {
    uint8_t mask;
    uint8_t reserved[3];
    GpuDirtyRect rects[FBSYNC_MAX_PROCS];
} FbSyncInfo;

extern const AppEntry apps[APP_COUNT];

extern int screen_w, screen_h;
extern uint32_t *real_fb;
extern int fb_pitch;
extern uint64_t fb_size;
extern int evtdev;
extern int fbsyncdev;
extern int inputdev;

extern Window windows[MAX_WINDOWS];
extern int num_windows;
extern int focus_pid;

extern int mouse_x, mouse_y;
extern int mouse_btn_left;

extern uint32_t cursor_save[CURSOR_W * CURSOR_H];
extern int cursor_saved_x;
extern int cursor_saved_y;
extern int cursor_drawn;
extern const uint8_t cursor_bitmap[CURSOR_H][CURSOR_W];

int clampi(int v, int lo, int hi);
void fb_pixel(int x, int y, uint32_t c);
void fb_fill_span(uint32_t *dst, int count, uint32_t c);
void fb_fill_rect(int x, int y, int w, int h, uint32_t c);
int rect_clip(Rect *r);
void flush_rect(Rect r);
void flush_cursor_damage(int old_x, int old_y);
int rect_intersect(Rect a, Rect b, Rect *out);
void add_damage_rect(Rect *rects, int *count, Rect r);
void window_visible_content(const Window *w, int *out_w, int *out_h);
Rect window_total_rect(Window *w);
Rect window_content_rect(Window *w);
int set_window_content_rect(Window *w, Rect r);
Rect app_initial_content_rect(const AppEntry *app);

void set_focus_pid(int pid);
void focus_window(Window *w);
void focus_top_window_or_desktop(void);

void draw_background(void);
void draw_background_rect(Rect r);
void draw_taskbar(void);
void draw_window_decoration(Window *w);
void composite_window(Window *w);
void draw_window_decoration_clipped(Window *w, Rect clip);
void composite_window_clipped(Window *w, Rect clip);
void restore_cursor(void);
void present_cursor(void);
void move_cursor_only(int old_x, int old_y);

int poll_events(int *out_key, int *out_key_type,
                int *out_mouse_abs_code, int *out_mouse_abs_val);

int mmap_child_fb(int pcb_idx);
int launch_app(const AppEntry *app);
void close_window(Window *w);
int reap_children(void);
int hit_taskbar_app(int mx, int my);
int hit_taskbar_power(int mx, int my);
int hit_window(int mx, int my, int *hit_close, int *hit_titlebar);
uint8_t poll_dirty(int *layout_dirty, Rect *damage, int *damage_count);

void render_damage(Rect *damage, int damage_count);
void render_frame(int full_redraw);
void run_boot_intro(void);

#endif
