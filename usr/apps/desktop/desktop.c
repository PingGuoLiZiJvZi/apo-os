#include "desktop.h"

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
    inputdev = open("/device/input", O_WRONLY, 0);
    set_focus_pid(1);

    /* Initialize mouse to center */
    mouse_x = screen_w / 2;
    mouse_y = screen_h / 2;

    /* Initialize windows */
    memset(windows, 0, sizeof(windows));
    num_windows = 0;

    printf("[desktop] ready\n");
    run_boot_intro();

    /* Initial draw */
    int ui_dirty = 1;
    int cursor_dirty = 0;
    int cursor_old_x = mouse_x;
    int cursor_old_y = mouse_y;
    uint8_t pending_child_dirty = 0;
    uint32_t last_child_render_ms = 0;
    Rect pending_damage[MAX_DAMAGE_RECTS];
    int pending_damage_count = 0;

    while (1) {
        int got_event = 0;
        int event_count = 0;
        int key, key_type;
        int ma_code, ma_val;

        /* Process a bounded burst of pending events. */
        while (event_count < MAX_EVENT_BURST &&
               poll_events(&key, &key_type, &ma_code, &ma_val)) {
            got_event = 1;
            event_count++;

            if (key_type == 1 && key == KEY_ESC) {
                /* ESC: close topmost window */
                if (num_windows > 0) {
                    close_window(&windows[num_windows - 1]);
                    ui_dirty = 1;
                }
            }

            /* Mouse absolute position */
            if (ma_code == 0) {
                /* X coordinate */
                int new_x = (int)((long long)ma_val * screen_w / (ABS_MAX + 1));
                new_x = clampi(new_x, 0, screen_w - 1);
                if (new_x != mouse_x) {
                    if (!cursor_dirty) {
                        cursor_old_x = mouse_x;
                        cursor_old_y = mouse_y;
                    }
                    mouse_x = new_x;
                    cursor_dirty = 1;
                }
            } else if (ma_code == 1) {
                /* Y coordinate */
                int new_y = (int)((long long)ma_val * screen_h / (ABS_MAX + 1));
                new_y = clampi(new_y, 0, screen_h - 1);
                if (new_y != mouse_y) {
                    if (!cursor_dirty) {
                        cursor_old_x = mouse_x;
                        cursor_old_y = mouse_y;
                    }
                    mouse_y = new_y;
                    cursor_dirty = 1;
                }
            }

            /* Mouse button (BTN_LEFT = 0x110 = 272) */
            if (key_type == 1 && key == KEY_MOUSE_LEFT) {
                mouse_btn_left = 1;

                /* Check hits */
                int app_idx = hit_taskbar_app(mouse_x, mouse_y);
                if (app_idx >= 0) {
                    launch_app(&apps[app_idx]);
                    ui_dirty = 1;
                    continue;
                }

                int power = hit_taskbar_power(mouse_x, mouse_y);
                if (power == 0) {
                    printf("[desktop] shutdown requested\n");
                    sys_shutdown();
                } else if (power == 1) {
                    printf("[desktop] reboot requested\n");
                    sys_reboot();
                } else if (mouse_y >= screen_h - TASKBAR_H) {
                    set_focus_pid(1);
                    ui_dirty = 1;
                    continue;
                }

                int hc = 0, ht = 0;
                int widx = hit_window(mouse_x, mouse_y, &hc, &ht);
                if (widx >= 0) {
                    if (hc) {
                        close_window(&windows[widx]);
                        ui_dirty = 1;
                    } else {
                        Window clicked = windows[widx];
                        if (ht) {
                            clicked.dragging = 1;
                            clicked.drag_ox = mouse_x - clicked.x;
                            clicked.drag_oy = mouse_y - clicked.y;
                        }

                        if (widx < num_windows - 1) {
                            for (int j = widx; j < num_windows - 1; j++) {
                                windows[j] = windows[j + 1];
                            }
                            windows[num_windows - 1] = clicked;
                            widx = num_windows - 1;
                        } else {
                            windows[widx] = clicked;
                        }

                        focus_window(&windows[widx]);
                        ui_dirty = 1;
                    }
                } else {
                    set_focus_pid(1);
                    ui_dirty = 1;
                }
            }

            if (key_type == 2 && key == KEY_MOUSE_LEFT) {
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
                        ui_dirty = 1;
                    }
                }
            }
        }

        /* Check for child dirty flags */
        int layout_dirty = 0;
        uint8_t dmask = poll_dirty(&layout_dirty, pending_damage, &pending_damage_count);
        if (dmask) {
            pending_child_dirty |= dmask;
        }
        if (layout_dirty) {
            ui_dirty = 1;
        }

        /* Reap exited children */
        if (reap_children()) {
            ui_dirty = 1;
        }

        int rendered = 0;
        uint32_t now_ms = NDL_GetTicks();
        int child_render_due = pending_child_dirty &&
            (uint32_t)(now_ms - last_child_render_ms) >= COMPOSITOR_FRAME_MS;

        if (ui_dirty) {
            render_frame(1);
            ui_dirty = 0;
            cursor_dirty = 0;
            pending_child_dirty = 0;
            pending_damage_count = 0;
            last_child_render_ms = now_ms;
            rendered = 1;
        } else if (child_render_due) {
            if (pending_damage_count > 0) {
                render_damage(pending_damage, pending_damage_count);
            } else {
                render_frame(0);
            }
            cursor_dirty = 0;
            pending_child_dirty = 0;
            pending_damage_count = 0;
            last_child_render_ms = now_ms;
            rendered = 1;
        } else if (cursor_dirty) {
            move_cursor_only(cursor_old_x, cursor_old_y);
            cursor_dirty = 0;
            rendered = 1;
        }

        if (rendered || !got_event || pending_child_dirty) {
            /* No immediate work, or work has just been coalesced; let apps run. */
            yield();
        }
    }

    NDL_Quit();
    return 0;
}
