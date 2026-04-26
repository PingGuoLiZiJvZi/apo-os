/*
 * desktop.c — Desktop compositor for apo-os
 *
 * This is the first user process (pid=1). It:
 *   - Owns the real GPU framebuffer (via mmap /device/fb at offset 0)
 *   - Composites child apps' shadow framebuffers onto the real fb
 *   - Draws taskbar, mouse cursor, window decorations
 *   - Handles mouse (virtio-tablet) and keyboard events
 *   - Power controls (shutdown / reboot)
 *
 * Child apps are launched via fork+execve. They mmap /device/fb and
 * get a per-process shadow buffer (kernel-redirected). When they call
 * fbsync, the kernel sets a dirty flag instead of doing real GPU sync.
 * Desktop polls dirty flags and composites.
 */

#include <NDL.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- Declarations from libos ---- */
extern void sys_shutdown(void);
extern void sys_reboot(void);

/* ---- Constants ---- */
#define TASKBAR_H      32
#define TITLEBAR_H     20
#define ICON_SIZE      24
#define ICON_GAP       12
#define MAX_WINDOWS    7   /* MAX_PROCS - 1 (desktop itself) */
#define CURSOR_W       12
#define CURSOR_H       18
#define ABS_MAX        32767  /* virtio-tablet coordinate range */

/* Linux key codes */
#define KEY_ESC   1

/* ---- App entries ---- */
typedef struct {
    const char *label;    /* Single char label for icon */
    const char *title;    /* Window title */
    const char *path;     /* exec path */
    const char *arg1;     /* optional argument */
    uint32_t   color;     /* icon color */
} AppEntry;

static const AppEntry k_apps[] = {
    {"P", "PAL",          "/bin/pal",   NULL,                       0x00e06060},
    {"B", "Flappy Bird",  "/bin/bird",  NULL,                       0x0060c060},
    {"M", "Mario",        "/bin/fceux", "/share/games/nes/mario.nes",0x006080e0},
    {"S", "Snake",        "/bin/snake", NULL,                       0x00c0c040},
};
#define NUM_APPS ((int)(sizeof(k_apps) / sizeof(k_apps[0])))

/* ---- Window ---- */
typedef struct {
    int active;
    int pid;
    int x, y;        /* window position (top-left of titlebar) */
    int cw, ch;      /* content size (= screen size, clipped when compositing) */
    int pcb_idx;     /* index in PCBs[] = pid - 1, for mmap offset */
    uint32_t *fb;    /* mmap'd child shadow fb (NULL if not mapped) */
    const char *title;
    int dragging;
    int drag_ox, drag_oy;  /* drag offsets */
} Window;

/* ---- Globals ---- */
static int screen_w, screen_h;
static uint32_t *real_fb;       /* mmap'd real GPU framebuffer */
static int fb_pitch;            /* bytes per row */
static uint64_t fb_size;        /* bytes of one framebuffer (w*h*4) */
static int evtdev = -1;
static int fbsyncdev = -1;

static Window windows[MAX_WINDOWS];
static int num_windows = 0;

static int mouse_x, mouse_y;
static int mouse_btn_left = 0;

/* Cursor bitmap (1 = white, 2 = black, 0 = transparent) */
static const uint8_t cursor_bitmap[CURSOR_H][CURSOR_W] = {
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

/* ---- Helper functions ---- */

static int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void fb_pixel(int x, int y, uint32_t c) {
    if (x >= 0 && x < screen_w && y >= 0 && y < screen_h) {
        real_fb[y * screen_w + x] = c;
    }
}

static void fb_fill_rect(int x, int y, int w, int h, uint32_t c) {
    int x0 = clampi(x, 0, screen_w);
    int y0 = clampi(y, 0, screen_h);
    int x1 = clampi(x + w, 0, screen_w);
    int y1 = clampi(y + h, 0, screen_h);
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            real_fb[py * screen_w + px] = c;
        }
    }
}

static uint32_t alpha_blend(uint32_t bg, uint32_t fg, int alpha) {
    /* Simple alpha blend: alpha 0-255 */
    int br = (bg >> 16) & 0xff, bg_ = (bg >> 8) & 0xff, bb = bg & 0xff;
    int fr = (fg >> 16) & 0xff, fg_ = (fg >> 8) & 0xff, fb_ = fg & 0xff;
    int r = (fr * alpha + br * (255 - alpha)) / 255;
    int g = (fg_ * alpha + bg_ * (255 - alpha)) / 255;
    int b = (fb_ * alpha + bb * (255 - alpha)) / 255;
    return (uint32_t)((r << 16) | (g << 8) | b);
}

