#include <NDL.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>

#define NDL_MAX_DIRTY_RECTS 64

typedef struct
{
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
} NDL_DirtyRect;

static int evtdev = -1;
static int fbdev = -1;
static int fbsyncdev = -1;
static int audiodev = -1;
static int screen_w = 0;
static int screen_h = 0;
static int canvas_w = 0;
static int canvas_h = 0;
static uint32_t *fbmem = NULL;
static size_t fbmem_size = 0;
static NDL_DirtyRect dirty_rects[NDL_MAX_DIRTY_RECTS];
static int dirty_count = 0;
static int audio_freq = 44100;
static int audio_channels = 2;
static int audio_pending = 0;
static uint64_t audio_last_us = 0;
static struct timeval start_time;

static uint64_t now_us(void)
{
  struct timeval tv;
  if (gettimeofday(&tv, 0) != 0)
    return 0;
  return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

static void update_audio_pending(void)
{
  uint64_t now = now_us();
  if (audio_last_us == 0)
  {
    audio_last_us = now;
    return;
  }
  if (now <= audio_last_us)
    return;

  uint64_t delta = now - audio_last_us;
  uint64_t bytes_per_sec = (uint64_t)audio_freq * (uint64_t)audio_channels * 2ULL;
  uint64_t consumed = delta * bytes_per_sec / 1000000ULL;
  if ((int)consumed > audio_pending)
    audio_pending = 0;
  else
    audio_pending -= (int)consumed;
  audio_last_us = now;
}

static int rect_x2(const NDL_DirtyRect *r)
{
  return r->x + r->w;
}

static int rect_y2(const NDL_DirtyRect *r)
{
  return r->y + r->h;
}

static int rect_contains(const NDL_DirtyRect *a, const NDL_DirtyRect *b)
{
  return a->x <= b->x && a->y <= b->y &&
         rect_x2(a) >= rect_x2(b) && rect_y2(a) >= rect_y2(b);
}

static int rect_touch_or_overlap(const NDL_DirtyRect *a, const NDL_DirtyRect *b)
{
  return a->x <= rect_x2(b) && rect_x2(a) >= b->x &&
         a->y <= rect_y2(b) && rect_y2(a) >= b->y;
}

static void rect_union_into(NDL_DirtyRect *dst, const NDL_DirtyRect *src)
{
  int x1 = dst->x < src->x ? dst->x : src->x;
  int y1 = dst->y < src->y ? dst->y : src->y;
  int x2 = rect_x2(dst) > rect_x2(src) ? rect_x2(dst) : rect_x2(src);
  int y2 = rect_y2(dst) > rect_y2(src) ? rect_y2(dst) : rect_y2(src);

  dst->x = x1;
  dst->y = y1;
  dst->w = x2 - x1;
  dst->h = y2 - y1;
}

static void dirty_collapse(void)
{
  if (dirty_count <= 1)
    return;
  for (int i = 1; i < dirty_count; i++)
    rect_union_into(&dirty_rects[0], &dirty_rects[i]);
  dirty_count = 1;
}

static void dirty_add_screen(int x, int y, int w, int h)
{
  if (w <= 0 || h <= 0)
    return;

  int x1 = x;
  int y1 = y;
  int x2 = x + w;
  int y2 = y + h;
  if (x1 < 0)
    x1 = 0;
  if (y1 < 0)
    y1 = 0;
  if (x2 > screen_w)
    x2 = screen_w;
  if (y2 > screen_h)
    y2 = screen_h;
  if (x1 >= x2 || y1 >= y2)
    return;

  NDL_DirtyRect nr = {x1, y1, x2 - x1, y2 - y1};
  for (int i = 0; i < dirty_count; i++)
  {
    if (rect_contains(&dirty_rects[i], &nr))
      return;
    if (rect_contains(&nr, &dirty_rects[i]) ||
        rect_touch_or_overlap(&dirty_rects[i], &nr))
    {
      rect_union_into(&dirty_rects[i], &nr);
      for (int j = 0; j < dirty_count; j++)
      {
        if (j == i)
          continue;
        if (!rect_touch_or_overlap(&dirty_rects[i], &dirty_rects[j]))
          continue;
        rect_union_into(&dirty_rects[i], &dirty_rects[j]);
        dirty_rects[j] = dirty_rects[dirty_count - 1];
        dirty_count--;
        j--;
      }
      return;
    }
  }

  if (dirty_count < NDL_MAX_DIRTY_RECTS)
  {
    dirty_rects[dirty_count++] = nr;
    return;
  }

  rect_union_into(&dirty_rects[0], &nr);
  dirty_collapse();
}

static void sync_fb(void)
{
  if (fbsyncdev < 0)
    return;
  if (dirty_count > 0)
  {
    size_t bytes = (size_t)dirty_count * sizeof(dirty_rects[0]);
    int n = write(fbsyncdev, dirty_rects, bytes);
    if (n == (int)bytes)
      dirty_count = 0;
    return;
  }
  char ch = 0;
  write(fbsyncdev, &ch, 1);
}

static void map_fb(void)
{
  if (fbmem || fbdev < 0 || screen_w <= 0 || screen_h <= 0)
    return;

  size_t size = (size_t)screen_w * (size_t)screen_h * sizeof(uint32_t);
  void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fbdev, 0);
  if (p == MAP_FAILED)
    return;

  fbmem = (uint32_t *)p;
  fbmem_size = size;
}

