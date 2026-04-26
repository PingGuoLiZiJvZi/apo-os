#include "desktop.h"

int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

void fb_pixel(int x, int y, uint32_t c) {
    if (x >= 0 && x < screen_w && y >= 0 && y < screen_h) {
        real_fb[y * screen_w + x] = c;
    }
}

void fb_fill_span(uint32_t *dst, int count, uint32_t c) {
    for (int i = 0; i < count; i++) {
        dst[i] = c;
    }
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t c) {
    int x0 = clampi(x, 0, screen_w);
    int y0 = clampi(y, 0, screen_h);
    int x1 = clampi(x + w, 0, screen_w);
    int y1 = clampi(y + h, 0, screen_h);
    for (int py = y0; py < y1; py++) {
        fb_fill_span(real_fb + py * screen_w + x0, x1 - x0, c);
    }
}

int rect_clip(Rect *r) {
    int x1 = clampi(r->x, 0, screen_w);
    int y1 = clampi(r->y, 0, screen_h);
    int x2 = clampi(r->x + r->w, 0, screen_w);
    int y2 = clampi(r->y + r->h, 0, screen_h);
    if (x1 >= x2 || y1 >= y2) return 0;
    r->x = x1;
    r->y = y1;
    r->w = x2 - x1;
    r->h = y2 - y1;
    return 1;
}

void flush_rect(Rect r) {
    if (rect_clip(&r)) {
        NDL_FlushRect(r.x, r.y, r.w, r.h);
    }
}

void flush_cursor_damage(int old_x, int old_y) {
    Rect oldr = {old_x, old_y, CURSOR_W, CURSOR_H};
    Rect newr = {mouse_x, mouse_y, CURSOR_W, CURSOR_H};
    flush_rect(oldr);
    if (old_x != mouse_x || old_y != mouse_y) {
        flush_rect(newr);
    }
}