/* ---- Simple 5x7 font for icon labels ---- */

static const uint8_t font5x7[128][7] = {
    ['P'] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    ['B'] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    ['M'] = {0x11,0x1B,0x15,0x11,0x11,0x11,0x11},
    ['S'] = {0x0E,0x10,0x10,0x0E,0x01,0x01,0x1E},
    ['X'] = {0x11,0x0A,0x04,0x04,0x0A,0x11,0x00},
};

static void draw_char(int cx, int cy, char ch, uint32_t color) {
    int idx = (int)(unsigned char)ch;
    if (idx < 0 || idx > 127) return;
    const uint8_t *glyph = font5x7[idx];
    for (int row = 0; row < 7; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            if (bits & (0x10 >> col)) {
                fb_pixel(cx + col, cy + row, color);
            }
        }
    }
}

/* ---- Background ---- */

static void draw_background(void) {
    for (int y = 0; y < screen_h - TASKBAR_H; y++) {
        /* Gradient: dark blue-gray at top, dark purple at bottom */
        int t = y * 255 / (screen_h - TASKBAR_H);
        int r = 18 + t * 12 / 255;
        int g = 18 + t * 6 / 255;
        int b = 30 + t * 20 / 255;
        uint32_t c = (uint32_t)((r << 16) | (g << 8) | b);
        for (int x = 0; x < screen_w; x++) {
            real_fb[y * screen_w + x] = c;
        }
    }
}

/* ---- Taskbar ---- */

static void draw_taskbar(void) {
    int ty = screen_h - TASKBAR_H;

    /* Taskbar background: dark gradient */
    for (int y = ty; y < screen_h; y++) {
        int t = (y - ty) * 255 / TASKBAR_H;
        int r = 25 + t * 5 / 255;
        int g = 25 + t * 3 / 255;
        int b = 32 + t * 8 / 255;
        uint32_t c = (uint32_t)((r << 16) | (g << 8) | b);
        for (int x = 0; x < screen_w; x++) {
            real_fb[y * screen_w + x] = c;
        }
    }

    /* Top border line */
    for (int x = 0; x < screen_w; x++) {
        fb_pixel(x, ty, 0x00404060);
    }

    /* App icons on the left */
    int ix = ICON_GAP;
    int iy = ty + (TASKBAR_H - ICON_SIZE) / 2;
    for (int i = 0; i < NUM_APPS; i++) {
        fb_fill_rect(ix, iy, ICON_SIZE, ICON_SIZE, k_apps[i].color);
        /* Draw label character centered */
        int cx = ix + (ICON_SIZE - 5) / 2;
        int cy = iy + (ICON_SIZE - 7) / 2;
        draw_char(cx, cy, k_apps[i].label[0], 0x00ffffff);
        ix += ICON_SIZE + ICON_GAP;
    }

    /* Power icons on the right */
    /* Shutdown (red square) */
    int sx = screen_w - ICON_GAP - ICON_SIZE;
    fb_fill_rect(sx, iy, ICON_SIZE, ICON_SIZE, 0x00c04040);
    draw_char(sx + (ICON_SIZE - 5) / 2, iy + (ICON_SIZE - 7) / 2, 'S', 0x00ffffff);

    /* Reboot (yellow square) */
    int rx = sx - ICON_GAP - ICON_SIZE;
    fb_fill_rect(rx, iy, ICON_SIZE, ICON_SIZE, 0x00c0a030);
    draw_char(rx + (ICON_SIZE - 5) / 2, iy + (ICON_SIZE - 7) / 2, 'B', 0x00ffffff);
}

/* ---- Window drawing ---- */

