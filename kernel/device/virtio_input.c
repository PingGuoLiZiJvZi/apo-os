#include "virtio_input.h"

#include "virtio.h"

#include "../libc/stdio.h"
#include "../libc/string.h"

#define VIRTIO_ID_INPUT 18
#define VIRTIO_INPUT_QUEUE_EVENT 0
#define INPUT_MAX_DEV 4
#define INPUT_NUM_DESC 16

typedef struct {
  uint16_t type;
  uint16_t code;
  uint32_t value;
} __attribute__((packed)) virtio_input_event_raw_t;

typedef struct {
  int ready;
  VirtioMMIODevice dev;
  virtio_input_event_raw_t event_buf[INPUT_NUM_DESC];
} InputDev;

static struct {
  int dev_count;
  InputDev devs[INPUT_MAX_DEV];
  VirtioInputEvent q[128];
  int q_head;
  int q_tail;
} g_in;

static void enqueue_event(uint16_t type, uint16_t code, uint32_t value) {
  int next_tail = (g_in.q_tail + 1) % (int)(sizeof(g_in.q) / sizeof(g_in.q[0]));
  if (next_tail == g_in.q_head) return;
  g_in.q[g_in.q_tail].type = type;
  g_in.q[g_in.q_tail].code = code;
  g_in.q[g_in.q_tail].value = value;
  g_in.q_tail = next_tail;
}

static int input_post_buffer(InputDev *d, int desc_idx) {
  if (!d || desc_idx < 0) return -1;
  d->dev.desc[desc_idx].addr = (uint64_t)&d->event_buf[desc_idx];
  d->dev.desc[desc_idx].len = sizeof(virtio_input_event_raw_t);
  d->dev.desc[desc_idx].flags = VRING_DESC_F_WRITE;
  d->dev.desc[desc_idx].next = 0;

  uint16_t slot = d->dev.avail->idx % d->dev.num_desc;
  d->dev.avail->ring[slot] = (uint16_t)desc_idx;
  __sync_synchronize();
  d->dev.avail->idx++;
  __sync_synchronize();
  return 0;
}

static void input_poll_one(InputDev *d) {
  if (!d || !d->ready) return;

  while (d->dev.used->idx != d->dev.used_idx) {
    struct VirtqUsedElem e = d->dev.used->ring[d->dev.used_idx % d->dev.num_desc];
    d->dev.used_idx++;

    int desc_idx = (int)e.id;
    if (desc_idx >= 0 && desc_idx < d->dev.num_desc) {
      virtio_input_event_raw_t *ev = &d->event_buf[desc_idx];
      enqueue_event(ev->type, ev->code, ev->value);
      input_post_buffer(d, desc_idx);
      virtio_mmio_write32(d->dev.base, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_INPUT_QUEUE_EVENT);
    }
  }
  virtio_ack_interrupt(&d->dev);
}

int virtio_input_init(void) {
  memset(&g_in, 0, sizeof(g_in));

  for (int nth = 0; nth < INPUT_MAX_DEV; nth++) {
    InputDev *d = &g_in.devs[g_in.dev_count];
    if (virtio_mmio_find_device(VIRTIO_ID_INPUT, nth, &d->dev) < 0) break;
    if (virtio_mmio_init_queue(&d->dev, VIRTIO_INPUT_QUEUE_EVENT, INPUT_NUM_DESC) < 0) continue;

    for (int i = 0; i < INPUT_NUM_DESC; i++) {
      input_post_buffer(d, i);
    }
    virtio_mmio_write32(d->dev.base, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_INPUT_QUEUE_EVENT);
    virtio_mmio_set_driver_ok(&d->dev);
    d->ready = 1;
    g_in.dev_count++;
    printf("  VirtIO input initialized (irq=%d, idx=%d)\n", d->dev.irq, nth);
  }

  if (g_in.dev_count == 0) {
    printf("  VirtIO input not found\n");
    return -1;
  }
  return 0;
}

void virtio_input_poll(void) {
  for (int i = 0; i < g_in.dev_count; i++) {
    input_poll_one(&g_in.devs[i]);
  }
}

int virtio_input_get_event(VirtioInputEvent *ev) {
  if (g_in.q_head == g_in.q_tail) return 0;
  *ev = g_in.q[g_in.q_head];
  g_in.q_head = (g_in.q_head + 1) % (int)(sizeof(g_in.q) / sizeof(g_in.q[0]));
  return 1;
}

int virtio_input_match_irq(int irq) {
  for (int i = 0; i < g_in.dev_count; i++) {
    if (g_in.devs[i].ready && irq == (int)g_in.devs[i].dev.irq) return 1;
  }
  return 0;
}

void virtio_input_handle_irq(void) {
  virtio_input_poll();
}
