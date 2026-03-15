#ifndef __VIRTIO_H__
#define __VIRTIO_H__

#include <stdint.h>

#define VIRTIO_MMIO_MAGIC_VALUE         0x000
#define VIRTIO_MMIO_VERSION             0x004
#define VIRTIO_MMIO_DEVICE_ID           0x008
#define VIRTIO_MMIO_VENDOR_ID           0x00C
#define VIRTIO_MMIO_HOST_FEATURES       0x010
#define VIRTIO_MMIO_HOST_FEATURES_SEL   0x014
#define VIRTIO_MMIO_GUEST_FEATURES      0x020
#define VIRTIO_MMIO_GUEST_FEATURES_SEL  0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE     0x028
#define VIRTIO_MMIO_QUEUE_SEL           0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034
#define VIRTIO_MMIO_QUEUE_NUM           0x038
#define VIRTIO_MMIO_QUEUE_ALIGN         0x03C
#define VIRTIO_MMIO_QUEUE_PFN           0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064
#define VIRTIO_MMIO_STATUS              0x070
#define VIRTIO_MMIO_CONFIG              0x100

#define VIRTIO_STATUS_ACKNOWLEDGE   1
#define VIRTIO_STATUS_DRIVER        2
#define VIRTIO_STATUS_DRIVER_OK     4
#define VIRTIO_STATUS_FEATURES_OK   8

#define VIRTIO_MMIO_BASE  0x10001000UL
#define VIRTIO_MMIO_SIZE  0x1000UL
#define VIRTIO_MMIO_COUNT 8

#define VRING_ALIGN 256

struct VirtqDesc {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
};

struct VirtqAvail {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[];
};

struct VirtqUsedElem {
  uint32_t id;
  uint32_t len;
};

struct VirtqUsed {
  uint16_t flags;
  uint16_t idx;
  struct VirtqUsedElem ring[];
};

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

typedef struct {
  uint64_t base;
  uint32_t irq;
  uint32_t device_id;
  uint32_t version;
  uint16_t num_desc;
  uint16_t used_idx;
  struct VirtqDesc *desc;
  struct VirtqAvail *avail;
  struct VirtqUsed *used;
  uint8_t *free_desc;
} VirtioMMIODevice;

uint32_t virtio_mmio_read32(uint64_t base, uint32_t off);
void virtio_mmio_write32(uint64_t base, uint32_t off, uint32_t val);
int virtio_mmio_find_device(uint32_t device_id, int nth, VirtioMMIODevice *dev);
int virtio_mmio_init_queue(VirtioMMIODevice *dev, uint32_t queue_sel, uint16_t num_desc);
void virtio_mmio_set_driver_ok(VirtioMMIODevice *dev);
int virtio_alloc_desc(VirtioMMIODevice *dev);
void virtio_free_desc(VirtioMMIODevice *dev, int idx);
void virtio_submit_and_poll(VirtioMMIODevice *dev, int head_desc, uint32_t queue_sel);
void virtio_ack_interrupt(VirtioMMIODevice *dev);

#endif
