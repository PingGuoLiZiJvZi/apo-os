/* ---- Simple 5x7 font for icon labels ---- */

#include <BMP.h>

#include "desktop.h"

#define DESKTOP_BG_PATH "/share/pictures/desktop-bg.bmp"

static uint32_t *desktop_bg;
static int desktop_bg_w;
static int desktop_bg_h;
static int desktop_bg_loaded;

static const uint8_t font5x7[128][7] = {
    ['A'] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    ['P'] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    ['B'] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    ['D'] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    ['E'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    ['I'] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F},
    ['K'] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    ['L'] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    ['M'] = {0x11,0x1B,0x15,0x11,0x11,0x11,0x11},
    ['N'] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    ['O'] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    ['R'] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    ['S'] = {0x0E,0x10,0x10,0x0E,0x01,0x01,0x1E},
    ['X'] = {0x11,0x0A,0x04,0x04,0x0A,0x11,0x00},
    ['a'] = {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
    ['b'] = {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E},
    ['d'] = {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F},
    ['e'] = {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
    ['f'] = {0x06,0x08,0x08,0x1E,0x08,0x08,0x08},
    ['i'] = {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
    ['k'] = {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
    ['l'] = {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
    ['m'] = {0x00,0x00,0x1A,0x15,0x15,0x15,0x15},
    ['n'] = {0x00,0x00,0x1E,0x11,0x11,0x11,0x11},
    ['o'] = {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
    ['p'] = {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
    ['r'] = {0x00,0x00,0x16,0x18,0x10,0x10,0x10},
    ['s'] = {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},
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

static void draw_char_clipped(int cx, int cy, char ch, uint32_t color, Rect clip) {
    int idx = (int)(unsigned char)ch;
    if (idx < 0 || idx > 127) return;
    const uint8_t *glyph = font5x7[idx];
    for (int row = 0; row < 7; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 5; col++) {
            int x = cx + col;
            int y = cy + row;
            if (x < clip.x || x >= clip.x + clip.w ||
                y < clip.y || y >= clip.y + clip.h) {
                continue;
            }
            if (bits & (0x10 >> col)) {
                fb_pixel(x, y, color);
            }
        }
    }
}

static void fb_fill_rect_clipped(int x, int y, int w, int h, uint32_t c, Rect clip) {
    Rect r = {x, y, w, h};
    Rect clipped;
    if (!rect_intersect(r, clip, &clipped)) return;
    fb_fill_rect(clipped.x, clipped.y, clipped.w, clipped.h, c);
}

/* ---- Background ---- */

static void load_desktop_background(void) {
    if (desktop_bg_loaded) return;
    desktop_bg_loaded = 1;
    desktop_bg = (uint32_t *)BMP_Load(DESKTOP_BG_PATH, &desktop_bg_w, &desktop_bg_h);
    if (!desktop_bg) {
        desktop_bg_w = 0;
        desktop_bg_h = 0;
    }
}

static uint32_t background_gradient_color(int y) {
    int desktop_h = screen_h - TASKBAR_H;
    int t = desktop_h > 0 ? y * 255 / desktop_h : 0;
    int r = 18 + t * 12 / 255;
    int g = 18 + t * 6 / 255;
    int b = 30 + t * 20 / 255;
    return (uint32_t)((r << 16) | (g << 8) | b);
}

static uint32_t background_image_pixel(int x, int y) {
    int src_x;
    int src_y;

    if (!desktop_bg || desktop_bg_w <= 0 || desktop_bg_h <= 0 ||
        screen_w <= 0 || screen_h <= 0) {
        return background_gradient_color(y);
    }

    src_x = x * desktop_bg_w / screen_w;
    src_y = y * desktop_bg_h / screen_h;
    src_x = clampi(src_x, 0, desktop_bg_w - 1);
    src_y = clampi(src_y, 0, desktop_bg_h - 1);
    return desktop_bg[src_y * desktop_bg_w + src_x];
}

void draw_background(void) {
    load_desktop_background();

    for (int y = 0; y < screen_h - TASKBAR_H; y++) {
        if (desktop_bg && desktop_bg_w == screen_w && desktop_bg_h == screen_h) {
            memcpy(real_fb + y * screen_w, desktop_bg + y * desktop_bg_w,
                   (size_t)screen_w * sizeof(uint32_t));
        } else {
            for (int x = 0; x < screen_w; x++) {
                real_fb[y * screen_w + x] = background_image_pixel(x, y);
            }
        }
    }
}

void draw_background_rect(Rect r) {
    load_desktop_background();

    if (!rect_clip(&r)) return;
    if (r.y >= screen_h - TASKBAR_H) return;
    if (r.y + r.h > screen_h - TASKBAR_H) {
        r.h = screen_h - TASKBAR_H - r.y;
    }
    for (int y = r.y; y < r.y + r.h; y++) {
        if (desktop_bg && desktop_bg_w == screen_w && desktop_bg_h == screen_h) {
            memcpy(real_fb + y * screen_w + r.x, desktop_bg + y * desktop_bg_w + r.x,
                   (size_t)r.w * sizeof(uint32_t));
        } else {
            for (int x = r.x; x < r.x + r.w; x++) {
                real_fb[y * screen_w + x] = background_image_pixel(x, y);
            }
        }
    }
}

/* ---- Desktop icons ---- */

static void fb_pixel_clipped(int x, int y, uint32_t color, Rect clip) {
    if (x < clip.x || x >= clip.x + clip.w ||
        y < clip.y || y >= clip.y + clip.h) {
        return;
    }
    fb_pixel(x, y, color);
}

static void fill_rect_clipped_direct(int x, int y, int w, int h,
                                     uint32_t color, Rect clip) {
    Rect r = {x, y, w, h};
    Rect clipped;
    if (!rect_intersect(r, clip, &clipped)) return;
    fb_fill_rect(clipped.x, clipped.y, clipped.w, clipped.h, color);
}

static void draw_app_icon_image(const AppEntry *app, Rect clip) {
    Rect icon = app->icon_rect;
    int src_size;
    int src_x0;
    int src_y0;

    if (!rect_intersect(icon, clip, NULL)) return;

    fill_rect_clipped_direct(icon.x - 1, icon.y - 1,
                             icon.w + 2, icon.h + 2, 0x00202028, clip);

    if (!app->icon_pixels || app->icon_w <= 0 || app->icon_h <= 0) {
        fill_rect_clipped_direct(icon.x, icon.y, icon.w, icon.h, app->color, clip);
        if (app->title[0]) {
            draw_char_clipped(icon.x + (icon.w - 5) / 2,
                              icon.y + (icon.h - 7) / 2,
                              app->title[0], 0x00ffffff, clip);
        }
        return;
    }

    src_size = app->icon_w < app->icon_h ? app->icon_w : app->icon_h;
    src_x0 = (app->icon_w - src_size) / 2;
    src_y0 = (app->icon_h - src_size) / 2;

    for (int y = 0; y < icon.h; y++) {
        int dy = icon.y + y;
        int sy = src_y0 + y * src_size / icon.h;
        for (int x = 0; x < icon.w; x++) {
            int dx = icon.x + x;
            int sx = src_x0 + x * src_size / icon.w;
            uint32_t c = app->icon_pixels[sy * app->icon_w + sx];
            fb_pixel_clipped(dx, dy, c, clip);
        }
    }
}

static void draw_app_label(const AppEntry *app, Rect clip) {
    int max_chars = DESKTOP_ICON_CELL_W / 6;
    int len = (int)strlen(app->title);
    int y = app->icon_rect.y + app->icon_rect.h + 6;
    int text_w;
    int x;

    if (len > max_chars) len = max_chars;
    if (len <= 0) return;
    text_w = len * 6 - 1;
    x = app->cell_rect.x + (app->cell_rect.w - text_w) / 2;

    for (int i = 0; i < len; i++) {
        draw_char_clipped(x + i * 6, y, app->title[i], 0x00f0f0f0, clip);
    }
}

static void draw_desktop_icons_in_clip(Rect clip) {
    Rect desktop_area = {0, 0, screen_w, screen_h - TASKBAR_H};
    if (!rect_intersect(clip, desktop_area, &clip)) return;

    for (int i = 0; i < num_apps; i++) {
        if (!rect_intersect(apps[i].cell_rect, clip, NULL)) continue;
        draw_app_icon_image(&apps[i], clip);
        draw_app_label(&apps[i], clip);
    }
}

void draw_desktop_icons(void) {
    Rect clip = {0, 0, screen_w, screen_h - TASKBAR_H};
    draw_desktop_icons_in_clip(clip);
}

void draw_desktop_icons_clipped(Rect clip) {
    draw_desktop_icons_in_clip(clip);
}

/* ---- Taskbar ---- */

void draw_taskbar(void) {
    int ty = screen_h - TASKBAR_H;

    /* Taskbar background: dark gradient */
    for (int y = ty; y < screen_h; y++) {
        int t = (y - ty) * 255 / TASKBAR_H;
        int r = 25 + t * 5 / 255;
        int g = 25 + t * 3 / 255;
        int b = 32 + t * 8 / 255;
        uint32_t c = (uint32_t)((r << 16) | (g << 8) | b);
        fb_fill_span(real_fb + y * screen_w, screen_w, c);
    }

    /* Top border line */
    for (int x = 0; x < screen_w; x++) {
        fb_pixel(x, ty, 0x00404060);
    }

    int iy = ty + (TASKBAR_H - ICON_SIZE) / 2;

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

void draw_window_decoration(Window *w) {
    if (!w->active) return;

    int wx = w->x;
    int wy = w->y;
    int vis_w, vis_h;
    window_visible_content(w, &vis_w, &vis_h);

    int total_w = vis_w + 4;  /* 2px border each side */
    int total_h = vis_h + TITLEBAR_H + 4;

    uint32_t border = (w->pid == focus_pid) ? 0x006070a0 : 0x00505070;
    uint32_t title_bg = (w->pid == focus_pid) ? 0x00486090 : 0x00354060;

    /* Window border */
    fb_fill_rect(wx, wy, total_w, 2, border);   /* top */
    fb_fill_rect(wx, wy + total_h - 2, total_w, 2, border); /* bottom */
    fb_fill_rect(wx, wy, 2, total_h, border);   /* left */
    fb_fill_rect(wx + total_w - 2, wy, 2, total_h, border); /* right */

    /* Title bar */
    fb_fill_rect(wx + 2, wy + 2, vis_w, TITLEBAR_H, title_bg);

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

void composite_window(Window *w) {
    if (!w->active || !w->fb) return;

    int vis_w, vis_h;
    window_visible_content(w, &vis_w, &vis_h);

    int dst_x = w->x + 2;  /* content starts after border */
    int dst_y = w->y + 2 + TITLEBAR_H;
    int src_x = w->src_x;
    int src_y = w->src_y;
    int copy_w = vis_w;
    int copy_h = vis_h;

    if (dst_x < 0) {
        src_x += -dst_x;
        copy_w += dst_x;
        dst_x = 0;
    }
    if (dst_y < 0) {
        src_y += -dst_y;
        copy_h += dst_y;
        dst_y = 0;
    }
    if (dst_x + copy_w > screen_w) copy_w = screen_w - dst_x;
    if (dst_y + copy_h > screen_h - TASKBAR_H) copy_h = screen_h - TASKBAR_H - dst_y;
    if (src_x + copy_w > screen_w) copy_w = screen_w - src_x;
    if (src_y + copy_h > screen_h) copy_h = screen_h - src_y;
    if (copy_w <= 0 || copy_h <= 0) return;

    for (int row = 0; row < copy_h; row++) {
        uint32_t *dst = real_fb + (dst_y + row) * screen_w + dst_x;
        uint32_t *src = w->fb + (src_y + row) * screen_w + src_x;
        memcpy(dst, src, (size_t)copy_w * sizeof(uint32_t));
    }
}

void draw_window_decoration_clipped(Window *w, Rect clip) {
    if (!w->active) return;
    if (!rect_intersect(window_total_rect(w), clip, NULL)) return;

    int wx = w->x;
    int wy = w->y;
    int vis_w, vis_h;
    window_visible_content(w, &vis_w, &vis_h);

    int total_w = vis_w + 4;
    int total_h = vis_h + TITLEBAR_H + 4;

    uint32_t border = (w->pid == focus_pid) ? 0x006070a0 : 0x00505070;
    uint32_t title_bg = (w->pid == focus_pid) ? 0x00486090 : 0x00354060;

    fb_fill_rect_clipped(wx, wy, total_w, 2, border, clip);
    fb_fill_rect_clipped(wx, wy + total_h - 2, total_w, 2, border, clip);
    fb_fill_rect_clipped(wx, wy, 2, total_h, border, clip);
    fb_fill_rect_clipped(wx + total_w - 2, wy, 2, total_h, border, clip);
    fb_fill_rect_clipped(wx + 2, wy + 2, vis_w, TITLEBAR_H, title_bg, clip);

    if (w->title) {
        int tx = wx + 6;
        int tty = wy + 2 + (TITLEBAR_H - 7) / 2;
        for (int i = 0; w->title[i] && i < 20; i++) {
            draw_char_clipped(tx, tty, w->title[i], 0x00d0d0e0, clip);
            tx += 6;
        }
    }

    int cbx = wx + 2 + vis_w - 16;
    int cby = wy + 2 + (TITLEBAR_H - 7) / 2;
    fb_fill_rect_clipped(cbx - 2, wy + 2, 18, TITLEBAR_H, 0x00804040, clip);
    draw_char_clipped(cbx + 4, cby, 'X', 0x00ffffff, clip);
}

void composite_window_clipped(Window *w, Rect clip) {
    if (!w->active || !w->fb) return;

    Rect dst_content = window_content_rect(w);
    Rect desktop_area = {0, 0, screen_w, screen_h - TASKBAR_H};
    Rect draw_rect;
    if (!rect_intersect(dst_content, clip, &draw_rect)) return;
    if (!rect_intersect(draw_rect, desktop_area, &draw_rect)) return;
    if (!rect_clip(&draw_rect)) return;

    int src_x = w->src_x + (draw_rect.x - dst_content.x);
    int src_y = w->src_y + (draw_rect.y - dst_content.y);
    int copy_w = draw_rect.w;
    int copy_h = draw_rect.h;

    if (src_x < 0) {
        draw_rect.x += -src_x;
        copy_w += src_x;
        src_x = 0;
    }
    if (src_y < 0) {
        draw_rect.y += -src_y;
        copy_h += src_y;
        src_y = 0;
    }
    if (src_x + copy_w > screen_w) copy_w = screen_w - src_x;
    if (src_y + copy_h > screen_h) copy_h = screen_h - src_y;
    if (copy_w <= 0 || copy_h <= 0) return;

    for (int row = 0; row < copy_h; row++) {
        uint32_t *dst = real_fb + (draw_rect.y + row) * screen_w + draw_rect.x;
        uint32_t *src = w->fb + (src_y + row) * screen_w + src_x;
        memcpy(dst, src, (size_t)copy_w * sizeof(uint32_t));
    }
}

/* ---- Mouse cursor ---- */

static void save_under_cursor(void) {
    cursor_saved_x = mouse_x;
    cursor_saved_y = mouse_y;
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

void restore_cursor(void) {
    if (!cursor_drawn) return;
    for (int row = 0; row < CURSOR_H; row++) {
        int y = cursor_saved_y + row;
        if (y < 0 || y >= screen_h) continue;
        for (int col = 0; col < CURSOR_W; col++) {
            int x = cursor_saved_x + col;
            if (x < 0 || x >= screen_w) continue;
            real_fb[y * screen_w + x] = cursor_save[row * CURSOR_W + col];
        }
    }
    cursor_drawn = 0;
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

void present_cursor(void) {
    save_under_cursor();
    draw_cursor();
    cursor_drawn = 1;
}

void move_cursor_only(int old_x, int old_y) {
    restore_cursor();
    present_cursor();
    flush_cursor_damage(old_x, old_y);
}

/* ---- Event handling ---- */
