#include "virtual_gpu.h"

#include "../libc/string.h"
#include "../memory/memory.h"
#include "virtio_gpu.h"

struct DisplayPCB {
  char shadow_fb_pages[VIRTUAL_GPU_SHADOW_FB_MAX_PAGES][PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
  int shadow_fb_npages;
  uint8_t fb_dirty;
  int fb_dirty_x;
  int fb_dirty_y;
  int fb_dirty_w;
  int fb_dirty_h;
};

typedef struct {
  uint8_t mask;
  uint8_t reserved[3];
  GpuDirtyRect rects[VIRTUAL_GPU_MAX_DISPLAYS];
} VirtualGpuFbSyncInfo;

static DisplayPCB g_displays[VIRTUAL_GPU_MAX_DISPLAYS];

void virtual_gpu_init_displays(void) {
  memset(g_displays, 0, sizeof(g_displays));
}

DisplayPCB *virtual_gpu_display_pcb(int pid) {
  if (pid <= 0 || pid > VIRTUAL_GPU_MAX_DISPLAYS) return 0;
  return &g_displays[pid - 1];
}

void virtual_gpu_reset_display(DisplayPCB *display) {
  if (!display) return;
  display->shadow_fb_npages = 0;
  display->fb_dirty = 0;
  display->fb_dirty_x = 0;
  display->fb_dirty_y = 0;
  display->fb_dirty_w = 0;
  display->fb_dirty_h = 0;
}

static void display_clear_dirty(DisplayPCB *display) {
  if (!display) return;
  display->fb_dirty = 0;
  display->fb_dirty_x = 0;
  display->fb_dirty_y = 0;
  display->fb_dirty_w = 0;
  display->fb_dirty_h = 0;
}

static int display_ensure_shadow(DisplayPCB *display) {
  if (!display) return -1;
  if (!virtio_gpu_is_ready()) return -1;
  uint64_t fb_size = virtio_gpu_fb_size();
  int pages = (int)((fb_size + PAGE_SIZE - 1) / PAGE_SIZE);
  if (pages <= 0 || pages > VIRTUAL_GPU_SHADOW_FB_MAX_PAGES) return -1;
  if (((uint64_t)display->shadow_fb_pages & (PAGE_SIZE - 1)) != 0) return -1;

  if (display->shadow_fb_npages <= 0) {
    display->shadow_fb_npages = pages;
    memset(display->shadow_fb_pages, 0, (size_t)pages * PAGE_SIZE);
  }
  return 0;
}

int virtual_gpu_fb_page(DisplayPCB *display, uint64_t offset, uint64_t *pa) {
  if (!pa) return -1;
  if ((offset % PAGE_SIZE) != 0) return -1;
  if (offset >= virtio_gpu_fb_size()) return -1;
  if (display_ensure_shadow(display) < 0) return -1;

  uint64_t page_idx = offset / PAGE_SIZE;
  if (page_idx >= (uint64_t)display->shadow_fb_npages) return -1;
  *pa = (uint64_t)&display->shadow_fb_pages[page_idx];
  return 0;
}

int virtual_gpu_existing_fb_page(DisplayPCB *display, uint64_t offset, uint64_t *pa) {
  if (!display || !pa) return -1;
  if ((offset % PAGE_SIZE) != 0) return -1;
  if (offset >= virtio_gpu_fb_size()) return -1;
  if (((uint64_t)display->shadow_fb_pages & (PAGE_SIZE - 1)) != 0) return -1;
  if (display->shadow_fb_npages <= 0) return -1;

  uint64_t page_idx = offset / PAGE_SIZE;
  if (page_idx >= (uint64_t)display->shadow_fb_npages) return -1;
  *pa = (uint64_t)&display->shadow_fb_pages[page_idx];
  return 0;
}

static int display_dirty_add(DisplayPCB *display, const GpuDirtyRect *rect) {
  if (!display || !rect) return 0;

  int screen_w = 0;
  int screen_h = 0;
  if (virtio_gpu_resolution(&screen_w, &screen_h) < 0) return 0;
  if (screen_w <= 0 || screen_h <= 0) return 0;

  int x1 = rect->x;
  int y1 = rect->y;
  int x2 = rect->x + rect->w;
  int y2 = rect->y + rect->h;

  if (x1 < 0) x1 = 0;
  if (y1 < 0) y1 = 0;
  if (x2 > screen_w) x2 = screen_w;
  if (y2 > screen_h) y2 = screen_h;
  if (x1 >= x2 || y1 >= y2) return 0;

  if (!display->fb_dirty || display->fb_dirty_w <= 0 || display->fb_dirty_h <= 0) {
    display->fb_dirty_x = x1;
    display->fb_dirty_y = y1;
    display->fb_dirty_w = x2 - x1;
    display->fb_dirty_h = y2 - y1;
  } else {
    int old_x2 = display->fb_dirty_x + display->fb_dirty_w;
    int old_y2 = display->fb_dirty_y + display->fb_dirty_h;
    if (x1 > display->fb_dirty_x) x1 = display->fb_dirty_x;
    if (y1 > display->fb_dirty_y) y1 = display->fb_dirty_y;
    if (x2 < old_x2) x2 = old_x2;
    if (y2 < old_y2) y2 = old_y2;
    display->fb_dirty_x = x1;
    display->fb_dirty_y = y1;
    display->fb_dirty_w = x2 - x1;
    display->fb_dirty_h = y2 - y1;
  }
  display->fb_dirty = 1;
  return 1;
}

static void display_dirty_mark_full_if_empty(DisplayPCB *display) {
  if (!display) return;
  if (display->fb_dirty_w > 0 && display->fb_dirty_h > 0) {
    display->fb_dirty = 1;
    return;
  }

  int screen_w = 0;
  int screen_h = 0;
  if (virtio_gpu_resolution(&screen_w, &screen_h) < 0) return;
  if (screen_w <= 0 || screen_h <= 0) return;

  display->fb_dirty = 1;
  display->fb_dirty_x = 0;
  display->fb_dirty_y = 0;
  display->fb_dirty_w = screen_w;
  display->fb_dirty_h = screen_h;
}

int virtual_gpu_fbsync_read(void *buf, size_t n) {
  if (!buf || n < 1) return 0;

  if (n >= sizeof(VirtualGpuFbSyncInfo)) {
    VirtualGpuFbSyncInfo info;
    memset(&info, 0, sizeof(info));
    for (int i = 0; i < VIRTUAL_GPU_MAX_DISPLAYS; i++) {
      DisplayPCB *display = &g_displays[i];
      if (!display->fb_dirty) continue;
      info.mask |= (uint8_t)(1 << i);
      info.rects[i].x = display->fb_dirty_x;
      info.rects[i].y = display->fb_dirty_y;
      info.rects[i].w = display->fb_dirty_w;
      info.rects[i].h = display->fb_dirty_h;
      display_clear_dirty(display);
    }
    memcpy(buf, &info, sizeof(info));
    return (int)sizeof(info);
  }

  uint8_t mask = 0;
  for (int i = 0; i < VIRTUAL_GPU_MAX_DISPLAYS; i++) {
    DisplayPCB *display = &g_displays[i];
    if (!display->fb_dirty) continue;
    mask |= (uint8_t)(1 << i);
    display_clear_dirty(display);
  }
  *(uint8_t *)buf = mask;
  return 1;
}

int virtual_gpu_fbsync_write(DisplayPCB *display, const void *buf, size_t n) {
  if (!display || !buf) return -1;

  int has_rect = 0;
  if (n >= sizeof(GpuDirtyRect) && (n % sizeof(GpuDirtyRect)) == 0) {
    const GpuDirtyRect *rects = (const GpuDirtyRect *)buf;
    size_t count = n / sizeof(GpuDirtyRect);
    for (size_t i = 0; i < count; i++) {
      if (display_dirty_add(display, &rects[i])) has_rect = 1;
    }
  }
  if (!has_rect) {
    display_dirty_mark_full_if_empty(display);
  }
  return 0;
}

static int shadow_fb_write(DisplayPCB *display, uint32_t *off, const void *buf, size_t n) {
  if (!display || !off || !buf) return -1;
  if ((n % 4) != 0) return -1;
  if (display_ensure_shadow(display) < 0) return -1;

  uint64_t fb_size = virtio_gpu_fb_size();
  uint64_t pos = *off;
  if (pos >= fb_size) return 0;

  size_t can = n;
  if ((uint64_t)can > fb_size - pos) can = (size_t)(fb_size - pos);

  const char *src = (const char *)buf;
  size_t copied = 0;
  while (copied < can) {
    uint64_t cur = pos + copied;
    uint64_t page_idx = cur / PAGE_SIZE;
    uint64_t in_page = cur % PAGE_SIZE;
    size_t chunk = PAGE_SIZE - (size_t)in_page;
    if (chunk > can - copied) chunk = can - copied;
    memcpy(display->shadow_fb_pages[page_idx] + in_page, src + copied, chunk);
    copied += chunk;
  }

  *off += (uint32_t)copied;
  return (int)copied;
}

int virtual_gpu_fb_write(DisplayPCB *display, uint32_t *off, const void *buf, size_t n) {
  return shadow_fb_write(display, off, buf, n);
}
