#include "virtio_gpu.h"

#include "virtio.h"

#include "../libc/stdio.h"
#include "../libc/string.h"
#include "../memory/memory.h"

#define VIRTIO_ID_GPU 16
#define VIRTIO_GPU_QUEUE_CTRL 0

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO      0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D    0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF         0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT           0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH        0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D   0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106

#define VIRTIO_GPU_RESP_OK_NODATA            0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO      0x1101

#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM     1

#define GPU_NUM_DESC 8
#define GPU_RESOURCE_ID 1

struct virtio_gpu_ctrl_hdr {
  uint32_t type;
  uint32_t flags;
  uint64_t fence_id;
  uint32_t ctx_id;
  uint32_t padding;
};

struct virtio_gpu_rect {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
};

struct virtio_gpu_display_one {
  struct virtio_gpu_rect r;
  uint32_t enabled;
  uint32_t flags;
};

struct virtio_gpu_resp_display_info {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_display_one pmodes[16];
};

struct virtio_gpu_resource_create_2d {
  struct virtio_gpu_ctrl_hdr hdr;
  uint32_t resource_id;
  uint32_t format;
  uint32_t width;
  uint32_t height;
};

struct virtio_gpu_resource_attach_backing {
  struct virtio_gpu_ctrl_hdr hdr;
  uint32_t resource_id;
  uint32_t nr_entries;
};

struct virtio_gpu_mem_entry {
  uint64_t addr;
  uint32_t length;
  uint32_t padding;
};

struct virtio_gpu_set_scanout {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_rect r;
  uint32_t scanout_id;
  uint32_t resource_id;
};

struct virtio_gpu_transfer_to_host_2d {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_rect r;
  uint64_t offset;
  uint32_t resource_id;
  uint32_t padding;
};

struct virtio_gpu_resource_flush {
  struct virtio_gpu_ctrl_hdr hdr;
  struct virtio_gpu_rect r;
  uint32_t resource_id;
  uint32_t padding;
};

static struct {
  int ready;
  int width;
  int height;
  int pages;
  VirtioMMIODevice dev;
  void *fb_pages[1024];
} g_gpu;

static uint8_t req_buf[16384];
static uint8_t resp_buf[4096];
static uint8_t status_byte;

static int gpu_cmd(const void *req, uint32_t req_len, void *resp, uint32_t resp_len) {
  VirtioMMIODevice *d = &g_gpu.dev;
  int d0 = virtio_alloc_desc(d);
  int d1 = virtio_alloc_desc(d);
  int d2 = virtio_alloc_desc(d);
  if (d0 < 0 || d1 < 0 || d2 < 0) return -1;

  status_byte = 0xff;
  d->desc[d0].addr = (uint64_t)req;
  d->desc[d0].len = req_len;
  d->desc[d0].flags = VRING_DESC_F_NEXT;
  d->desc[d0].next = (uint16_t)d1;

  d->desc[d1].addr = (uint64_t)resp;
  d->desc[d1].len = resp_len;
  d->desc[d1].flags = VRING_DESC_F_NEXT | VRING_DESC_F_WRITE;
  d->desc[d1].next = (uint16_t)d2;

  d->desc[d2].addr = (uint64_t)&status_byte;
  d->desc[d2].len = 1;
  d->desc[d2].flags = VRING_DESC_F_WRITE;
  d->desc[d2].next = 0;

  virtio_submit_and_poll(d, d0, VIRTIO_GPU_QUEUE_CTRL);
  virtio_ack_interrupt(d);

  virtio_free_desc(d, d0);
  virtio_free_desc(d, d1);
  virtio_free_desc(d, d2);
  return 0;
}

static int fb_copy_rect(int x, int y, int w, int h, const uint32_t *pixels) {
  if (!pixels) return -1;
  for (int row = 0; row < h; row++) {
    int py = y + row;
    if (py < 0 || py >= g_gpu.height) continue;
    for (int col = 0; col < w; col++) {
      int px = x + col;
      if (px < 0 || px >= g_gpu.width) continue;
      uint64_t off = ((uint64_t)py * (uint64_t)g_gpu.width + (uint64_t)px) * 4;
      int page_idx = (int)(off / PAGE_SIZE);
      int in_page = (int)(off % PAGE_SIZE);
      *(uint32_t *)((char *)g_gpu.fb_pages[page_idx] + in_page) = pixels[row * w + col];
    }
  }
  return 0;
}