static void draw_window_decoration(Window *w) {
    if (!w->active) return;

    int wx = w->x;
    int wy = w->y;
    /* Calculate visible content size */
    int vis_w = w->cw;
    int vis_h = w->ch;
    if (vis_w > screen_w - 4) vis_w = screen_w - 4;
    if (vis_h > screen_h - TASKBAR_H - TITLEBAR_H - 4) vis_h = screen_h - TASKBAR_H - TITLEBAR_H - 4;

    int total_w = vis_w + 4;  /* 2px border each side */
    int total_h = vis_h + TITLEBAR_H + 4;

    /* Window border */
    fb_fill_rect(wx, wy, total_w, 2, 0x00505070);   /* top */
    fb_fill_rect(wx, wy + total_h - 2, total_w, 2, 0x00505070); /* bottom */
    fb_fill_rect(wx, wy, 2, total_h, 0x00505070);   /* left */
    fb_fill_rect(wx + total_w - 2, wy, 2, total_h, 0x00505070); /* right */

    /* Title bar */
    fb_fill_rect(wx + 2, wy + 2, vis_w, TITLEBAR_H, 0x00354060);

    /* Title text */
    if (w->title) {
        int tx = wx + 6;
        int tty = wy + 2 + (TITLEBAR_H - 7) / 2;
        for (int i = 0; w->title[i] && i < 20; i++) {
            draw_char(tx, tty, w->title[i], 0x00d0d0e0);
            tx += 6;
        }
    }

    /* Close button (X) */
    int cbx = wx + 2 + vis_w - 16;
    int cby = wy + 2 + (TITLEBAR_H - 7) / 2;
    fb_fill_rect(cbx - 2, wy + 2, 18, TITLEBAR_H, 0x00804040);
    draw_char(cbx + 4, cby, 'X', 0x00ffffff);
}

static void composite_window(Window *w) {
    if (!w->active || !w->fb) return;

    int wx = w->x + 2;  /* content starts after border */
    int wy = w->y + 2 + TITLEBAR_H;

    int vis_w = w->cw;
    int vis_h = w->ch;
    if (vis_w > screen_w - 4) vis_w = screen_w - 4;
    if (vis_h > screen_h - TASKBAR_H - TITLEBAR_H - 4) vis_h = screen_h - TASKBAR_H - TITLEBAR_H - 4;

    for (int row = 0; row < vis_h; row++) {
        int dy = wy + row;
        if (dy < 0 || dy >= screen_h - TASKBAR_H) continue;
        for (int col = 0; col < vis_w; col++) {
            int dx = wx + col;
            if (dx < 0 || dx >= screen_w) continue;
            /* Read from child's shadow fb */
            uint32_t pixel = w->fb[row * screen_w + col];
            real_fb[dy * screen_w + dx] = pixel;
        }
    }
}

/* ---- Mouse cursor ---- */

static uint32_t cursor_save[CURSOR_W * CURSOR_H];

static void save_under_cursor(void) {
    for (int row = 0; row < CURSOR_H; row++) {
        int y = mouse_y + row;
        for (int col = 0; col < CURSOR_W; col++) {
            int x = mouse_x + col;
            if (x >= 0 && x < screen_w && y >= 0 && y < screen_h) {
                cursor_save[row * CURSOR_W + col] = real_fb[y * screen_w + x];
            } else {
                cursor_save[row * CURSOR_W + col] = 0;
            }
        }
    }
}

static void draw_cursor(void) {
    for (int row = 0; row < CURSOR_H; row++) {
        int y = mouse_y + row;
        if (y < 0 || y >= screen_h) continue;
        for (int col = 0; col < CURSOR_W; col++) {
            int x = mouse_x + col;
            if (x < 0 || x >= screen_w) continue;
            uint8_t val = cursor_bitmap[row][col];
            if (val == 1) {
                real_fb[y * screen_w + x] = 0x00ffffff;
            } else if (val == 2) {
                real_fb[y * screen_w + x] = 0x00000000;
            }
        }
    }
}

/* ---- Event handling ---- */

static int poll_events(int *out_key, int *out_key_type,
                       int *out_mouse_abs_code, int *out_mouse_abs_val) {
    *out_key = 0;
    *out_key_type = 0;
    *out_mouse_abs_code = -1;
    *out_mouse_abs_val = 0;

    if (evtdev < 0) return 0;

    char buf[64];
    int n = read(evtdev, buf, sizeof(buf) - 1);
    if (n <= 0) return 0;
    buf[n] = '\0';

    int code = 0, val = 0;
    if (sscanf(buf, "kd %d", &code) == 1) {
        *out_key = code;
        *out_key_type = 1; /* key down */
        return 1;
    }
    if (sscanf(buf, "ku %d", &code) == 1) {
        *out_key = code;
        *out_key_type = 2; /* key up */
        return 1;
    }
    if (sscanf(buf, "ma %d %d", &code, &val) == 2) {
        *out_mouse_abs_code = code;
        *out_mouse_abs_val = val;
        return 1;
    }
    return 0;
}

