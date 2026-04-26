/* ---- Simple 5x7 font for icon labels ---- */

#include "desktop.h"

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

void draw_background(void) {
    for (int y = 0; y < screen_h - TASKBAR_H; y++) {
        /* Gradient: dark blue-gray at top, dark purple at bottom */
        int t = y * 255 / (screen_h - TASKBAR_H);
        int r = 18 + t * 12 / 255;
        int g = 18 + t * 6 / 255;
        int b = 30 + t * 20 / 255;
        uint32_t c = (uint32_t)((r << 16) | (g << 8) | b);
        fb_fill_span(real_fb + y * screen_w, screen_w, c);
    }
}

void draw_background_rect(Rect r) {
    if (!rect_clip(&r)) return;
    if (r.y >= screen_h - TASKBAR_H) return;
    if (r.y + r.h > screen_h - TASKBAR_H) {
        r.h = screen_h - TASKBAR_H - r.y;
    }
    for (int y = r.y; y < r.y + r.h; y++) {
        int t = y * 255 / (screen_h - TASKBAR_H);
        int red = 18 + t * 12 / 255;
        int green = 18 + t * 6 / 255;
        int blue = 30 + t * 20 / 255;
        uint32_t c = (uint32_t)((red << 16) | (green << 8) | blue);
        fb_fill_span(real_fb + y * screen_w + r.x, r.w, c);
    }
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

    /* App icons on the left */
    int ix = ICON_GAP;
    int iy = ty + (TASKBAR_H - ICON_SIZE) / 2;
    for (int i = 0; i < APP_COUNT; i++) {
        fb_fill_rect(ix, iy, ICON_SIZE, ICON_SIZE, apps[i].color);
        /* Draw label character centered */
        int cx = ix + (ICON_SIZE - 5) / 2;
        int cy = iy + (ICON_SIZE - 7) / 2;
        draw_char(cx, cy, apps[i].label[0], 0x00ffffff);
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