int virtio_gpu_init(void) {
  memset(&g_gpu, 0, sizeof(g_gpu));
  if (virtio_mmio_find_device(VIRTIO_ID_GPU, 0, &g_gpu.dev) < 0) {
    printf("  VirtIO GPU not found\n");
    return -1;
  }
  if (virtio_mmio_init_queue(&g_gpu.dev, VIRTIO_GPU_QUEUE_CTRL, GPU_NUM_DESC) < 0) {
    printf("  VirtIO GPU queue init failed\n");
    return -1;
  }

  struct virtio_gpu_ctrl_hdr req;
  struct virtio_gpu_resp_display_info resp;
  memset(&req, 0, sizeof(req));
  memset(&resp, 0, sizeof(resp));
  req.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
  if (gpu_cmd(&req, sizeof(req), &resp, sizeof(resp)) < 0) return -1;
  if (resp.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) return -1;

  g_gpu.width = (int)resp.pmodes[0].r.width;
  g_gpu.height = (int)resp.pmodes[0].r.height;
  if (g_gpu.width <= 0 || g_gpu.height <= 0) return -1;

  uint64_t fb_size = (uint64_t)g_gpu.width * (uint64_t)g_gpu.height * 4;
  g_gpu.pages = (int)((fb_size + PAGE_SIZE - 1) / PAGE_SIZE);
  if (g_gpu.pages > (int)(sizeof(g_gpu.fb_pages) / sizeof(g_gpu.fb_pages[0]))) return -1;
  for (int i = 0; i < g_gpu.pages; i++) {
    g_gpu.fb_pages[i] = kalloc();
    if (!g_gpu.fb_pages[i]) return -1;
    memset(g_gpu.fb_pages[i], 0, PAGE_SIZE);
  }

  struct virtio_gpu_resource_create_2d *c2d = (struct virtio_gpu_resource_create_2d *)req_buf;
  memset(c2d, 0, sizeof(*c2d));
  c2d->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
  c2d->resource_id = GPU_RESOURCE_ID;
  c2d->format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
  c2d->width = (uint32_t)g_gpu.width;
  c2d->height = (uint32_t)g_gpu.height;
  if (gpu_cmd(c2d, sizeof(*c2d), resp_buf, sizeof(struct virtio_gpu_ctrl_hdr)) < 0) return -1;

  struct virtio_gpu_resource_attach_backing *ab = (struct virtio_gpu_resource_attach_backing *)req_buf;
  memset(req_buf, 0, sizeof(req_buf));
  ab->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
  ab->resource_id = GPU_RESOURCE_ID;
  ab->nr_entries = (uint32_t)g_gpu.pages;
  struct virtio_gpu_mem_entry *me = (struct virtio_gpu_mem_entry *)(ab + 1);
  for (int i = 0; i < g_gpu.pages; i++) {
    me[i].addr = (uint64_t)g_gpu.fb_pages[i];
    me[i].length = PAGE_SIZE;
    me[i].padding = 0;
  }
  uint32_t ab_len = sizeof(*ab) + (uint32_t)g_gpu.pages * sizeof(struct virtio_gpu_mem_entry);
  if (gpu_cmd(ab, ab_len, resp_buf, sizeof(struct virtio_gpu_ctrl_hdr)) < 0) return -1;

  struct virtio_gpu_set_scanout sc;
  memset(&sc, 0, sizeof(sc));
  sc.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
  sc.r.x = 0;
  sc.r.y = 0;
  sc.r.width = (uint32_t)g_gpu.width;
  sc.r.height = (uint32_t)g_gpu.height;
  sc.scanout_id = 0;
  sc.resource_id = GPU_RESOURCE_ID;
  if (gpu_cmd(&sc, sizeof(sc), resp_buf, sizeof(struct virtio_gpu_ctrl_hdr)) < 0) return -1;

  virtio_mmio_set_driver_ok(&g_gpu.dev);
  g_gpu.ready = 1;
  printf("  VirtIO GPU initialized (%dx%d, irq=%d)\n", g_gpu.width, g_gpu.height, g_gpu.dev.irq);
  return 0;
}

int virtio_gpu_is_ready(void) {
  return g_gpu.ready;
}

int virtio_gpu_resolution(int *w, int *h) {
  if (!g_gpu.ready) return -1;
  if (w) *w = g_gpu.width;
  if (h) *h = g_gpu.height;
  return 0;
}

int virtio_gpu_fbdraw(int x, int y, int w, int h, const uint32_t *pixels, int sync) {
  if (!g_gpu.ready || w <= 0 || h <= 0) return -1;
  if (fb_copy_rect(x, y, w, h, pixels) < 0) return -1;
  if (!sync) return 0;

  struct virtio_gpu_transfer_to_host_2d tx;
  memset(&tx, 0, sizeof(tx));
  tx.hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
  tx.r.x = (uint32_t)x;
  tx.r.y = (uint32_t)y;
  tx.r.width = (uint32_t)w;
  tx.r.height = (uint32_t)h;
  tx.offset = ((uint64_t)y * (uint64_t)g_gpu.width + (uint64_t)x) * 4;
  tx.resource_id = GPU_RESOURCE_ID;
  if (gpu_cmd(&tx, sizeof(tx), resp_buf, sizeof(struct virtio_gpu_ctrl_hdr)) < 0) return -1;

  struct virtio_gpu_resource_flush fl;
  memset(&fl, 0, sizeof(fl));
  fl.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
  fl.r.x = (uint32_t)x;
  fl.r.y = (uint32_t)y;
  fl.r.width = (uint32_t)w;
  fl.r.height = (uint32_t)h;
  fl.resource_id = GPU_RESOURCE_ID;
  if (gpu_cmd(&fl, sizeof(fl), resp_buf, sizeof(struct virtio_gpu_ctrl_hdr)) < 0) return -1;

  return 0;
}

void virtio_gpu_poll(void) {
  if (!g_gpu.ready) return;
  virtio_ack_interrupt(&g_gpu.dev);
}

int virtio_gpu_match_irq(int irq) {
  return g_gpu.ready && irq == (int)g_gpu.dev.irq;
}

void virtio_gpu_handle_irq(void) {
  if (!g_gpu.ready) return;
  virtio_ack_interrupt(&g_gpu.dev);
}
