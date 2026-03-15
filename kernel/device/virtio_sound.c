#include "virtio_sound.h"

#include "virtio.h"

#include "../libc/stdio.h"
#include "../libc/string.h"
#include "../memory/memory.h"

#define VIRTIO_ID_SOUND 25

#define VIRTIO_SND_QUEUE_CTRL 0
#define VIRTIO_SND_QUEUE_EVENT 1
#define VIRTIO_SND_QUEUE_TX 2
#define VIRTIO_SND_QUEUE_RX 3

#define SND_CTRL_DESC 8
#define SND_TX_DESC 16
#define SND_OTHER_DESC 4

#define VIRTIO_SND_R_PCM_SET_PARAMS 0x0101
#define VIRTIO_SND_R_PCM_PREPARE    0x0102
#define VIRTIO_SND_R_PCM_START      0x0104

#define VIRTIO_SND_S_OK             0x8000

#define VIRTIO_SND_PCM_FMT_S16      5
#define VIRTIO_SND_PCM_RATE_44100   6

typedef struct {
  struct VirtqDesc *desc;
  struct VirtqAvail *avail;
  struct VirtqUsed *used;
  uint8_t *free_desc;
  uint16_t num_desc;
  uint16_t used_idx;
  uint16_t queue_sel;
} VirtioQueue;

struct virtio_snd_hdr {
  uint32_t code;
};

struct virtio_snd_pcm_set_params {
  struct virtio_snd_hdr hdr;
  uint32_t stream_id;
  uint32_t buffer_bytes;
  uint32_t period_bytes;
  uint32_t features;
  uint8_t channels;
  uint8_t format;
  uint8_t rate;
  uint8_t padding;
};

struct virtio_snd_pcm_hdr {
  struct virtio_snd_hdr hdr;
  uint32_t stream_id;
};

struct virtio_snd_pcm_xfer {
  uint32_t stream_id;
};

struct virtio_snd_pcm_status {
  uint32_t status;
  uint32_t latency_bytes;
};

static struct {
  int ready;
  VirtioMMIODevice dev;
  VirtioQueue q_ctrl;
  VirtioQueue q_event;
  VirtioQueue q_tx;
  VirtioQueue q_rx;
} g_snd;

static int q_alloc_desc(VirtioQueue *q) {
  for (uint16_t i = 0; i < q->num_desc; i++) {
    if (q->free_desc[i]) {
      q->free_desc[i] = 0;
      return (int)i;
    }
  }
  return -1;
}

static void q_free_desc(VirtioQueue *q, int idx) {
  if (!q || idx < 0 || idx >= q->num_desc) return;
  q->free_desc[idx] = 1;
}

static int setup_queue(uint16_t queue_sel, uint16_t num_desc, VirtioQueue *q) {
  memset(q, 0, sizeof(*q));
  q->queue_sel = queue_sel;
  q->num_desc = num_desc;

  virtio_mmio_write32(g_snd.dev.base, VIRTIO_MMIO_QUEUE_SEL, queue_sel);
  uint32_t max = virtio_mmio_read32(g_snd.dev.base, VIRTIO_MMIO_QUEUE_NUM_MAX);
  if (max == 0 || max < num_desc) return -1;

  virtio_mmio_write32(g_snd.dev.base, VIRTIO_MMIO_QUEUE_NUM, num_desc);
  virtio_mmio_write32(g_snd.dev.base, VIRTIO_MMIO_GUEST_PAGE_SIZE, PAGE_SIZE);
  virtio_mmio_write32(g_snd.dev.base, VIRTIO_MMIO_QUEUE_ALIGN, VRING_ALIGN);

  void *page = kalloc();
  if (!page) return -1;
  memset(page, 0, PAGE_SIZE);

  q->desc = (struct VirtqDesc *)page;
  q->avail = (struct VirtqAvail *)((char *)page + num_desc * sizeof(struct VirtqDesc));
  uint64_t avail_end = (uint64_t)q->avail + sizeof(uint16_t) * 2 + sizeof(uint16_t) * num_desc;
  uint64_t used_off = (avail_end - (uint64_t)page + VRING_ALIGN - 1) & ~(uint64_t)(VRING_ALIGN - 1);
  q->used = (struct VirtqUsed *)((char *)page + used_off);

  q->free_desc = (uint8_t *)kalloc();
  if (!q->free_desc) return -1;
  memset(q->free_desc, 0, PAGE_SIZE);
  for (uint16_t i = 0; i < num_desc; i++) q->free_desc[i] = 1;

  virtio_mmio_write32(g_snd.dev.base, VIRTIO_MMIO_QUEUE_PFN, (uint32_t)((uint64_t)page / PAGE_SIZE));
  return 0;
}