/* ---- Window management ---- */

static Window *find_window_by_pid(int pid) {
    for (int i = 0; i < num_windows; i++) {
        if (windows[i].active && windows[i].pid == pid) return &windows[i];
    }
    return NULL;
}

static int mmap_child_fb(int pcb_idx) {
    /* Desktop mmaps child's shadow fb at offset (1 + pcb_idx) * fb_size */
    uint64_t off = fb_size * (1 + (uint64_t)pcb_idx);
    int fd = open("/device/fb", O_RDWR, 0);
    if (fd < 0) return -1;
    void *p = mmap(NULL, (size_t)fb_size, PROT_READ, MAP_SHARED, fd, (off_t)off);
    close(fd);
    if (p == MAP_FAILED || p == (void *)-1) return -1;

    /* Find the window for this pcb_idx */
    for (int i = 0; i < num_windows; i++) {
        if (windows[i].active && windows[i].pcb_idx == pcb_idx) {
            windows[i].fb = (uint32_t *)p;
            return 0;
        }
    }
    munmap(p, (size_t)fb_size);
    return -1;
}

static int launch_app(const AppEntry *app) {
    pid_t pid = fork();
    if (pid < 0) {
        printf("[desktop] fork failed for %s\n", app->title);
        return -1;
    }
    if (pid == 0) {
        /* Child process */
        if (app->arg1) {
            char *argv[] = {(char *)app->path, (char *)app->arg1, NULL};
            char *envp[] = {NULL};
            execve(app->path, argv, envp);
        } else {
            char *argv[] = {(char *)app->path, NULL};
            char *envp[] = {NULL};
            execve(app->path, argv, envp);
        }
        _exit(127);
    }

    /* Parent: create window */
    if (num_windows >= MAX_WINDOWS) {
        printf("[desktop] no window slots\n");
        kill(pid, 9);
        return -1;
    }

    Window *w = &windows[num_windows++];
    w->active = 1;
    w->pid = pid;
    w->pcb_idx = pid - 1; /* pid = pcb_idx + 1 */
    w->cw = screen_w;
    w->ch = screen_h;
    w->title = app->title;
    w->fb = NULL;
    w->dragging = 0;

    /* Position: cascade from top-left */
    w->x = 10 + (num_windows - 1) * 30;
    w->y = 10 + (num_windows - 1) * 25;

    printf("[desktop] launched %s pid=%d pcb_idx=%d\n", app->title, pid, w->pcb_idx);
    return pid;
}

static void close_window(Window *w) {
    if (!w || !w->active) return;

    printf("[desktop] closing window pid=%d\n", w->pid);
    kill(w->pid, 9);
    waitpid(w->pid, NULL, 0);

    if (w->fb) {
        munmap(w->fb, (size_t)fb_size);
        w->fb = NULL;
    }

    w->active = 0;
    w->pid = 0;

    /* Compact window array */
    int idx = (int)(w - windows);
    for (int i = idx; i < num_windows - 1; i++) {
        windows[i] = windows[i + 1];
    }
    num_windows--;
    memset(&windows[num_windows], 0, sizeof(Window));
}

/* Check if a child has exited */
static void reap_children(void) {
    for (int i = 0; i < num_windows; i++) {
        if (!windows[i].active) continue;
        int status = 0;
        int r = waitpid(windows[i].pid, &status, 1); /* WNOHANG */
        if (r == windows[i].pid) {
            printf("[desktop] child pid=%d exited status=%d\n", windows[i].pid, status);
            if (windows[i].fb) {
                munmap(windows[i].fb, (size_t)fb_size);
                windows[i].fb = NULL;
            }
            windows[i].active = 0;
            /* Compact */
            for (int j = i; j < num_windows - 1; j++) {
                windows[j] = windows[j + 1];
            }
            num_windows--;
            memset(&windows[num_windows], 0, sizeof(Window));
            i--; /* re-check this index */
        }
    }
}

