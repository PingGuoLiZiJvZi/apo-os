#include <BMP.h>
#include <NDL.h>
#include <am.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define KEY_ESC 1
#define KEY_ENTER 28
#define KEY_UP 103
#define KEY_LEFT 105
#define KEY_RIGHT 106
#define KEY_DOWN 108

#define ICON_COLS 2
#define ICON_ROWS 2
#define ICON_COUNT (ICON_COLS * ICON_ROWS)

typedef struct {
  const char *title;
  const char *exec_path;
  const char *arg1;
} AppEntry;

typedef struct {
  char bg_path[256];
  char icon_paths[ICON_COUNT][256];
} MenuConfig;

typedef struct {
  uint32_t *pixels;
  int w;
  int h;
  int valid;
} Image;

static const AppEntry k_apps[ICON_COUNT] = {
    {"PAL", "/bin/pal", NULL},
    {"Flappy Bird", "/bin/bird", NULL},
  {"Super Mario", "/bin/fceux", "/share/games/nes/mario.nes"},
  {"Snake", "/bin/snake", NULL},
};

static int screen_w = 640;
static int screen_h = 480;
static uint32_t *canvas = NULL;

static int clampi(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static uint16_t rd16(const uint8_t *p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static int validate_bmp_header(const char *path, int *out_w, int *out_h) {
  int fd = open(path, O_RDONLY, 0);
  if (fd < 0) return 0;

  uint8_t hdr[54];
  int n = read(fd, hdr, sizeof(hdr));
  close(fd);
  if (n != (int)sizeof(hdr)) return 0;

  if (hdr[0] != 'B' || hdr[1] != 'M') return 0;
  if (rd32(hdr + 14) < 40) return 0;

  uint32_t width = rd32(hdr + 18);
  uint32_t height = rd32(hdr + 22);
  uint16_t planes = rd16(hdr + 26);
  uint16_t bpp = rd16(hdr + 28);
  uint32_t compression = rd32(hdr + 30);

  if (planes != 1) return 0;
  if (bpp != 24 && bpp != 32) return 0;
  if (compression != 0) return 0;
  if (width == 0 || height == 0) return 0;
  if (width > 4096 || height > 4096) return 0;

  if (out_w) *out_w = (int)width;
  if (out_h) *out_h = (int)height;
  return 1;
}

static void image_free(Image *img) {
  if (!img) return;
  if (img->pixels) {
    free(img->pixels);
    img->pixels = NULL;
  }
  img->w = 0;
  img->h = 0;
  img->valid = 0;
}

static int image_load_checked(Image *img, const char *path) {
  if (!img || !path || path[0] == '\0') return 0;
  int w = 0, h = 0;
  if (!validate_bmp_header(path, &w, &h)) {
    printf("[menu] invalid bmp format: %s\n", path);
    return 0;
  }
  void *p = BMP_Load(path, &w, &h);
  if (!p || w <= 0 || h <= 0) {
    printf("[menu] bmp load failed: %s\n", path);
    return 0;
  }
  img->pixels = (uint32_t *)p;
  img->w = w;
  img->h = h;
  img->valid = 1;
  return 1;
}

static void blit_scaled(uint32_t *dst, int dw, int dh, const Image *src, int dx, int dy,
                        int rw, int rh) {
  if (!dst || !src || !src->valid) return;
  if (rw <= 0 || rh <= 0) return;

  for (int y = 0; y < rh; y++) {
    int py = dy + y;
    if (py < 0 || py >= dh) continue;
    int sy = (int)((long long)y * src->h / rh);
    if (sy < 0) sy = 0;
    if (sy >= src->h) sy = src->h - 1;

    for (int x = 0; x < rw; x++) {
      int px = dx + x;
      if (px < 0 || px >= dw) continue;
      int sx = (int)((long long)x * src->w / rw);
      if (sx < 0) sx = 0;
      if (sx >= src->w) sx = src->w - 1;
      dst[py * dw + px] = src->pixels[sy * src->w + sx];
    }
  }
}

static void fill_rect(uint32_t *dst, int x, int y, int w, int h, uint32_t c) {
  if (!dst || w <= 0 || h <= 0) return;
  int x0 = clampi(x, 0, screen_w);
  int y0 = clampi(y, 0, screen_h);
  int x1 = clampi(x + w, 0, screen_w);
  int y1 = clampi(y + h, 0, screen_h);
  for (int py = y0; py < y1; py++) {
    for (int px = x0; px < x1; px++) {
      dst[py * screen_w + px] = c;
    }
  }
}

static int parse_line(char *line, char **key, char **val) {
  while (*line == ' ' || *line == '\t') line++;
  if (*line == '#' || *line == '\0' || *line == '\n' || *line == '\r') return 0;
  char *eq = strchr(line, '=');
  if (!eq) return 0;
  *eq = '\0';
  char *k = line;
  char *v = eq + 1;

  char *kend = k + strlen(k);
  while (kend > k && (kend[-1] == ' ' || kend[-1] == '\t')) *--kend = '\0';
  while (*v == ' ' || *v == '\t') v++;
  char *vend = v + strlen(v);
  while (vend > v && (vend[-1] == '\n' || vend[-1] == '\r' || vend[-1] == ' ' || vend[-1] == '\t')) {
    *--vend = '\0';
  }
  *key = k;
  *val = v;
  return 1;
}

static void load_config(MenuConfig *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  strncpy(cfg->bg_path, "/share/pictures/projectn.bmp", sizeof(cfg->bg_path) - 1);
  for (int i = 0; i < ICON_COUNT; i++) {
    strncpy(cfg->icon_paths[i], "/share/pictures/projectn.bmp", sizeof(cfg->icon_paths[i]) - 1);
  }

  int fd = open("/share/menu/menu.conf", O_RDONLY, 0);
  if (fd < 0) {
    printf("[menu] /share/menu/menu.conf not found, using defaults\n");
    return;
  }

  char buf[4096];
  int n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) return;
  buf[n] = '\0';

  char *save = NULL;
  char *line = strtok_r(buf, "\n", &save);
  while (line) {
    char *k = NULL;
    char *v = NULL;
    if (parse_line(line, &k, &v)) {
      if (strcmp(k, "background") == 0) {
        strncpy(cfg->bg_path, v, sizeof(cfg->bg_path) - 1);
      } else if (strncmp(k, "icon", 4) == 0) {
        int id = k[4] - '0';
        if (id >= 0 && id < ICON_COUNT) {
          strncpy(cfg->icon_paths[id], v, sizeof(cfg->icon_paths[id]) - 1);
        }
      }
    }
    line = strtok_r(NULL, "\n", &save);
  }
}

static int read_dispinfo(int *w, int *h) {
  int fd = open("/device/dispinfo", O_RDONLY, 0);
  if (fd < 0) return -1;
  char buf[64] = {0};
  int n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0) return -1;
  buf[n] = '\0';
  int sw = 0, sh = 0;
  if (sscanf(buf, "WIDTH:%d\nHEIGHT:%d\n", &sw, &sh) != 2) return -1;
  if (sw <= 0 || sh <= 0) return -1;
  *w = sw;
  *h = sh;
  return 0;
}