static void unmap_fb(void)
{
  if (!fbmem)
    return;
  munmap(fbmem, fbmem_size);
  fbmem = NULL;
  fbmem_size = 0;
  dirty_count = 0;
}

static void read_dispinfo(void)
{
  int fd = open("/device/dispinfo", O_RDONLY, 0);
  if (fd < 0)
    return;
  char buf[64] = {0};
  int n = read(fd, buf, sizeof(buf) - 1);
  close(fd);
  if (n <= 0)
    return;
  buf[n] = '\0';
  int w = 0, h = 0;
  if (sscanf(buf, "WIDTH:%d\nHEIGHT:%d\n", &w, &h) == 2 && w > 0 && h > 0)
  {
    screen_w = w;
    screen_h = h;
  }
}

int NDL_Init(uint32_t flags)
{
  (void)flags;
  gettimeofday(&start_time, 0);
  read_dispinfo();
  evtdev = open("/device/events", O_RDONLY, 0);
  fbdev = open("/device/fb", O_RDWR, 0);
  fbsyncdev = open("/device/fbsync", O_WRONLY, 0);
  audiodev = open("/device/audio", O_WRONLY, 0);
  map_fb();
  audio_last_us = now_us();
  return 0;
}

void NDL_Quit(void)
{
  unmap_fb();
  if (evtdev >= 0)
  {
    close(evtdev);
    evtdev = -1;
  }
  if (fbdev >= 0)
  {
    close(fbdev);
    fbdev = -1;
  }
  if (fbsyncdev >= 0)
  {
    close(fbsyncdev);
    fbsyncdev = -1;
  }
  if (audiodev >= 0)
  {
    close(audiodev);
    audiodev = -1;
  }
}

uint32_t NDL_GetTicks(void)
{
  struct timeval now;
  gettimeofday(&now, 0);
  long secs = (long)(now.tv_sec - start_time.tv_sec);
  long usecs = (long)(now.tv_usec - start_time.tv_usec);
  if (usecs < 0)
  {
    secs -= 1;
    usecs += 1000000;
  }
  if (secs < 0)
  {
    return 0;
  }
  uint64_t elapsed = (uint64_t)secs * 1000ULL + (uint64_t)usecs / 1000ULL;
  return (uint32_t)elapsed;
}

int NDL_PollEvent(char *buf, int len)
{
  if (!buf || len <= 0 || evtdev < 0)
    return 0;
  int n = read(evtdev, buf, len - 1);
  if (n <= 0)
  {
    if (len > 0)
      buf[0] = '\0';
    return 0;
  }
  buf[n] = '\0';
  return n;
}

void NDL_OpenCanvas(int *w, int *h)
{
  read_dispinfo();
  if (screen_w <= 0 || screen_h <= 0)
  {
    screen_w = 640;
    screen_h = 480;
  }

  if (!w || !h)
  {
    canvas_w = screen_w;
    canvas_h = screen_h;
    map_fb();
    return;
  }

  if (*w == 0 || *h == 0)
  {
    *w = screen_w;
    *h = screen_h;
  }
  if (*w > screen_w)
    *w = screen_w;
  if (*h > screen_h)
    *h = screen_h;
  canvas_w = *w;
  canvas_h = *h;
  map_fb();
}