/* ---- Hit testing ---- */

static int hit_taskbar_app(int mx, int my) {
    int ty = screen_h - TASKBAR_H;
    if (my < ty || my >= screen_h) return -1;
    int iy = ty + (TASKBAR_H - ICON_SIZE) / 2;
    if (my < iy || my >= iy + ICON_SIZE) return -1;

    int ix = ICON_GAP;
    for (int i = 0; i < NUM_APPS; i++) {
        if (mx >= ix && mx < ix + ICON_SIZE) return i;
        ix += ICON_SIZE + ICON_GAP;
    }
    return -1;
}

/* Returns: 0 = shutdown, 1 = reboot, -1 = no hit */
static int hit_taskbar_power(int mx, int my) {
    int ty = screen_h - TASKBAR_H;
    if (my < ty || my >= screen_h) return -1;
    int iy = ty + (TASKBAR_H - ICON_SIZE) / 2;
    if (my < iy || my >= iy + ICON_SIZE) return -1;

    /* Shutdown */
    int sx = screen_w - ICON_GAP - ICON_SIZE;
    if (mx >= sx && mx < sx + ICON_SIZE) return 0;

    /* Reboot */
    int rx = sx - ICON_GAP - ICON_SIZE;
    if (mx >= rx && mx < rx + ICON_SIZE) return 1;

    return -1;
}

/* Returns window index hit, or -1 */
/* *hit_close = 1 if the close button was hit */
/* *hit_titlebar = 1 if the titlebar (not close) was hit */
static int hit_window(int mx, int my, int *hit_close, int *hit_titlebar) {
    *hit_close = 0;
    *hit_titlebar = 0;

    /* Check windows from top (last) to bottom (first) */
    for (int i = num_windows - 1; i >= 0; i--) {
        Window *w = &windows[i];
        if (!w->active) continue;

        int vis_w = w->cw;
        int vis_h = w->ch;
        if (vis_w > screen_w - 4) vis_w = screen_w - 4;
        if (vis_h > screen_h - TASKBAR_H - TITLEBAR_H - 4) vis_h = screen_h - TASKBAR_H - TITLEBAR_H - 4;

        int total_w = vis_w + 4;
        int total_h = vis_h + TITLEBAR_H + 4;

        if (mx >= w->x && mx < w->x + total_w &&
            my >= w->y && my < w->y + total_h) {

            /* Check close button */
            int cbx = w->x + 2 + vis_w - 16;
            int cby = w->y + 2;
            if (mx >= cbx - 2 && mx < cbx + 16 &&
                my >= cby && my < cby + TITLEBAR_H) {
                *hit_close = 1;
                return i;
            }

            /* Check titlebar */
            if (my >= w->y + 2 && my < w->y + 2 + TITLEBAR_H) {
                *hit_titlebar = 1;
                return i;
            }

            return i;
        }
    }
    return -1;
}

/* ---- Dirty bitmask polling ---- */

static uint8_t poll_dirty(void) {
    if (fbsyncdev < 0) return 0;
    uint8_t mask = 0;
    int n = read(fbsyncdev, &mask, 1);
    if (n <= 0) return 0;
    return mask;
}

/* ---- Compositing ---- */

static void render_frame(void) {
    /* Draw background */
    draw_background();

    /* Composite windows (bottom to top) */
    for (int i = 0; i < num_windows; i++) {
        Window *w = &windows[i];
        if (!w->active) continue;

        /* Try to mmap child fb if not yet mapped */
        if (!w->fb) {
            mmap_child_fb(w->pcb_idx);
        }

        draw_window_decoration(w);
        composite_window(w);
    }

    /* Draw taskbar (on top of everything) */
    draw_taskbar();

    /* Draw cursor */
    draw_cursor();

    /* Flush to GPU */
    NDL_FlushRect(0, 0, screen_w, screen_h);
}