static int queue_submit_sync(VirtioQueue *q, int head_desc) {
  if (!q || head_desc < 0) return -1;
  uint16_t slot = q->avail->idx % q->num_desc;
  q->avail->ring[slot] = (uint16_t)head_desc;
  __sync_synchronize();
  q->avail->idx++;
  __sync_synchronize();

  virtio_mmio_write32(g_snd.dev.base, VIRTIO_MMIO_QUEUE_NOTIFY, q->queue_sel);
  while (q->used->idx == q->used_idx) {
    __sync_synchronize();
  }
  q->used_idx++;
  return 0;
}

static int snd_pcm_cmd(uint32_t code, uint32_t stream_id) {
  struct virtio_snd_pcm_hdr req;
  struct virtio_snd_hdr resp;
  int d0 = q_alloc_desc(&g_snd.q_ctrl);
  int d1 = q_alloc_desc(&g_snd.q_ctrl);
  if (d0 < 0 || d1 < 0) {
    if (d0 >= 0) q_free_desc(&g_snd.q_ctrl, d0);
    if (d1 >= 0) q_free_desc(&g_snd.q_ctrl, d1);
    return -1;
  }

  req.hdr.code = code;
  req.stream_id = stream_id;
  memset(&resp, 0, sizeof(resp));

  g_snd.q_ctrl.desc[d0].addr = (uint64_t)&req;
  g_snd.q_ctrl.desc[d0].len = sizeof(req);
  g_snd.q_ctrl.desc[d0].flags = VRING_DESC_F_NEXT;
  g_snd.q_ctrl.desc[d0].next = (uint16_t)d1;

  g_snd.q_ctrl.desc[d1].addr = (uint64_t)&resp;
  g_snd.q_ctrl.desc[d1].len = sizeof(resp);
  g_snd.q_ctrl.desc[d1].flags = VRING_DESC_F_WRITE;
  g_snd.q_ctrl.desc[d1].next = 0;

  if (queue_submit_sync(&g_snd.q_ctrl, d0) < 0) {
    q_free_desc(&g_snd.q_ctrl, d0);
    q_free_desc(&g_snd.q_ctrl, d1);
    return -1;
  }

  q_free_desc(&g_snd.q_ctrl, d0);
  q_free_desc(&g_snd.q_ctrl, d1);
  return (resp.code == VIRTIO_SND_S_OK) ? 0 : -1;
}

static int snd_set_params(void) {
  struct virtio_snd_pcm_set_params req;
  struct virtio_snd_hdr resp;
  int d0 = q_alloc_desc(&g_snd.q_ctrl);
  int d1 = q_alloc_desc(&g_snd.q_ctrl);
  if (d0 < 0 || d1 < 0) {
    if (d0 >= 0) q_free_desc(&g_snd.q_ctrl, d0);
    if (d1 >= 0) q_free_desc(&g_snd.q_ctrl, d1);
    return -1;
  }

  memset(&req, 0, sizeof(req));
  memset(&resp, 0, sizeof(resp));
  req.hdr.code = VIRTIO_SND_R_PCM_SET_PARAMS;
  req.stream_id = 0;
  req.buffer_bytes = 4096;
  req.period_bytes = 1024;
  req.features = 0;
  req.channels = 2;
  req.format = VIRTIO_SND_PCM_FMT_S16;
  req.rate = VIRTIO_SND_PCM_RATE_44100;

  g_snd.q_ctrl.desc[d0].addr = (uint64_t)&req;
  g_snd.q_ctrl.desc[d0].len = sizeof(req);
  g_snd.q_ctrl.desc[d0].flags = VRING_DESC_F_NEXT;
  g_snd.q_ctrl.desc[d0].next = (uint16_t)d1;

  g_snd.q_ctrl.desc[d1].addr = (uint64_t)&resp;
  g_snd.q_ctrl.desc[d1].len = sizeof(resp);
  g_snd.q_ctrl.desc[d1].flags = VRING_DESC_F_WRITE;
  g_snd.q_ctrl.desc[d1].next = 0;

  if (queue_submit_sync(&g_snd.q_ctrl, d0) < 0) {
    q_free_desc(&g_snd.q_ctrl, d0);
    q_free_desc(&g_snd.q_ctrl, d1);
    return -1;
  }

  q_free_desc(&g_snd.q_ctrl, d0);
  q_free_desc(&g_snd.q_ctrl, d1);
  return (resp.code == VIRTIO_SND_S_OK) ? 0 : -1;
}

