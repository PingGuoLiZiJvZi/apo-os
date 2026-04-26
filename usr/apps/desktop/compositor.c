#include "desktop.h"

static void render_windows(void) {
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
}

void render_damage(Rect *damage, int damage_count) {
    if (!damage || damage_count <= 0) return;

    int had_cursor = cursor_drawn;
    int old_cursor_x = cursor_saved_x;
    int old_cursor_y = cursor_saved_y;
    restore_cursor();

    for (int d = 0; d < damage_count; d++) {
        Rect clip = damage[d];
        if (!rect_clip(&clip)) continue;
        if (clip.y >= screen_h - TASKBAR_H) continue;
        if (clip.y + clip.h > screen_h - TASKBAR_H) {
            clip.h = screen_h - TASKBAR_H - clip.y;
        }
        if (clip.w <= 0 || clip.h <= 0) continue;

        draw_background_rect(clip);

        for (int i = 0; i < num_windows; i++) {
            Window *w = &windows[i];
            if (!w->active) continue;
            if (!w->fb) {
                mmap_child_fb(w->pcb_idx);
            }
            if (!rect_intersect(window_total_rect(w), clip, NULL)) continue;
            draw_window_decoration_clipped(w, clip);
            composite_window_clipped(w, clip);
        }
    }

    present_cursor();

    for (int d = 0; d < damage_count; d++) {
        flush_rect(damage[d]);
    }
    if (had_cursor) {
        Rect old_cursor = {old_cursor_x, old_cursor_y, CURSOR_W, CURSOR_H};
        flush_rect(old_cursor);
    }
    Rect new_cursor = {mouse_x, mouse_y, CURSOR_W, CURSOR_H};
    flush_rect(new_cursor);
}

void render_frame(int full_redraw) {
    restore_cursor();

    if (full_redraw) {
        draw_background();
    }

    render_windows();

    if (full_redraw) {
        /* Draw taskbar (on top of everything) */
        draw_taskbar();
    }

    /* Draw cursor */
    present_cursor();

    /* Flush to GPU */
    if (full_redraw) {
        NDL_FlushRect(0, 0, screen_w, screen_h);
    } else {
        NDL_FlushRect(0, 0, screen_w, screen_h - TASKBAR_H);
    }
}