static void draw_menu_base(const Image *bg) {
  if (bg && bg->valid) {
    blit_scaled(canvas, screen_w, screen_h, bg, 0, 0, screen_w, screen_h);
  } else {
    fill_rect(canvas, 0, 0, screen_w, screen_h, 0x001e1e24);
  }
}

static void draw_icon_tile(int x, int y, int w, int h, const Image *icon, int focused) {
  uint32_t border = focused ? 0x00ffd166 : 0x00404040;
  uint32_t body = focused ? 0x002f2f3a : 0x00262630;
  fill_rect(canvas, x, y, w, h, border);
  fill_rect(canvas, x + 2, y + 2, w - 4, h - 4, body);
  if (icon && icon->valid) {
    blit_scaled(canvas, screen_w, screen_h, icon, x + 6, y + 6, w - 12, h - 12);
  }
}

static void draw_menu_scene(const Image *bg, Image icons[ICON_COUNT], int selected, uint32_t *dst) {
  uint32_t *old_canvas = canvas;
  canvas = dst;
  draw_menu_base(bg);

  int tile_w = 180;
  int tile_h = 120;
  int gap_x = 36;
  int gap_y = 26;

  int total_w = ICON_COLS * tile_w + (ICON_COLS - 1) * gap_x;
  int total_h = ICON_ROWS * tile_h + (ICON_ROWS - 1) * gap_y;
  int base_x = (screen_w - total_w) / 2;
  int base_y = (screen_h - total_h) / 2;

  for (int i = 0; i < ICON_COUNT; i++) {
    int row = i / ICON_COLS;
    int col = i % ICON_COLS;
    int x = base_x + col * (tile_w + gap_x);
    int y = base_y + row * (tile_h + gap_y);
    draw_icon_tile(x, y, tile_w, tile_h, &icons[i], i == selected);
  }

  canvas = old_canvas;
}

static void present_scene(const uint32_t *scene) {
  if (!scene || !canvas) return;
  memcpy(canvas, scene, (size_t)screen_w * (size_t)screen_h * sizeof(uint32_t));
  NDL_DrawRect(canvas, 0, 0, screen_w, screen_h);
}

