#include "desktop.h"

static void sync_input_focus(void) {
    if (inputdev >= 0) {
        write(inputdev, &focus_pid, sizeof(focus_pid));
    }
}

void set_focus_pid(int pid) {
    focus_pid = pid > 0 ? pid : 1;
    sync_input_focus();
}

void focus_window(Window *w) {
    if (w && w->active) {
        set_focus_pid(w->pid);
    } else {
        set_focus_pid(1);
    }
}

void focus_top_window_or_desktop(void) {
    if (num_windows > 0) {
        focus_window(&windows[num_windows - 1]);
    } else {
        set_focus_pid(1);
    }
}

int mmap_child_fb(int pcb_idx) {
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

int launch_app(const AppEntry *app) {
    pid_t pid = fork();
    if (pid < 0) {
        printf("[desktop] fork failed for %s\n", app->title);
        return -1;
    }
    if (pid == 0) {
        /* Child process */
        if (app->has_arg) {
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
    w->cw = 1;
    w->ch = 1;
    w->src_x = 0;
    w->src_y = 0;
    w->content_valid = 0;
    set_window_content_rect(w, app_initial_content_rect(app));
    w->title = app->title;
    w->fb = NULL;
    w->dragging = 0;

    /* Position: cascade from top-left */
    w->x = 10 + (num_windows - 1) * 30;
    w->y = 10 + (num_windows - 1) * 25;

    focus_window(w);
    printf("[desktop] launched %s pid=%d pcb_idx=%d\n", app->title, pid, w->pcb_idx);
    return pid;
}

void close_window(Window *w) {
    if (!w || !w->active) return;

    int closing_pid = w->pid;
    printf("[desktop] closing window pid=%d\n", closing_pid);
    kill(closing_pid, 9);
    waitpid(closing_pid, NULL, 0);

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
    if (focus_pid == closing_pid) {
        focus_top_window_or_desktop();
    }
}

/* Check if a child has exited */
int reap_children(void) {
    int removed = 0;
    for (int i = 0; i < num_windows; i++) {
        if (!windows[i].active) continue;
        int status = 0;
        int r = waitpid(windows[i].pid, &status, 1); /* WNOHANG */
        if (r == windows[i].pid) {
            int exited_pid = windows[i].pid;
            printf("[desktop] child pid=%d exited status=%d\n", exited_pid, status);
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
            if (focus_pid == exited_pid) {
                focus_top_window_or_desktop();
            }
            removed = 1;
        }
    }
    return removed;
}

/* ---- Hit testing ---- */

/* Returns: 0 = shutdown, 1 = reboot, -1 = no hit */
int hit_taskbar_power(int mx, int my) {
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
int hit_window(int mx, int my, int *hit_close, int *hit_titlebar) {
    *hit_close = 0;
    *hit_titlebar = 0;

    /* Check windows from top (last) to bottom (first) */
    for (int i = num_windows - 1; i >= 0; i--) {
        Window *w = &windows[i];
        if (!w->active) continue;

        int vis_w, vis_h;
        window_visible_content(w, &vis_w, &vis_h);

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

static Window *find_window_by_pcb_idx(int pcb_idx) {
    for (int i = 0; i < num_windows; i++) {
        if (windows[i].active && windows[i].pcb_idx == pcb_idx) return &windows[i];
    }
    return NULL;
}

uint8_t poll_dirty(int *layout_dirty, Rect *damage, int *damage_count) {
    (void)layout_dirty;
    if (fbsyncdev < 0) return 0;

    FbSyncInfo info;
    memset(&info, 0, sizeof(info));
    int n = read(fbsyncdev, &info, sizeof(info));
    if (n <= 0) return 0;

    if (n == 1) return info.mask;

    for (int i = 0; i < FBSYNC_MAX_PROCS; i++) {
        if ((info.mask & (uint8_t)(1 << i)) == 0) continue;
        GpuDirtyRect *dr = &info.rects[i];
        if (dr->w <= 0 || dr->h <= 0) continue;

        Window *w = find_window_by_pcb_idx(i);
        if (!w) continue;
        if (damage && damage_count) {
            Rect src_dirty = {dr->x, dr->y, dr->w, dr->h};
            Rect src_content = {w->src_x, w->src_y, w->cw, w->ch};
            Rect clipped_src;
            if (rect_intersect(src_dirty, src_content, &clipped_src)) {
                Rect dst = {
                    w->x + 2 + (clipped_src.x - w->src_x),
                    w->y + 2 + TITLEBAR_H + (clipped_src.y - w->src_y),
                    clipped_src.w,
                    clipped_src.h
                };
                add_damage_rect(damage, damage_count, dst);
            }
        }
    }
    return info.mask;
}
