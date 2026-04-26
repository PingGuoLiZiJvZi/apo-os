// VirtIO Block Device Driver (Legacy MMIO, polling)

#include "disk.h"
#include "../libc/stdio.h"
#include "../libc/string.h"
#include "../memory/memory.h"
#include "../system/system.h"

// VirtIO MMIO register offsets (legacy v1)
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

// status bits
#define VIRTIO_STATUS_ACKNOWLEDGE   1
#define VIRTIO_STATUS_DRIVER        2
#define VIRTIO_STATUS_DRIVER_OK     4
#define VIRTIO_STATUS_FEATURES_OK   8

// block request types
#define VIRTIO_BLK_T_IN    0  // read
#define VIRTIO_BLK_T_OUT   1  // write

// QEMU virt: 8 VirtIO MMIO slots, 0x10001000..0x10008000, 0x1000 apart
#define VIRTIO_MMIO_BASE  0x10001000
#define VIRTIO_MMIO_SIZE  0x1000
#define VIRTIO_MMIO_COUNT 8

#define NUM_DESC 8

#define DISK_CACHE_BYTES  (64 * 1024)
#define DISK_CACHE_BLOCKS (DISK_CACHE_BYTES / BLOCK_SIZE)

// descriptor flags
#define VRING_DESC_F_NEXT     1  // buffer continues via 'next'
#define VRING_DESC_F_WRITE    2  // device-writable

struct VirtqDesc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct VirtqAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[NUM_DESC];
};

struct VirtqUsedElem {
    uint32_t id;
    uint32_t len;
};

struct VirtqUsed {
    uint16_t flags;
    uint16_t idx;
    struct VirtqUsedElem ring[NUM_DESC];
};