int virtio_sound_init(void) {
  memset(&g_snd, 0, sizeof(g_snd));
  if (virtio_mmio_find_device(VIRTIO_ID_SOUND, 0, &g_snd.dev) < 0) {
    printf("  VirtIO sound not found\n");
    return -1;
  }

  virtio_mmio_write32(g_snd.dev.base, VIRTIO_MMIO_STATUS, 0);
  uint32_t status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
  virtio_mmio_write32(g_snd.dev.base, VIRTIO_MMIO_STATUS, status);

  virtio_mmio_write32(g_snd.dev.base, VIRTIO_MMIO_GUEST_FEATURES_SEL, 0);
  virtio_mmio_write32(g_snd.dev.base, VIRTIO_MMIO_GUEST_FEATURES, 0);
  status |= VIRTIO_STATUS_FEATURES_OK;
  virtio_mmio_write32(g_snd.dev.base, VIRTIO_MMIO_STATUS, status);

  if (setup_queue(VIRTIO_SND_QUEUE_CTRL, SND_CTRL_DESC, &g_snd.q_ctrl) < 0) {
    printf("  VirtIO sound ctrl queue init failed\n");
    return -1;
  }
  if (setup_queue(VIRTIO_SND_QUEUE_EVENT, SND_OTHER_DESC, &g_snd.q_event) < 0) {
    printf("  VirtIO sound event queue init failed\n");
    return -1;
  }
  if (setup_queue(VIRTIO_SND_QUEUE_TX, SND_TX_DESC, &g_snd.q_tx) < 0) {
    printf("  VirtIO sound tx queue init failed\n");
    return -1;
  }
  if (setup_queue(VIRTIO_SND_QUEUE_RX, SND_OTHER_DESC, &g_snd.q_rx) < 0) {
    printf("  VirtIO sound rx queue init failed\n");
    return -1;
  }

  status |= VIRTIO_STATUS_DRIVER_OK;
  virtio_mmio_write32(g_snd.dev.base, VIRTIO_MMIO_STATUS, status);

  if (snd_set_params() < 0 || snd_pcm_cmd(VIRTIO_SND_R_PCM_PREPARE, 0) < 0 || snd_pcm_cmd(VIRTIO_SND_R_PCM_START, 0) < 0) {
    printf("  VirtIO sound stream setup failed\n");
    return -1;
  }

  g_snd.ready = 1;
  printf("  VirtIO sound initialized (irq=%d, stream=0, 44.1kHz s16 stereo)\n", g_snd.dev.irq);
  return 0;
}

int virtio_sound_is_ready(void) {
  return g_snd.ready;
}

int virtio_sound_write(const void *buf, size_t n) {
  if (!g_snd.ready || !buf || n < 4) return -1;

  size_t aligned = (n / 4) * 4;
  if (aligned == 0) return 0;

  struct virtio_snd_pcm_xfer xfer;
  struct virtio_snd_pcm_status st;

  int d0 = q_alloc_desc(&g_snd.q_tx);
  int d1 = q_alloc_desc(&g_snd.q_tx);
  int d2 = q_alloc_desc(&g_snd.q_tx);
  if (d0 < 0 || d1 < 0 || d2 < 0) {
    if (d0 >= 0) q_free_desc(&g_snd.q_tx, d0);
    if (d1 >= 0) q_free_desc(&g_snd.q_tx, d1);
    if (d2 >= 0) q_free_desc(&g_snd.q_tx, d2);
    return -1;
  }

  xfer.stream_id = 0;
  memset(&st, 0, sizeof(st));

  g_snd.q_tx.desc[d0].addr = (uint64_t)&xfer;
  g_snd.q_tx.desc[d0].len = sizeof(xfer);
  g_snd.q_tx.desc[d0].flags = VRING_DESC_F_NEXT;
  g_snd.q_tx.desc[d0].next = (uint16_t)d1;

  g_snd.q_tx.desc[d1].addr = (uint64_t)buf;
  g_snd.q_tx.desc[d1].len = (uint32_t)aligned;
  g_snd.q_tx.desc[d1].flags = VRING_DESC_F_NEXT;
  g_snd.q_tx.desc[d1].next = (uint16_t)d2;

  g_snd.q_tx.desc[d2].addr = (uint64_t)&st;
  g_snd.q_tx.desc[d2].len = sizeof(st);
  g_snd.q_tx.desc[d2].flags = VRING_DESC_F_WRITE;
  g_snd.q_tx.desc[d2].next = 0;

  if (queue_submit_sync(&g_snd.q_tx, d0) < 0) {
    q_free_desc(&g_snd.q_tx, d0);
    q_free_desc(&g_snd.q_tx, d1);
    q_free_desc(&g_snd.q_tx, d2);
    return -1;
  }

  q_free_desc(&g_snd.q_tx, d0);
  q_free_desc(&g_snd.q_tx, d1);
  q_free_desc(&g_snd.q_tx, d2);
  return (st.status == VIRTIO_SND_S_OK) ? (int)aligned : -1;
}

void virtio_sound_poll(void) {
  if (!g_snd.ready) return;
  virtio_ack_interrupt(&g_snd.dev);
}

int virtio_sound_match_irq(int irq) {
  return g_snd.ready && irq == (int)g_snd.dev.irq;
}

void virtio_sound_handle_irq(void) {
  virtio_sound_poll();
}