/* ---- Main ---- */

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    printf("[desktop] starting...\n");

    /* Initialize NDL for framebuffer */
    NDL_Init(0);
    int cw = 0, ch = 0;
    NDL_OpenCanvas(&cw, &ch);
    screen_w = cw;
    screen_h = ch;

    printf("[desktop] screen %dx%d\n", screen_w, screen_h);

    /* Get direct framebuffer pointer (real GPU fb for pid=1) */
    real_fb = NDL_GetFramebuffer(&fb_pitch);
    if (!real_fb) {
        printf("[desktop] failed to get framebuffer\n");
        return 1;
    }

    fb_size = (uint64_t)screen_w * (uint64_t)screen_h * 4ULL;

    /* Open event and fbsync devices */
    evtdev = open("/device/events", O_RDONLY, 0);
    fbsyncdev = open("/device/fbsync", O_RDWR, 0);

    /* Initialize mouse to center */
    mouse_x = screen_w / 2;
    mouse_y = screen_h / 2;

    /* Initialize windows */
    memset(windows, 0, sizeof(windows));
    num_windows = 0;

    printf("[desktop] ready\n");

    /* Initial draw */
    int dirty = 1;

    while (1) {
        int got_event = 0;
        int key, key_type;
        int ma_code, ma_val;

        /* Process all pending events */
        while (poll_events(&key, &key_type, &ma_code, &ma_val)) {
            got_event = 1;

            if (key_type == 1 && key == KEY_ESC) {
                /* ESC: close topmost window */
                if (num_windows > 0) {
                    close_window(&windows[num_windows - 1]);
                    dirty = 1;
                }
            }

            /* Mouse absolute position */
            if (ma_code == 0) {
                /* X coordinate */
                mouse_x = (int)((long long)ma_val * screen_w / (ABS_MAX + 1));
                mouse_x = clampi(mouse_x, 0, screen_w - 1);
                dirty = 1;
            } else if (ma_code == 1) {
                /* Y coordinate */
                mouse_y = (int)((long long)ma_val * screen_h / (ABS_MAX + 1));
                mouse_y = clampi(mouse_y, 0, screen_h - 1);
                dirty = 1;
            }

            /* Mouse button (BTN_LEFT = 0x110 = 272) */
            if (key_type == 1 && key == 272) {
                mouse_btn_left = 1;

                /* Check hits */
                int app_idx = hit_taskbar_app(mouse_x, mouse_y);
                if (app_idx >= 0) {
                    launch_app(&k_apps[app_idx]);
                    dirty = 1;
                    continue;
                }

                int power = hit_taskbar_power(mouse_x, mouse_y);
                if (power == 0) {
                    printf("[desktop] shutdown requested\n");
                    sys_shutdown();
                } else if (power == 1) {
                    printf("[desktop] reboot requested\n");
                    sys_reboot();
                }

                int hc = 0, ht = 0;
                int widx = hit_window(mouse_x, mouse_y, &hc, &ht);
                if (widx >= 0) {
                    if (hc) {
                        close_window(&windows[widx]);
                        dirty = 1;
                    } else if (ht) {
                        /* Start drag */
                        windows[widx].dragging = 1;
                        windows[widx].drag_ox = mouse_x - windows[widx].x;
                        windows[widx].drag_oy = mouse_y - windows[widx].y;

                        /* Bring to front */
                        if (widx < num_windows - 1) {
                            Window tmp = windows[widx];
                            for (int j = widx; j < num_windows - 1; j++) {
                                windows[j] = windows[j + 1];
                            }
                            windows[num_windows - 1] = tmp;
                        }
                        dirty = 1;
                    }
                }
            }

            if (key_type == 2 && key == 272) {
                mouse_btn_left = 0;
                /* End all drags */
                for (int i = 0; i < num_windows; i++) {
                    windows[i].dragging = 0;
                }
            }
        }

        /* Handle dragging */
        if (mouse_btn_left) {
            for (int i = 0; i < num_windows; i++) {
                if (windows[i].dragging) {
                    int new_x = mouse_x - windows[i].drag_ox;
                    int new_y = mouse_y - windows[i].drag_oy;
                    if (new_x != windows[i].x || new_y != windows[i].y) {
                        windows[i].x = new_x;
                        windows[i].y = new_y;
                        dirty = 1;
                    }
                }
            }
        }

        /* Check for child dirty flags */
        uint8_t dmask = poll_dirty();
        if (dmask) {
            dirty = 1;
        }

        /* Reap exited children */
        reap_children();

        /* Render if dirty */
        if (dirty) {
            render_frame();
            dirty = 0;
        }

        if (!got_event && !dmask) {
            /* No events, yield CPU */
            yield();
        }
    }

    NDL_Quit();
    return 0;
}
