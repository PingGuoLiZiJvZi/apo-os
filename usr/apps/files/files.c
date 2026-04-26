#include <am.h>
#include <fcntl.h>
#include <klib-macros.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define VIEW_W 480
#define VIEW_H 320
#define MAX_PATH_LEN 256
#define MAX_ENTRIES 256
#define DIRSIZ 28
#define HEADER_H 28
#define LINE_H 10
#define CHAR_W 6
#define TEXT_X 8

typedef struct {
  uint32_t inum;
  char name[DIRSIZ];
} DiskDirEntry;

typedef struct {
  char name[DIRSIZ + 1];
  char path[MAX_PATH_LEN];
  int is_dir;
  long size;
} FileEntry;

static uint32_t *fb;
static int fb_w = VIEW_W;
static int fb_h = VIEW_H;
static FileEntry entries[MAX_ENTRIES];
static int entry_count;
static int selected;
static int list_top;
static char current_path[MAX_PATH_LEN] = "/";
static char preview_path[MAX_PATH_LEN];
static long preview_size;
static int preview_line;
static int preview_mode;

static const uint8_t font5x7[128][7] = {
  [' '] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  ['!'] = {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
  ['"'] = {0x0A,0x0A,0x0A,0x00,0x00,0x00,0x00},
  ['#'] = {0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A},
  ['$'] = {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04},
  ['%'] = {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
  ['&'] = {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D},
  ['\''] = {0x04,0x04,0x04,0x00,0x00,0x00,0x00},
  ['('] = {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
  [')'] = {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
  ['*'] = {0x00,0x04,0x15,0x0E,0x15,0x04,0x00},
  ['+'] = {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
  [','] = {0x00,0x00,0x00,0x00,0x00,0x04,0x08},
  ['-'] = {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
  ['.'] = {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
  ['/'] = {0x01,0x02,0x02,0x04,0x08,0x08,0x10},
  ['0'] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
  ['1'] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
  ['2'] = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
  ['3'] = {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
  ['4'] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
  ['5'] = {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},
  ['6'] = {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
  ['7'] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
  ['8'] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
  ['9'] = {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
  [':'] = {0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00},
  [';'] = {0x00,0x0C,0x0C,0x00,0x0C,0x04,0x08},
  ['<'] = {0x02,0x04,0x08,0x10,0x08,0x04,0x02},
  ['='] = {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
  ['>'] = {0x08,0x04,0x02,0x01,0x02,0x04,0x08},
  ['?'] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
  ['@'] = {0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E},
  ['A'] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
  ['B'] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
  ['C'] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  ['D'] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
  ['E'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
  ['F'] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
  ['G'] = {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
  ['H'] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
  ['I'] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F},
  ['J'] = {0x01,0x01,0x01,0x01,0x11,0x11,0x0E},
  ['K'] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
  ['L'] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
  ['M'] = {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
  ['N'] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
  ['O'] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
  ['P'] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
  ['Q'] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
  ['R'] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
  ['S'] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},
  ['T'] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
  ['U'] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
  ['V'] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
  ['W'] = {0x11,0x11,0x11,0x15,0x15,0x15,0x0A},
  ['X'] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
  ['Y'] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
  ['Z'] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
  ['['] = {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E},
  ['\\'] = {0x10,0x08,0x08,0x04,0x02,0x02,0x01},
  [']'] = {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E},
  ['^'] = {0x04,0x0A,0x11,0x00,0x00,0x00,0x00},
  ['_'] = {0x00,0x00,0x00,0x00,0x00,0x00,0x1F},
  ['`'] = {0x08,0x04,0x02,0x00,0x00,0x00,0x00},
  ['a'] = {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F},
  ['b'] = {0x10,0x10,0x1E,0x11,0x11,0x11,0x1E},
  ['c'] = {0x00,0x00,0x0E,0x10,0x10,0x10,0x0E},
  ['d'] = {0x01,0x01,0x0F,0x11,0x11,0x11,0x0F},
  ['e'] = {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E},
  ['f'] = {0x06,0x08,0x08,0x1E,0x08,0x08,0x08},
  ['g'] = {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E},
  ['h'] = {0x10,0x10,0x1E,0x11,0x11,0x11,0x11},
  ['i'] = {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E},
  ['j'] = {0x02,0x00,0x06,0x02,0x02,0x12,0x0C},
  ['k'] = {0x10,0x10,0x12,0x14,0x18,0x14,0x12},
  ['l'] = {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E},
  ['m'] = {0x00,0x00,0x1A,0x15,0x15,0x15,0x15},
  ['n'] = {0x00,0x00,0x1E,0x11,0x11,0x11,0x11},
  ['o'] = {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E},
  ['p'] = {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10},
  ['q'] = {0x00,0x00,0x0F,0x11,0x0F,0x01,0x01},
  ['r'] = {0x00,0x00,0x16,0x18,0x10,0x10,0x10},
  ['s'] = {0x00,0x00,0x0F,0x10,0x0E,0x01,0x1E},
  ['t'] = {0x08,0x08,0x1E,0x08,0x08,0x08,0x06},
  ['u'] = {0x00,0x00,0x11,0x11,0x11,0x13,0x0D},
  ['v'] = {0x00,0x00,0x11,0x11,0x11,0x0A,0x04},
  ['w'] = {0x00,0x00,0x11,0x15,0x15,0x15,0x0A},
  ['x'] = {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11},
  ['y'] = {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E},
  ['z'] = {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F},
  ['{'] = {0x02,0x04,0x04,0x08,0x04,0x04,0x02},
  ['|'] = {0x04,0x04,0x04,0x04,0x04,0x04,0x04},
  ['}'] = {0x08,0x04,0x04,0x02,0x04,0x04,0x08},
  ['~'] = {0x00,0x00,0x08,0x15,0x02,0x00,0x00},
};

static int mini(int a, int b) {
  return a < b ? a : b;
}

static int maxi(int a, int b) {
  return a > b ? a : b;
}

static void copy_str(char *dst, int dst_size, const char *src) {
  int i = 0;
  if (!dst || dst_size <= 0) return;
  if (!src) src = "";
  for (; i < dst_size - 1 && src[i]; i++) dst[i] = src[i];
  dst[i] = '\0';
}

static void clear_screen(uint32_t color) {
  for (int i = 0; i < fb_w * fb_h; i++) fb[i] = color;
}

static void fill_rect(int x, int y, int w, int h, uint32_t color) {
  int x1 = maxi(x, 0);
  int y1 = maxi(y, 0);
  int x2 = mini(x + w, fb_w);
  int y2 = mini(y + h, fb_h);
  for (int yy = y1; yy < y2; yy++) {
    uint32_t *row = fb + yy * fb_w;
    for (int xx = x1; xx < x2; xx++) row[xx] = color;
  }
}

static char printable_char(unsigned char ch) {
  if (ch >= 32 && ch <= 126) return (char)ch;
  return '.';
}

static void draw_char(int x, int y, char ch, uint32_t color) {
  unsigned char idx = (unsigned char)ch;
  const uint8_t *g = font5x7[idx];
  if (!g[0] && !g[1] && !g[2] && !g[3] && !g[4] && !g[5] && !g[6] && ch != ' ') {
    g = font5x7[(int)'?'];
  }
  for (int row = 0; row < 7; row++) {
    uint8_t bits = g[row];
    for (int col = 0; col < 5; col++) {
      if (bits & (0x10 >> col)) {
        int px = x + col;
        int py = y + row;
        if (px >= 0 && px < fb_w && py >= 0 && py < fb_h) {
          fb[py * fb_w + px] = color;
        }
      }
    }
  }
}

static void draw_text_limit(int x, int y, const char *s, uint32_t color, int max_chars) {
  if (!s || max_chars <= 0) return;
  for (int i = 0; s[i] && i < max_chars; i++) {
    draw_char(x + i * CHAR_W, y, printable_char((unsigned char)s[i]), color);
  }
}

static void draw_text(int x, int y, const char *s, uint32_t color) {
  draw_text_limit(x, y, s, color, (fb_w - x) / CHAR_W);
}

static void present(void) {
  io_write(AM_GPU_FBDRAW, 0, 0, fb, fb_w, fb_h, true);
}

static int is_root_path(const char *path) {
  return strcmp(path, "/") == 0;
}

static void parent_path_of(const char *path, char *out, int out_size) {
  char tmp[MAX_PATH_LEN];
  int len;

  if (is_root_path(path)) {
    copy_str(out, out_size, "/");
    return;
  }

  copy_str(tmp, sizeof(tmp), path);
  len = (int)strlen(tmp);
  while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';
  while (len > 1 && tmp[len - 1] != '/') len--;
  if (len <= 1) {
    copy_str(out, out_size, "/");
    return;
  }
  tmp[len - 1] = '\0';
  copy_str(out, out_size, tmp);
}

static void join_path(const char *dir, const char *name, char *out, int out_size) {
  if (is_root_path(dir)) {
    snprintf(out, out_size, "/%s", name);
  } else {
    snprintf(out, out_size, "%s/%s", dir, name);
  }
}

static int entry_less(const FileEntry *a, const FileEntry *b) {
  if (a->is_dir != b->is_dir) return a->is_dir > b->is_dir;
  return strcmp(a->name, b->name) < 0;
}

static void sort_entries(int start) {
  for (int i = start + 1; i < entry_count; i++) {
    FileEntry tmp = entries[i];
    int j = i - 1;
    while (j >= start && entry_less(&tmp, &entries[j])) {
      entries[j + 1] = entries[j];
      j--;
    }
    entries[j + 1] = tmp;
  }
}

static int visible_list_rows(void) {
  return (fb_h - HEADER_H - 8) / LINE_H;
}

static void keep_selected_visible(void) {
  int rows = visible_list_rows();
  if (rows < 1) rows = 1;
  if (selected < 0) selected = 0;
  if (selected >= entry_count) selected = entry_count - 1;
  if (selected < 0) selected = 0;
  if (selected < list_top) list_top = selected;
  if (selected >= list_top + rows) list_top = selected - rows + 1;
  if (list_top < 0) list_top = 0;
}

static int load_dir(const char *path) {
  char dir_path[MAX_PATH_LEN];
  int fd;
  DiskDirEntry de;
  int sort_start = 0;

  copy_str(dir_path, sizeof(dir_path), path);

  fd = open(dir_path, O_RDONLY, 0);
  if (fd < 0) return -1;

  entry_count = 0;
  if (!is_root_path(dir_path)) {
    parent_path_of(dir_path, entries[0].path, sizeof(entries[0].path));
    copy_str(entries[0].name, sizeof(entries[0].name), "..");
    entries[0].is_dir = 1;
    entries[0].size = 0;
    entry_count = 1;
    sort_start = 1;
  }

  while (entry_count < MAX_ENTRIES &&
         read(fd, &de, sizeof(de)) == (int)sizeof(de)) {
    if (de.inum == 0) continue;

    char name[DIRSIZ + 1];
    struct stat st;
    memcpy(name, de.name, DIRSIZ);
    name[DIRSIZ] = '\0';
    if (name[0] == '\0') continue;

    FileEntry *e = &entries[entry_count];
    copy_str(e->name, sizeof(e->name), name);
    join_path(dir_path, name, e->path, sizeof(e->path));
    if (stat(e->path, &st) < 0) continue;
    e->is_dir = S_ISDIR(st.st_mode);
    e->size = (long)st.st_size;
    entry_count++;
  }
  close(fd);

  sort_entries(sort_start);
  copy_str(current_path, sizeof(current_path), dir_path);
  selected = entry_count > 0 ? 0 : -1;
  list_top = 0;
  keep_selected_visible();
  return 0;
}

static void draw_header(const char *title, const char *path) {
  char line[MAX_PATH_LEN + 32];
  fill_rect(0, 0, fb_w, HEADER_H, 0x0024262b);
  fill_rect(0, HEADER_H - 2, fb_w, 2, 0x0062b080);
  snprintf(line, sizeof(line), "%s  %s", title, path ? path : "");
  draw_text_limit(TEXT_X, 9, line, 0x00f0f0e8, (fb_w - TEXT_X * 2) / CHAR_W);
}

static void draw_list(void) {
  int rows = visible_list_rows();
  clear_screen(0x0017191c);
  draw_header("FILES", current_path);

  if (entry_count == 0) {
    draw_text(TEXT_X, HEADER_H + 12, "empty", 0x00909090);
    return;
  }

  keep_selected_visible();
  for (int row = 0; row < rows; row++) {
    int idx = list_top + row;
    if (idx >= entry_count) break;
    int y = HEADER_H + 4 + row * LINE_H;
    FileEntry *e = &entries[idx];
    char text[96];

    if (idx == selected) {
      fill_rect(4, y - 1, fb_w - 8, LINE_H, 0x00314640);
    }

    if (e->is_dir) {
      snprintf(text, sizeof(text), "DIR  %s%s", e->name, strcmp(e->name, "..") == 0 ? "" : "/");
      draw_text_limit(TEXT_X, y, text, idx == selected ? 0x00ffffff : 0x00cce8b0,
                      (fb_w - TEXT_X * 2) / CHAR_W);
    } else {
      snprintf(text, sizeof(text), "FILE %-28s %ld", e->name, e->size);
      draw_text_limit(TEXT_X, y, text, idx == selected ? 0x00ffffff : 0x00d8d8d8,
                      (fb_w - TEXT_X * 2) / CHAR_W);
    }
  }
}

static int preview_rows(void) {
  return (fb_h - HEADER_H - 8) / LINE_H;
}

static void preview_emit_char(char ch, int *line, int *col, int start_line, int rows) {
  int max_cols = (fb_w - TEXT_X * 2) / CHAR_W;
  if (max_cols < 1) max_cols = 1;

  if (*line >= start_line && *line < start_line + rows) {
    int y = HEADER_H + 4 + (*line - start_line) * LINE_H;
    draw_char(TEXT_X + (*col) * CHAR_W, y, ch, 0x00e4e4dd);
  }

  (*col)++;
  if (*col >= max_cols) {
    *col = 0;
    (*line)++;
  }
}

static void draw_preview(void) {
  char hdr[MAX_PATH_LEN + 64];
  char buf[256];
  int rows = preview_rows();
  int fd;
  int line = 0;
  int col = 0;

  clear_screen(0x0017191c);
  snprintf(hdr, sizeof(hdr), "FILE  %s  %ld", preview_path, preview_size);
  draw_header("VIEW", hdr);

  fd = open(preview_path, O_RDONLY, 0);
  if (fd < 0) {
    draw_text(TEXT_X, HEADER_H + 12, "open failed", 0x00ff9090);
    return;
  }

  while (line < preview_line + rows) {
    int n = read(fd, buf, sizeof(buf));
    if (n <= 0) break;

    for (int i = 0; i < n && line < preview_line + rows; i++) {
      unsigned char ch = (unsigned char)buf[i];
      if (ch == '\n') {
        col = 0;
        line++;
      } else if (ch == '\r') {
        continue;
      } else if (ch == '\t') {
        int spaces = 4 - (col % 4);
        for (int s = 0; s < spaces && line < preview_line + rows; s++) {
          preview_emit_char(' ', &line, &col, preview_line, rows);
        }
      } else {
        preview_emit_char(printable_char(ch), &line, &col, preview_line, rows);
      }
    }
  }

  close(fd);
}

static void open_selected(void) {
  if (selected < 0 || selected >= entry_count) return;
  FileEntry *e = &entries[selected];
  if (e->is_dir) {
    char target[MAX_PATH_LEN];
    copy_str(target, sizeof(target), e->path);
    load_dir(target);
    return;
  }
  copy_str(preview_path, sizeof(preview_path), e->path);
  preview_size = e->size;
  preview_line = 0;
  preview_mode = 1;
}

static void go_parent(void) {
  char parent[MAX_PATH_LEN];
  if (is_root_path(current_path)) return;
  parent_path_of(current_path, parent, sizeof(parent));
  load_dir(parent);
}

static int handle_list_key(int key) {
  int rows = visible_list_rows();
  if (key == AM_KEY_ESCAPE) return -1;
  if (key == AM_KEY_BACKSPACE) {
    go_parent();
    return 1;
  }
  if (entry_count <= 0) return 0;

  if (key == AM_KEY_UP) selected--;
  else if (key == AM_KEY_DOWN) selected++;
  else if (key == AM_KEY_PAGEUP) selected -= rows;
  else if (key == AM_KEY_PAGEDOWN) selected += rows;
  else if (key == AM_KEY_HOME) selected = 0;
  else if (key == AM_KEY_END) selected = entry_count - 1;
  else if (key == AM_KEY_RETURN) open_selected();
  else return 0;

  keep_selected_visible();
  return 1;
}

static int handle_preview_key(int key) {
  int rows = preview_rows();
  if (key == AM_KEY_ESCAPE || key == AM_KEY_BACKSPACE) {
    preview_mode = 0;
    return 1;
  }
  if (key == AM_KEY_UP && preview_line > 0) preview_line--;
  else if (key == AM_KEY_DOWN) preview_line++;
  else if (key == AM_KEY_PAGEUP) preview_line = maxi(0, preview_line - rows);
  else if (key == AM_KEY_PAGEDOWN) preview_line += rows;
  else if (key == AM_KEY_HOME) preview_line = 0;
  else return 0;
  return 1;
}

int main(void) {
  ioe_init();
  AM_GPU_CONFIG_T gpu = io_read(AM_GPU_CONFIG);
  if (gpu.present) {
    fb_w = mini(VIEW_W, gpu.width);
    fb_h = mini(VIEW_H, gpu.height);
  }

  fb = (uint32_t *)malloc((size_t)fb_w * (size_t)fb_h * sizeof(uint32_t));
  if (!fb) {
    printf("[files] framebuffer allocation failed\n");
    return 1;
  }

  if (load_dir("/") < 0) {
    printf("[files] cannot open root directory\n");
    return 1;
  }

  int dirty = 1;
  while (1) {
    if (dirty) {
      if (preview_mode) draw_preview();
      else draw_list();
      present();
      dirty = 0;
    }

    AM_INPUT_KEYBRD_T ev = io_read(AM_INPUT_KEYBRD);
    if (ev.keycode == AM_KEY_NONE) {
      yield();
      continue;
    }
    if (!ev.keydown) continue;

    int r = preview_mode ? handle_preview_key(ev.keycode) : handle_list_key(ev.keycode);
    if (r < 0) break;
    if (r > 0) dirty = 1;
  }

  free(fb);
  return 0;
}
