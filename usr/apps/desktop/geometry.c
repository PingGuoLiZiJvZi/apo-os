#include "desktop.h"

int rect_intersect(Rect a, Rect b, Rect *out) {
    int x1 = a.x > b.x ? a.x : b.x;
    int y1 = a.y > b.y ? a.y : b.y;
    int x2 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    int y2 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    if (x1 >= x2 || y1 >= y2) return 0;
    if (out) {
        out->x = x1;
        out->y = y1;
        out->w = x2 - x1;
        out->h = y2 - y1;
    }
    return 1;
}

static int rect_touch_or_overlap(Rect a, Rect b) {
    return a.x <= b.x + b.w && a.x + a.w >= b.x &&
           a.y <= b.y + b.h && a.y + a.h >= b.y;
}

static void rect_union_into(Rect *dst, Rect src) {
    int x1 = dst->x < src.x ? dst->x : src.x;
    int y1 = dst->y < src.y ? dst->y : src.y;
    int x2 = dst->x + dst->w > src.x + src.w ? dst->x + dst->w : src.x + src.w;
    int y2 = dst->y + dst->h > src.y + src.h ? dst->y + dst->h : src.y + src.h;
    dst->x = x1;
    dst->y = y1;
    dst->w = x2 - x1;
    dst->h = y2 - y1;
}

void add_damage_rect(Rect *rects, int *count, Rect r) {
    if (!rect_clip(&r)) return;
    if (r.y >= screen_h - TASKBAR_H) return;
    if (r.y + r.h > screen_h - TASKBAR_H) {
        r.h = screen_h - TASKBAR_H - r.y;
    }
    if (r.w <= 0 || r.h <= 0) return;

    for (int i = 0; i < *count; i++) {
        if (!rect_touch_or_overlap(rects[i], r)) continue;
        rect_union_into(&rects[i], r);
        for (int j = 0; j < *count; j++) {
            if (j == i || !rect_touch_or_overlap(rects[i], rects[j])) continue;
            rect_union_into(&rects[i], rects[j]);
            rects[j] = rects[*count - 1];
            (*count)--;
            j--;
        }
        return;
    }

    if (*count < MAX_DAMAGE_RECTS) {
        rects[(*count)++] = r;
        return;
    }

    rect_union_into(&rects[0], r);
    *count = 1;
}

static int max_content_w(void) {
    int max_w = screen_w - 4;
    return max_w > 1 ? max_w : 1;
}

static int max_content_h(void) {
    int max_h = screen_h - TASKBAR_H - TITLEBAR_H - 4;
    return max_h > 1 ? max_h : 1;
}

void window_visible_content(const Window *w, int *out_w, int *out_h) {
    int vis_w = w->cw;
    int vis_h = w->ch;
    if (vis_w > max_content_w()) vis_w = max_content_w();
    if (vis_h > max_content_h()) vis_h = max_content_h();
    if (vis_w < 1) vis_w = 1;
    if (vis_h < 1) vis_h = 1;
    *out_w = vis_w;
    *out_h = vis_h;
}

Rect window_total_rect(Window *w) {
    int vis_w, vis_h;
    window_visible_content(w, &vis_w, &vis_h);
    Rect r = {w->x, w->y, vis_w + 4, vis_h + TITLEBAR_H + 4};
    return r;
}

Rect window_content_rect(Window *w) {
    int vis_w, vis_h;
    window_visible_content(w, &vis_w, &vis_h);
    Rect r = {w->x + 2, w->y + 2 + TITLEBAR_H, vis_w, vis_h};
    return r;
}

static int clip_source_rect(Rect *r) {
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

int set_window_content_rect(Window *w, Rect r) {
    if (!w || !clip_source_rect(&r)) return 0;

    if (w->content_valid) {
        int x1 = w->src_x < r.x ? w->src_x : r.x;
        int y1 = w->src_y < r.y ? w->src_y : r.y;
        int x2 = w->src_x + w->cw;
        int y2 = w->src_y + w->ch;
        if (r.x + r.w > x2) x2 = r.x + r.w;
        if (r.y + r.h > y2) y2 = r.y + r.h;
        r.x = x1;
        r.y = y1;
        r.w = x2 - x1;
        r.h = y2 - y1;
        if (!clip_source_rect(&r)) return 0;
    }

    if (r.w > max_content_w()) r.w = max_content_w();
    if (r.h > max_content_h()) r.h = max_content_h();

    int changed = !w->content_valid ||
        w->src_x != r.x || w->src_y != r.y ||
        w->cw != r.w || w->ch != r.h;
    w->src_x = r.x;
    w->src_y = r.y;
    w->cw = r.w;
    w->ch = r.h;
    w->content_valid = 1;
    return changed;
}

Rect app_initial_content_rect(const AppEntry *app) {
    int w = app->pref_w > 0 ? app->pref_w : 320;
    int h = app->pref_h > 0 ? app->pref_h : 240;
    if (w > screen_w) w = screen_w;
    if (h > screen_h) h = screen_h;

    Rect r;
    r.w = w;
    r.h = h;
    if (app->centered) {
        r.x = (screen_w - w) / 2;
        r.y = (screen_h - h) / 2;
    } else {
        r.x = 0;
        r.y = 0;
    }
    clip_source_rect(&r);
    return r;
}
