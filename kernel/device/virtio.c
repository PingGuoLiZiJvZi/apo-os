#include "virtio.h"

#include "../libc/string.h"
#include "../memory/memory.h"
#include "../system/system.h"

uint32_t virtio_mmio_read32(uint64_t base, uint32_t off) {
  return *(volatile uint32_t *)(base + off);
}

void virtio_mmio_write32(uint64_t base, uint32_t off, uint32_t val) {
  *(volatile uint32_t *)(base + off) = val;
}

int virtio_mmio_find_device(uint32_t device_id, int nth, VirtioMMIODevice *dev) {
  int found = 0;
  for (int i = 0; i < VIRTIO_MMIO_COUNT; i++) {
    uint64_t base = VIRTIO_MMIO_BASE + (uint64_t)i * VIRTIO_MMIO_SIZE;
    uint32_t magic = virtio_mmio_read32(base, VIRTIO_MMIO_MAGIC_VALUE);
    uint32_t devid = virtio_mmio_read32(base, VIRTIO_MMIO_DEVICE_ID);
    if (magic != 0x74726976 || devid != device_id) continue;
    if (found++ != nth) continue;

    memset(dev, 0, sizeof(*dev));
    dev->base = base;
    dev->irq = 1 + (uint32_t)i;
    dev->device_id = devid;
    dev->version = virtio_mmio_read32(base, VIRTIO_MMIO_VERSION);
    return 0;
  }
  return -1;
}

int virtio_mmio_init_queue(VirtioMMIODevice *dev, uint32_t queue_sel, uint16_t num_desc) {
  if (!dev) return -1;

  virtio_mmio_write32(dev->base, VIRTIO_MMIO_STATUS, 0);

  uint32_t status = 0;
  status |= VIRTIO_STATUS_ACKNOWLEDGE;
  virtio_mmio_write32(dev->base, VIRTIO_MMIO_STATUS, status);
  status |= VIRTIO_STATUS_DRIVER;
  virtio_mmio_write32(dev->base, VIRTIO_MMIO_STATUS, status);

  virtio_mmio_write32(dev->base, VIRTIO_MMIO_GUEST_FEATURES_SEL, 0);
  virtio_mmio_write32(dev->base, VIRTIO_MMIO_GUEST_FEATURES, 0);

  status |= VIRTIO_STATUS_FEATURES_OK;
  virtio_mmio_write32(dev->base, VIRTIO_MMIO_STATUS, status);

  virtio_mmio_write32(dev->base, VIRTIO_MMIO_QUEUE_SEL, queue_sel);
  uint32_t max = virtio_mmio_read32(dev->base, VIRTIO_MMIO_QUEUE_NUM_MAX);
  if (max == 0 || max < num_desc) return -1;

  virtio_mmio_write32(dev->base, VIRTIO_MMIO_QUEUE_NUM, num_desc);
  virtio_mmio_write32(dev->base, VIRTIO_MMIO_GUEST_PAGE_SIZE, PAGE_SIZE);
  virtio_mmio_write32(dev->base, VIRTIO_MMIO_QUEUE_ALIGN, VRING_ALIGN);

  void *page = kalloc();
  if (!page) return -1;
  memset(page, 0, PAGE_SIZE);

  dev->desc = (struct VirtqDesc *)page;
  dev->avail = (struct VirtqAvail *)((char *)page + num_desc * sizeof(struct VirtqDesc));

  uint64_t avail_end = (uint64_t)dev->avail + sizeof(uint16_t) * 2 + sizeof(uint16_t) * num_desc;
  uint64_t used_off = (avail_end - (uint64_t)page + VRING_ALIGN - 1) & ~(uint64_t)(VRING_ALIGN - 1);
  dev->used = (struct VirtqUsed *)((char *)page + used_off);
  dev->free_desc = (uint8_t *)kalloc();
  if (!dev->free_desc) return -1;
  memset(dev->free_desc, 0, PAGE_SIZE);

  dev->num_desc = num_desc;
  dev->used_idx = 0;
  for (uint16_t i = 0; i < num_desc; i++) dev->free_desc[i] = 1;

  virtio_mmio_write32(dev->base, VIRTIO_MMIO_QUEUE_PFN, (uint32_t)((uint64_t)page / PAGE_SIZE));
  return 0;
}

void virtio_mmio_set_driver_ok(VirtioMMIODevice *dev) {
  uint32_t status = virtio_mmio_read32(dev->base, VIRTIO_MMIO_STATUS);
  status |= VIRTIO_STATUS_DRIVER_OK;
  virtio_mmio_write32(dev->base, VIRTIO_MMIO_STATUS, status);
}

int virtio_alloc_desc(VirtioMMIODevice *dev) {
  for (uint16_t i = 0; i < dev->num_desc; i++) {
    if (dev->free_desc[i]) {
      dev->free_desc[i] = 0;
      return (int)i;
    }
  }
  return -1;
}

void virtio_free_desc(VirtioMMIODevice *dev, int idx) {
  if (idx < 0 || idx >= dev->num_desc) panic("virtio_free_desc: bad index");
  dev->free_desc[idx] = 1;
}

void virtio_submit_and_poll(VirtioMMIODevice *dev, int head_desc, uint32_t queue_sel) {
  uint16_t slot = dev->avail->idx % dev->num_desc;
  dev->avail->ring[slot] = (uint16_t)head_desc;
  __sync_synchronize();
  dev->avail->idx++;
  __sync_synchronize();

  virtio_mmio_write32(dev->base, VIRTIO_MMIO_QUEUE_NOTIFY, queue_sel);
  while (dev->used->idx == dev->used_idx) {
    __sync_synchronize();
  }
  dev->used_idx++;
}

void virtio_ack_interrupt(VirtioMMIODevice *dev) {
  uint32_t st = virtio_mmio_read32(dev->base, VIRTIO_MMIO_INTERRUPT_STATUS);
  if (st) virtio_mmio_write32(dev->base, VIRTIO_MMIO_INTERRUPT_ACK, st);
}