static int launch_game(const AppEntry *entry) {
  pid_t pid = fork();
  if (pid < 0) {
    printf("[menu] fork failed for %s\n", entry->title);
    return -1;
  }

  if (pid == 0) {
    if (entry->arg1) {
      char *argv[] = {(char *)entry->exec_path, (char *)entry->arg1, NULL};
      char *envp[] = {NULL};
      execve(entry->exec_path, argv, envp);
    } else {
      char *argv[] = {(char *)entry->exec_path, NULL};
      char *envp[] = {NULL};
      execve(entry->exec_path, argv, envp);
    }
    _exit(127);
  }

  printf("[menu] launched %s pid=%d\n", entry->title, (int)pid);
  return (int)pid;
}

static void daemon_watch_child(int child_pid) {
  int evfd = open("/device/events", O_RDONLY, 0);
  int killed = 0;
  while (1) {
    int status = 0;
    int wr = waitpid(child_pid, &status, 1);
    if (wr == child_pid) {
      printf("[menu] child pid=%d exit status=%d\n", child_pid, status);
      break;
    }

    if (evfd >= 0) {
      char evbuf[64];
      int n = read(evfd, evbuf, sizeof(evbuf) - 1);
      printf("[menu] read event: %.*s\n", n, evbuf);
      if (n > 0) {
        evbuf[n] = '\0';
        int code = 0;
        if (sscanf(evbuf, "kd %d", &code) == 1 && code == KEY_ESC) {
          kill(child_pid, 9);
          killed = 1;
          printf("[menu] ESC detected -> kill pid=%d\n", child_pid);
        }
      }
    }

    if (!killed) {
      yield();
    }
  }

  if (evfd >= 0) close(evfd);
  if (killed) {
    printf("[menu] forced return to menu after kill\n");
  }
}

int main(int argc, char *argv[]) {
  (void)argc;
  (void)argv;

  if (read_dispinfo(&screen_w, &screen_h) < 0) {
    screen_w = 640;
    screen_h = 480;
  }

  canvas = (uint32_t *)malloc((size_t)screen_w * (size_t)screen_h * sizeof(uint32_t));
  uint32_t *scene = (uint32_t *)malloc((size_t)screen_w * (size_t)screen_h * sizeof(uint32_t));
  if (!canvas || !scene) {
    printf("[menu] alloc framebuffer failed\n");
    return 1;
  }

  MenuConfig cfg;
  load_config(&cfg);

  Image bg = {0};
  Image icons[ICON_COUNT];
  memset(icons, 0, sizeof(icons));

  image_load_checked(&bg, cfg.bg_path);
  for (int i = 0; i < ICON_COUNT; i++) {
    image_load_checked(&icons[i], cfg.icon_paths[i]);
  }

  NDL_Init(0);
  int cw = screen_w, ch = screen_h;
  NDL_OpenCanvas(&cw, &ch);

  int running = 1;
  int dirty = 1;
  int selected = 0;

  printf("[menu] ready, arrows switch / Enter launch / Esc kill child\n");

  while (running) {
    char ev[64];
    int n = 0;
    int got_event = 0;
    while ((n = NDL_PollEvent(ev, sizeof(ev))) > 0) {
      got_event = 1;
      int key = 0;

      if (sscanf(ev, "kd %d", &key) == 1) {
        int row = selected / ICON_COLS;
        int col = selected % ICON_COLS;
        if (key == KEY_UP) {
          row = (row + ICON_ROWS - 1) % ICON_ROWS;
          selected = row * ICON_COLS + col;
          dirty = 1;
        } else if (key == KEY_DOWN) {
          row = (row + 1) % ICON_ROWS;
          selected = row * ICON_COLS + col;
          dirty = 1;
        } else if (key == KEY_LEFT) {
          col = (col + ICON_COLS - 1) % ICON_COLS;
          selected = row * ICON_COLS + col;
          dirty = 1;
        } else if (key == KEY_RIGHT) {
          col = (col + 1) % ICON_COLS;
          selected = row * ICON_COLS + col;
          dirty = 1;
        } else if (key == KEY_ENTER) {
          NDL_Quit();
          int pid = launch_game(&k_apps[selected]);
          if (pid > 0) {
            daemon_watch_child(pid);
          }
          NDL_Init(0);
          cw = screen_w;
          ch = screen_h;
          NDL_OpenCanvas(&cw, &ch);
          dirty = 1;
        }
        continue;
      }
    }

    if (dirty) {
      draw_menu_scene(&bg, icons, selected, scene);
      present_scene(scene);
      dirty = 0;
    }

    if (!got_event) {
      yield();
    }
  }

  NDL_Quit();
  image_free(&bg);
  for (int i = 0; i < ICON_COUNT; i++) {
    image_free(&icons[i]);
  }
  free(scene);
  free(canvas);
  return 0;
}
