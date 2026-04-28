#ifndef __VIRTIO_GPU_H__
#define __VIRTIO_GPU_H__

#include <stdint.h>
#include <stddef.h>

typedef struct
{
  int32_t x;
  int32_t y;
  int32_t w;
  int32_t h;
} GpuDirtyRect;

int virtio_gpu_init(void);
int virtio_gpu_is_ready(void);
int virtio_gpu_resolution(int *w, int *h);
uint64_t virtio_gpu_fb_size(void);
int virtio_gpu_fb_page(uint64_t offset, uint64_t *pa);
int virtio_gpu_fbdraw(int x, int y, int w, int h, const uint32_t *pixels, int sync);
int virtio_gpu_fbwrite(int x, int y, int w, int h, const uint32_t *pixels);
int virtio_gpu_fbdirty(int x, int y, int w, int h);
int virtio_gpu_fbdirty_rects(const GpuDirtyRect *rects, size_t count);
int virtio_gpu_fbsync(void);
void virtio_gpu_poll(void);
int virtio_gpu_match_irq(int irq);
void virtio_gpu_handle_irq(void);

#endif