struct VirtioBlkReqHdr {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

// driver state
static struct {
    struct VirtqDesc  *desc;
    struct VirtqAvail *avail;
    struct VirtqUsed  *used;
    char free[NUM_DESC];   // 1 = free
    uint16_t used_idx;
} disk;

static uint64_t virtio_base;

typedef struct {
    int valid;
    int dirty;
    size_t block_no;
    uint64_t last_used;
    uint8_t data[BLOCK_SIZE];
} DiskCacheBlock;

static DiskCacheBlock block_cache[DISK_CACHE_BLOCKS];
static uint64_t block_cache_clock;

static inline void     mmio_w32(uint64_t off, uint32_t v) { *(volatile uint32_t *)(virtio_base + off) = v; }
static inline uint32_t mmio_r32(uint64_t off)             { return *(volatile uint32_t *)(virtio_base + off); }

static int alloc_desc(void)
{
    for (int i = 0; i < NUM_DESC; i++) {
        if (disk.free[i]) {
            disk.free[i] = 0;
            return i;
        }
    }
    return -1;
}

static void disk_cache_init(void)
{
    memset(block_cache, 0, sizeof(block_cache));
    block_cache_clock = 0;
}

static void free_desc(int i)
{
    if (i < 0 || i >= NUM_DESC)
        panic("free_desc: out of range");
    disk.free[i] = 1;
}

void init_disk(void)
{
    printf("Initializing VirtIO block device...\n");

    // scan MMIO slots for block device (device-id 2)
    virtio_base = 0;
    for (int i = 0; i < VIRTIO_MMIO_COUNT; i++) {
        uint64_t base = VIRTIO_MMIO_BASE + (uint64_t)i * VIRTIO_MMIO_SIZE;
        uint32_t magic = *(volatile uint32_t *)(base + VIRTIO_MMIO_MAGIC_VALUE);
        uint32_t devid = *(volatile uint32_t *)(base + VIRTIO_MMIO_DEVICE_ID);
        if (magic == 0x74726976 && devid == 2) {
            virtio_base = base;
            break;
        }
    }
    if (virtio_base == 0)
        panic("virtio disk: no block device found");

    uint32_t magic   = mmio_r32(VIRTIO_MMIO_MAGIC_VALUE);
    uint32_t version = mmio_r32(VIRTIO_MMIO_VERSION);
    printf("  Found at 0x%lx  Magic=0x%x  Version=%d  DeviceID=%d  VendorID=0x%x\n",
           virtio_base, magic, version,
           mmio_r32(VIRTIO_MMIO_DEVICE_ID),
           mmio_r32(VIRTIO_MMIO_VENDOR_ID));

    if (version != 1 && version != 2)
        panic("virtio disk: unsupported version");

    // reset
    mmio_w32(VIRTIO_MMIO_STATUS, 0);

    // negotiate
    uint32_t status = 0;
    status |= VIRTIO_STATUS_ACKNOWLEDGE;
    mmio_w32(VIRTIO_MMIO_STATUS, status);

    status |= VIRTIO_STATUS_DRIVER;
    mmio_w32(VIRTIO_MMIO_STATUS, status);

    mmio_w32(VIRTIO_MMIO_GUEST_FEATURES, 0);

    status |= VIRTIO_STATUS_FEATURES_OK;
    mmio_w32(VIRTIO_MMIO_STATUS, status);

    // set up virtqueue 0
    mmio_w32(VIRTIO_MMIO_QUEUE_SEL, 0);

    uint32_t max = mmio_r32(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0)
        panic("virtio disk: queue 0 not available");
    if (max < NUM_DESC)
        panic("virtio disk: queue too small");
    mmio_w32(VIRTIO_MMIO_QUEUE_NUM, NUM_DESC);

    mmio_w32(VIRTIO_MMIO_GUEST_PAGE_SIZE, PAGE_SIZE);

    // use QUEUE_ALIGN=256 so desc + avail + used all fit in one page
    #define VRING_ALIGN 256
    mmio_w32(VIRTIO_MMIO_QUEUE_ALIGN, VRING_ALIGN);

    void *buf = kalloc();
    if (!buf)
        panic("virtio disk: kalloc for virtqueue");
    memset(buf, 0, PAGE_SIZE);

    disk.desc  = (struct VirtqDesc *)buf;
    disk.avail = (struct VirtqAvail *)((char *)buf + NUM_DESC * sizeof(struct VirtqDesc));

    // used ring starts at next VRING_ALIGN boundary
    uint64_t avail_end = (uint64_t)disk.avail + sizeof(struct VirtqAvail);
    uint64_t used_off  = (avail_end - (uint64_t)buf + VRING_ALIGN - 1) & ~(uint64_t)(VRING_ALIGN - 1);
    disk.used  = (struct VirtqUsed *)((char *)buf + used_off);

    mmio_w32(VIRTIO_MMIO_QUEUE_PFN, (uint32_t)((uint64_t)buf / PAGE_SIZE));

    for (int i = 0; i < NUM_DESC; i++)
        disk.free[i] = 1;
    disk.used_idx = 0;

    // device is live
    status |= VIRTIO_STATUS_DRIVER_OK;
    mmio_w32(VIRTIO_MMIO_STATUS, status);

    printf("  VirtIO block device initialized (queue_num=%d, page=0x%lx)\n",
           NUM_DESC, (uint64_t)buf);

    disk_cache_init();
}

// request header & status (single-threaded, one set is enough)
static struct VirtioBlkReqHdr req_hdr;
static uint8_t                req_status;

static size_t virtio_blk_rw(size_t block_no, void *buf, int write)
{
    req_hdr.type     = write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req_hdr.reserved = 0;
    req_hdr.sector   = block_no;
    req_status = 0xFF;

    int d0 = alloc_desc();
    int d1 = alloc_desc();
    int d2 = alloc_desc();
    if (d0 < 0 || d1 < 0 || d2 < 0)
        panic("virtio_blk_rw: no free descriptors");

    // desc 0: request header (device-readable)
    disk.desc[d0].addr  = (uint64_t)&req_hdr;
    disk.desc[d0].len   = sizeof(req_hdr);
    disk.desc[d0].flags = VRING_DESC_F_NEXT;
    disk.desc[d0].next  = d1;

    // desc 1: data buffer
    disk.desc[d1].addr  = (uint64_t)buf;
    disk.desc[d1].len   = BLOCK_SIZE;
    disk.desc[d1].flags = VRING_DESC_F_NEXT | (write ? 0 : VRING_DESC_F_WRITE);
    disk.desc[d1].next  = d2;

    // desc 2: status byte (device-writable)
    disk.desc[d2].addr  = (uint64_t)&req_status;
    disk.desc[d2].len   = 1;
    disk.desc[d2].flags = VRING_DESC_F_WRITE;
    disk.desc[d2].next  = 0;

    // push into available ring
    disk.avail->ring[disk.avail->idx % NUM_DESC] = d0;
    __sync_synchronize();
    disk.avail->idx += 1;
    __sync_synchronize();

    // notify device
    mmio_w32(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    // poll for completion
    while (disk.used->idx == disk.used_idx)
        __sync_synchronize();
    disk.used_idx += 1;

    free_desc(d0);
    free_desc(d1);
    free_desc(d2);

    // ack interrupt if pending
    uint32_t intr = mmio_r32(VIRTIO_MMIO_INTERRUPT_STATUS);
    if (intr)
        mmio_w32(VIRTIO_MMIO_INTERRUPT_ACK, intr);

    if (req_status != 0) {
        printf("virtio_blk_rw: device returned status %d\n", req_status);
        return 0;
    }
    return BLOCK_SIZE;
}

static DiskCacheBlock *cache_lookup(size_t block_no)
{
    for (int i = 0; i < DISK_CACHE_BLOCKS; i++) {
        if (block_cache[i].valid && block_cache[i].block_no == block_no) {
            return &block_cache[i];
        }
    }
    return 0;
}

static void cache_touch(DiskCacheBlock *entry)
{
    entry->last_used = ++block_cache_clock;
}

static void cache_flush_entry(DiskCacheBlock *entry)
{
    if (!entry->valid || !entry->dirty) {
        return;
    }
    if (virtio_blk_rw(entry->block_no, entry->data, 1) != BLOCK_SIZE) {
        panic("disk cache: flush failed");
    }
    entry->dirty = 0;
}

static DiskCacheBlock *cache_alloc_entry(void)
{
    DiskCacheBlock *victim = 0;

    for (int i = 0; i < DISK_CACHE_BLOCKS; i++) {
        if (!block_cache[i].valid) {
            return &block_cache[i];
        }
        if (victim == 0 || block_cache[i].last_used < victim->last_used) {
            victim = &block_cache[i];
        }
    }

    cache_flush_entry(victim);
    victim->valid = 0;
    victim->dirty = 0;
    return victim;
}

static DiskCacheBlock *cache_load_entry(size_t block_no)
{
    DiskCacheBlock *entry = cache_lookup(block_no);
    if (entry) {
        cache_touch(entry);
        return entry;
    }

    entry = cache_alloc_entry();
    if (virtio_blk_rw(block_no, entry->data, 0) != BLOCK_SIZE) {
        entry->valid = 0;
        entry->dirty = 0;
        return 0;
    }

    entry->valid = 1;
    entry->dirty = 0;
    entry->block_no = block_no;
    cache_touch(entry);
    return entry;
}

size_t disk_read(size_t block_no, void *buf)
{
    DiskCacheBlock *entry = cache_load_entry(block_no);
    if (!entry) {
        return 0;
    }
    memcpy(buf, entry->data, BLOCK_SIZE);
    return BLOCK_SIZE;
}

size_t disk_write(size_t block_no, void *buf)
{
    DiskCacheBlock *entry = cache_lookup(block_no);
    if (!entry) {
        entry = cache_alloc_entry();
        entry->valid = 1;
        entry->block_no = block_no;
    }

    memcpy(entry->data, buf, BLOCK_SIZE);
    entry->dirty = 1;
    cache_touch(entry);
    return BLOCK_SIZE;
}

void disk_flush(void)
{
    for (int i = 0; i < DISK_CACHE_BLOCKS; i++) {
        cache_flush_entry(&block_cache[i]);
    }
}
void disk_test()    
{
    static uint8_t wbuf[512];
    static uint8_t rbuf[512];
    for (int i = 0; i < 512; i++) wbuf[i] = (uint8_t)(i & 0xFF);
    printf("[TEST] Writing 512 bytes to block 0...\n");
    disk_write(0, wbuf);
    printf("[TEST] Reading 512 bytes from block 0...\n");
    disk_read(0, rbuf);
    int ok = 1;
    for (int i = 0; i < 512; i++) {
        if (rbuf[i] != wbuf[i]) { ok = 0; break; }
    }
    printf("[TEST] Block I/O: %s\n", ok ? "PASS" : "FAIL");
}