void NDL_DrawRect(uint32_t *pixels, int x, int y, int w, int h)
{
  if (!pixels || w <= 0 || h <= 0 || fbdev < 0)
    return;
  if (screen_w <= 0 || screen_h <= 0)
    read_dispinfo();
  map_fb();
  if (canvas_w <= 0 || canvas_h <= 0)
  {
    canvas_w = screen_w;
    canvas_h = screen_h;
  }

  int base_x = (screen_w - canvas_w) / 2;
  int base_y = (screen_h - canvas_h) / 2;
  int draw_x = x + base_x;
  int draw_y = y + base_y;

  int px = draw_x;
  int py = draw_y;
  int copy_w = w;
  int copy_h = h;
  int src_x = 0;
  int src_y = 0;

  if (px < 0)
  {
    src_x = -px;
    copy_w += px;
    px = 0;
  }
  if (py < 0)
  {
    src_y = -py;
    copy_h += py;
    py = 0;
  }
  if (px + copy_w > screen_w)
    copy_w = screen_w - px;
  if (py + copy_h > screen_h)
    copy_h = screen_h - py;
  if (copy_w <= 0 || copy_h <= 0)
    return;

  if (fbmem)
  {
    for (int row = 0; row < copy_h; row++)
    {
      uint32_t *dst = fbmem + (py + row) * screen_w + px;
      uint32_t *src = pixels + (src_y + row) * w + src_x;
      memcpy(dst, src, (size_t)copy_w * sizeof(uint32_t));
    }
    dirty_add_screen(px, py, copy_w, copy_h);
    sync_fb();
    return;
  }

  if (px == 0 && copy_w == screen_w && src_x == 0 && w == copy_w)
  {
    off_t off = (off_t)(py * screen_w * 4);
    lseek(fbdev, off, SEEK_SET);
    write(fbdev, pixels + src_y * w, (size_t)copy_w * copy_h * 4);
    sync_fb();
    return;
  }

  for (int row = 0; row < copy_h; row++)
  {
    int src_off = (src_y + row) * w + src_x;
    off_t off = (off_t)(((py + row) * screen_w + px) * 4);
    if (lseek(fbdev, off, SEEK_SET) < 0)
      return;
    write(fbdev, &pixels[src_off], (size_t)copy_w * 4);
  }
  sync_fb();
}

uint32_t *NDL_GetFramebuffer(int *pitch)
{
  if (screen_w <= 0 || screen_h <= 0)
    read_dispinfo();
  map_fb();
  if (!fbmem)
    return NULL;
  if (canvas_w <= 0 || canvas_h <= 0)
  {
    canvas_w = screen_w;
    canvas_h = screen_h;
  }

  int base_x = (screen_w - canvas_w) / 2;
  int base_y = (screen_h - canvas_h) / 2;
  if (pitch)
    *pitch = screen_w * (int)sizeof(uint32_t);
  return fbmem + base_y * screen_w + base_x;
}

void NDL_FlushRect(int x, int y, int w, int h)
{
  if (w <= 0 || h <= 0)
  {
    x = 0;
    y = 0;
    w = canvas_w;
    h = canvas_h;
  }
  if (canvas_w <= 0 || canvas_h <= 0)
  {
    canvas_w = screen_w;
    canvas_h = screen_h;
  }

  int base_x = (screen_w - canvas_w) / 2;
  int base_y = (screen_h - canvas_h) / 2;
  dirty_add_screen(x + base_x, y + base_y, w, h);
  sync_fb();
}

void NDL_OpenAudio(int freq, int channels, int samples)
{
  (void)samples;
  if (freq > 0)
    audio_freq = freq;
  if (channels > 0)
    audio_channels = channels;
  audio_pending = 0;
  audio_last_us = now_us();
}

void NDL_CloseAudio(void)
{
  audio_pending = 0;
}

int NDL_PlayAudio(void *buf, int len)
{
  if (!buf || len <= 0 || audiodev < 0)
    return 0;
  update_audio_pending();
  int n = write(audiodev, buf, (size_t)len);
  if (n > 0)
    audio_pending += n;
  return (n > 0) ? n : 0;
}

int NDL_QueryAudio(void)
{
  update_audio_pending();
  return audio_pending;
}
