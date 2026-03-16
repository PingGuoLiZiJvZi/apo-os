#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ndl_ipc.h>

typedef struct {
  int x;
  int y;
  int w;
  int h;
  uint32_t body_color;
} Window;

static int screen_w = 640;
static int screen_h = 480;
static int fbfd = -1;
static int evfd = -1;
static uint32_t *framebuf = NULL;
static uint32_t *appbuf = NULL;

static int mouse_x = 0;
static int mouse_y = 0;

#define KEY_TAB 15
#define BTN_LEFT 272
#define CURSOR_SIZE 14

typedef struct {
  int x;
  int y;
  int w;
  int h;
  int ready;
} ClientWindow;

static int app_ev_write_fd = -1;
static int app_draw_read_fd = -1;
static uint8_t app_ipc_buf[64 * 1024];
static int app_ipc_len = 0;
static int ipc_warn_cnt = 0;

static int clampi(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

static int abs_to_screen(int value, int max_px) {
  if (max_px <= 0) return 0;
  int v = clampi(value, 0, 32767);
  return (int)((long long)v * (long long)max_px / 32767LL);
}

static uint32_t cursor_pixel(int cx, int cy) {
  if (cx < 0 || cy < 0 || cx >= CURSOR_SIZE || cy >= CURSOR_SIZE) return 0;
  if (cx == 0 || cy == 0 || cx == CURSOR_SIZE - 1 || cy == CURSOR_SIZE - 1) return 0x00000000;
  if ((cx >= CURSOR_SIZE / 2 - 1 && cx <= CURSOR_SIZE / 2) ||
      (cy >= CURSOR_SIZE / 2 - 1 && cy <= CURSOR_SIZE / 2)) {
    return 0x00000000;
  }
  if (cx >= 3 && cx <= CURSOR_SIZE - 4 && cy >= 3 && cy <= CURSOR_SIZE - 4) return 0x0000ff00;
  return 0x00ffffff;
}

static void fb_write_rect_from_buf(int x, int y, int w, int h, const uint32_t *buf, int stride) {
  if (!buf || fbfd < 0 || w <= 0 || h <= 0) return;

  for (int row = 0; row < h; row++) {
    int py = y + row;
    if (py < 0 || py >= screen_h) continue;

    int src_col = 0;
    int px = x;
    int copy_w = w;
    if (px < 0) {
      src_col = -px;
      copy_w += px;
      px = 0;
    }
    if (px + copy_w > screen_w) copy_w = screen_w - px;
    if (copy_w <= 0) continue;

    off_t off = (off_t)(((size_t)py * (size_t)screen_w + (size_t)px) * sizeof(uint32_t));
    if (lseek(fbfd, off, SEEK_SET) < 0) return;
    write(fbfd, buf + (size_t)row * (size_t)stride + (size_t)src_col, (size_t)copy_w * sizeof(uint32_t));
  }
}

static void fb_restore_bg_rect(int x, int y, int w, int h) {
  if (!framebuf || w <= 0 || h <= 0) return;
  for (int row = 0; row < h; row++) {
    int py = y + row;
    if (py < 0 || py >= screen_h) continue;
    fb_write_rect_from_buf(x, py, w, 1, framebuf + (size_t)py * (size_t)screen_w + (size_t)x, screen_w);
  }
}

static void fb_draw_cursor(int x, int y) {
  uint32_t rowbuf[CURSOR_SIZE];
  for (int row = 0; row < CURSOR_SIZE; row++) {
    for (int col = 0; col < CURSOR_SIZE; col++) {
      rowbuf[col] = cursor_pixel(col, row);
    }
    fb_write_rect_from_buf(x, y + row, CURSOR_SIZE, 1, rowbuf, CURSOR_SIZE);
  }
}

static void fill_rect(int x, int y, int w, int h, uint32_t color) {
  if (!framebuf || w <= 0 || h <= 0) return;

  int x0 = x < 0 ? 0 : x;
  int y0 = y < 0 ? 0 : y;
  int x1 = x + w;
  int y1 = y + h;
  if (x1 > screen_w) x1 = screen_w;
  if (y1 > screen_h) y1 = screen_h;
  if (x0 >= x1 || y0 >= y1) return;

  for (int py = y0; py < y1; py++) {
    uint32_t *row = framebuf + py * screen_w;
    for (int px = x0; px < x1; px++) {
      row[px] = color;
    }
  }
}

static void flush_framebuffer(void) {
  if (fbfd < 0 || !framebuf) return;
  size_t bytes = (size_t)screen_w * (size_t)screen_h * sizeof(uint32_t);
  if (lseek(fbfd, 0, SEEK_SET) < 0) return;
  write(fbfd, framebuf, bytes);
}

static int point_in_window(const Window *win, int x, int y) {
  if (!win) return 0;
  return x >= win->x && x < win->x + win->w && y >= win->y && y < win->y + win->h;
}

static void draw_window(const Window *win, int focused, int zindex) {
  if (!win) return;

  const int border = 2;
  const int title_h = 22;

  uint32_t border_color = focused ? 0x00ffd166 : 0x007a7a7a;
  uint32_t title_color = focused ? 0x002f6fdf : 0x004a4a4a;
  uint32_t shade = (zindex == 0) ? 0x00101010 : 0x00000000;

  fill_rect(win->x + 5, win->y + 5, win->w, win->h, shade);
  fill_rect(win->x, win->y, win->w, win->h, border_color);
  fill_rect(win->x + border, win->y + border, win->w - border * 2, title_h, title_color);
  fill_rect(win->x + border, win->y + border + title_h, win->w - border * 2,
            win->h - border * 2 - title_h, win->body_color);

  int deco_w = 10;
  fill_rect(win->x + win->w - 14, win->y + 6, deco_w, deco_w, 0x00d9534f);
  fill_rect(win->x + win->w - 28, win->y + 6, deco_w, deco_w, 0x00f0ad4e);
  fill_rect(win->x + win->w - 42, win->y + 6, deco_w, deco_w, 0x005cb85c);

  uint32_t stripe = focused ? 0x001e4aa8 : 0x00363636;
  for (int i = 0; i < 4; i++) {
    int sw = (win->w - 20) / 4;
    fill_rect(win->x + 10 + i * sw, win->y + 9, sw - 3, 3, stripe);
  }
}

static void compose_scene(Window wins[2], int focused) {
  fill_rect(0, 0, screen_w, screen_h, 0x00222224);

  fill_rect(0, screen_h - 28, screen_w, 28, 0x0018181a);
  fill_rect(8, screen_h - 22, 72, 16, focused == 0 ? 0x004d8bf5 : 0x00444444);
  fill_rect(88, screen_h - 22, 72, 16, focused == 1 ? 0x004d8bf5 : 0x00444444);

  int bg_lines = 12;
  for (int i = 0; i < bg_lines; i++) {
    int y = 16 + i * 24;
    int x = 12 + (i % 2) * 8;
    fill_rect(x, y, screen_w - 24, 2, 0x002c2c30);
  }

  int back = focused ^ 1;
  draw_window(&wins[back], 0, 0);
  draw_window(&wins[focused], 1, 1);
}

static void switch_focus(int *focused) {
  if (!focused) return;
  *focused ^= 1;
}

static int write_full(int fd, const void *buf, size_t len) {
  const char *p = (const char *)buf;
  size_t done = 0;
  while (done < len) {
    int n = write(fd, p + done, len - done);
    if (n <= 0) return -1;
    done += (size_t)n;
  }
  return 0;
}

static int spawn_ipc_client(const char *path) {
  int p_evt[2];
  int p_draw[2];
  if (pipe(p_evt) < 0) return -1;
  if (pipe(p_draw) < 0) {
    close(p_evt[0]);
    close(p_evt[1]);
    return -1;
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(p_evt[0]); close(p_evt[1]);
    close(p_draw[0]); close(p_draw[1]);
    return -1;
  }

  if (pid == 0) {
    dup2(p_evt[0], 3);
    dup2(p_draw[1], 4);
    close(p_evt[0]); close(p_evt[1]);
    close(p_draw[0]); close(p_draw[1]);

    char *argv[] = {(char *)path, NULL};
    char *envp[] = {NULL};
    execve(path, argv, envp);
    _exit(127);
  }

  close(p_evt[0]);
  close(p_draw[1]);
  app_ev_write_fd = p_evt[1];
  app_draw_read_fd = p_draw[0];
  return (int)pid;
}

static void compose_scene_m2(Window wins[2], int focused, const ClientWindow *cw) {
  compose_scene(wins, focused);
  if (!cw || !cw->ready || !appbuf) return;

  for (int row = 0; row < cw->h; row++) {
    int py = cw->y + row;
    if (py < 0 || py >= screen_h) continue;
    int dstx = cw->x;
    int copy_w = cw->w;
    if (dstx < 0) {
      copy_w += dstx;
      dstx = 0;
    }
    if (dstx + copy_w > screen_w) copy_w = screen_w - dstx;
    if (copy_w <= 0) continue;

    uint32_t *dst = framebuf + py * screen_w + dstx;
    uint32_t *src = appbuf + row * cw->w + (dstx - cw->x);
    memcpy(dst, src, (size_t)copy_w * sizeof(uint32_t));
  }
}

static int handle_client_draw_msg(const ClientWindow *cw) {
  if (app_draw_read_fd < 0) return 0;

  int updated = 0;

  while (app_ipc_len < (int)sizeof(app_ipc_buf)) {
    int n = read(app_draw_read_fd, app_ipc_buf + app_ipc_len, sizeof(app_ipc_buf) - app_ipc_len);
    if (n <= 0) break;
    app_ipc_len += n;
  }

  while (app_ipc_len >= (int)sizeof(NDLIPCMsg)) {
    NDLIPCMsg msg;
    memcpy(&msg, app_ipc_buf, sizeof(msg));

    if (msg.magic != NDL_IPC_MAGIC || msg.reserved != NDL_IPC_TAG) {
      printf("[desktop][ipc] bad magic=0x%x type=%u (drop 1 byte)\n", msg.magic, (unsigned)msg.type);
      memmove(app_ipc_buf, app_ipc_buf + 1, (size_t)(app_ipc_len - 1));
      app_ipc_len--;
      continue;
    }

    int payload = 0;
    if (msg.type == NDL_IPC_C2W_DRAW) {
      if (msg.w <= 0 || msg.h <= 0) {
        memmove(app_ipc_buf, app_ipc_buf + sizeof(NDLIPCMsg), (size_t)(app_ipc_len - (int)sizeof(NDLIPCMsg)));
        app_ipc_len -= (int)sizeof(NDLIPCMsg);
        continue;
      }
      payload = msg.w * msg.h * (int)sizeof(uint32_t);
      if (payload < 0 || payload > (int)sizeof(app_ipc_buf)) {
        printf("[desktop][ipc] invalid payload=%d\n", payload);
        memmove(app_ipc_buf, app_ipc_buf + 1, (size_t)(app_ipc_len - 1));
        app_ipc_len--;
        continue;
      }
    }

    int total = (int)sizeof(NDLIPCMsg) + payload;
    if (app_ipc_len < total) break;

    if (msg.type == NDL_IPC_C2W_HELLO) {
      printf("[desktop][ipc] hello from client size=%dx%d\n", msg.w, msg.h);
      updated = 1;
    } else if (msg.type == NDL_IPC_C2W_DRAW && cw && cw->ready && appbuf) {
      const uint32_t *pix = (const uint32_t *)(app_ipc_buf + sizeof(NDLIPCMsg));
      for (int row = 0; row < msg.h; row++) {
        int dy = msg.y + row;
        if (dy < 0 || dy >= cw->h) continue;
        for (int col = 0; col < msg.w; col++) {
          int dx = msg.x + col;
          if (dx < 0 || dx >= cw->w) continue;
          appbuf[dy * cw->w + dx] = pix[row * msg.w + col];
        }
      }
      updated = 1;
    }

    if (app_ipc_len > total) {
      memmove(app_ipc_buf, app_ipc_buf + total, (size_t)(app_ipc_len - total));
    }
    app_ipc_len -= total;
  }

  return updated;
}

static void send_key_event_to_client(const char *ev) {
  if (app_ev_write_fd < 0 || !ev) return;
  int len = (int)strlen(ev);
  if (len <= 0) return;

  NDLIPCMsg msg;
  memset(&msg, 0, sizeof(msg));
  msg.magic = NDL_IPC_MAGIC;
  msg.reserved = NDL_IPC_TAG;
  msg.type = NDL_IPC_W2C_EVENT;
  msg.w = len;
  if (write_full(app_ev_write_fd, &msg, sizeof(msg)) < 0) return;
  write_full(app_ev_write_fd, ev, (size_t)len);
}

int main(void) {
  int infofd = open("/device/dispinfo", O_RDONLY, 0);
  if (infofd >= 0) {
    char buf[64] = {0};
    int n = read(infofd, buf, sizeof(buf) - 1);
    if (n > 0) {
      buf[n] = '\0';
      int w = 0, h = 0;
      if (sscanf(buf, "WIDTH:%d\nHEIGHT:%d\n", &w, &h) == 2 && w > 0 && h > 0) {
        screen_w = w;
        screen_h = h;
      }
    }
    close(infofd);
  }

  fbfd = open("/device/fb", O_WRONLY, 0);
  evfd = open("/device/events", O_RDONLY, 0);
  if (fbfd < 0 || evfd < 0) {
    printf("[desktop] open device failed fb=%d events=%d\n", fbfd, evfd);
    return 1;
  }

  framebuf = (uint32_t *)malloc((size_t)screen_w * (size_t)screen_h * sizeof(uint32_t));
  if (!framebuf) {
    printf("[desktop] alloc framebuffer failed\n");
    return 1;
  }

  Window windows[2];
  windows[0].x = screen_w / 10;
  windows[0].y = screen_h / 8;
  windows[0].w = screen_w * 45 / 100;
  windows[0].h = screen_h * 55 / 100;
  windows[0].body_color = 0x00304060;

  windows[1].x = screen_w * 35 / 100;
  windows[1].y = screen_h * 22 / 100;
  windows[1].w = screen_w * 45 / 100;
  windows[1].h = screen_h * 55 / 100;
  windows[1].body_color = 0x00604030;

  ClientWindow client;
  client.x = windows[1].x + 8;
  client.y = windows[1].y + 30;
  client.w = windows[1].w - 16;
  client.h = windows[1].h - 38;
  client.ready = 1;
  if (client.w <= 32) client.w = 32;
  if (client.h <= 32) client.h = 32;

  appbuf = (uint32_t *)malloc((size_t)client.w * (size_t)client.h * sizeof(uint32_t));
  if (!appbuf) return 1;
  for (int i = 0; i < client.w * client.h; i++) appbuf[i] = 0x00101010;

  int child = spawn_ipc_client("/bin/sdl-ipc-smoke");
  if (child < 0) {
    printf("[desktop] spawn ipc client failed\n");
  } else {
    printf("[desktop] spawned sdl-ipc-smoke pid=%d evfd=%d drawfd=%d\n", child, app_ev_write_fd, app_draw_read_fd);
  }

  mouse_x = screen_w / 2;
  mouse_y = screen_h / 2;
  mouse_x = clampi(mouse_x, 0, screen_w - CURSOR_SIZE);
  mouse_y = clampi(mouse_y, 0, screen_h - CURSOR_SIZE);
  int prev_mouse_x = mouse_x;
  int prev_mouse_y = mouse_y;

  int focused = 1;
  compose_scene_m2(windows, focused, &client);
  flush_framebuffer();
  fb_draw_cursor(mouse_x, mouse_y);

  printf("[desktop] M2 ready: IPC SDL client + keyboard forwarding\n");

  char evbuf[64];
  while (1) {
    int client_updated = handle_client_draw_msg(&client);

    int n = read(evfd, evbuf, sizeof(evbuf) - 1);
    if (n > 0) {
      evbuf[n] = '\0';

      int key = 0;
      if (sscanf(evbuf, "kd %d", &key) == 1) {
        int need_full_present = 0;
        if (key == KEY_TAB) {
          switch_focus(&focused);
          need_full_present = 1;
        }
        if (key == BTN_LEFT) {
          if (point_in_window(&windows[0], mouse_x, mouse_y)) focused = 0;
          if (point_in_window(&windows[1], mouse_x, mouse_y)) focused = 1;
          need_full_present = 1;
        }
        if (focused == 1) {
          send_key_event_to_client(evbuf);
        }
        if (need_full_present) {
          compose_scene_m2(windows, focused, &client);
          flush_framebuffer();
          fb_draw_cursor(mouse_x, mouse_y);
        }
        continue;
      }

      int type = 0;
      int code = 0;
      int value = 0;
      if (sscanf(evbuf, "m %d %d %d", &type, &code, &value) == 3) {
        if (type == 0 && code == 0 && value == 0) {
          continue;
        }

        int old_x = mouse_x;
        int old_y = mouse_y;
        int moved = 0;
        int need_full_present = 0;

        if (type == 2) {
          if (code == 0) mouse_x += value;
          if (code == 1) mouse_y += value;
          moved = 1;
        } else if (type == 3) {
          if (code == 0) mouse_x = abs_to_screen(value, screen_w - CURSOR_SIZE);
          if (code == 1) mouse_y = abs_to_screen(value, screen_h - CURSOR_SIZE);
          moved = 1;
        }

        mouse_x = clampi(mouse_x, 0, screen_w - CURSOR_SIZE);
        mouse_y = clampi(mouse_y, 0, screen_h - CURSOR_SIZE);
        if (mouse_x != old_x || mouse_y != old_y) moved = 1;

        if (type == 1 && value == 1) {
          if (point_in_window(&windows[0], mouse_x, mouse_y)) focused = 0;
          if (point_in_window(&windows[1], mouse_x, mouse_y)) focused = 1;
          need_full_present = 1;
        }

        if (need_full_present) {
          compose_scene_m2(windows, focused, &client);
          flush_framebuffer();
          fb_draw_cursor(mouse_x, mouse_y);
          prev_mouse_x = mouse_x;
          prev_mouse_y = mouse_y;
        } else if (moved) {
          fb_restore_bg_rect(prev_mouse_x, prev_mouse_y, CURSOR_SIZE, CURSOR_SIZE);
          fb_draw_cursor(mouse_x, mouse_y);
          prev_mouse_x = mouse_x;
          prev_mouse_y = mouse_y;
        }
        continue;
      }

      if (sscanf(evbuf, "ku %d", &key) == 1) {
        if (focused == 1) {
          send_key_event_to_client(evbuf);
        }
        continue;
      }
    }

    if (client_updated) {
      compose_scene_m2(windows, focused, &client);
      flush_framebuffer();
      fb_draw_cursor(mouse_x, mouse_y);
    }
  }

  return 0;
}
