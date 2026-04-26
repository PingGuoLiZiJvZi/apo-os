#ifndef __VIRTIO_GPU_H__
#define __VIRTIO_GPU_H__

#include <stdint.h>

int virtio_gpu_init(void);
int virtio_gpu_is_ready(void);
int virtio_gpu_resolution(int *w, int *h);
int virtio_gpu_fbdraw(int x, int y, int w, int h, const uint32_t *pixels, int sync);
int virtio_gpu_fbwrite(int x, int y, int w, int h, const uint32_t *pixels);
int virtio_gpu_fbsync(void);
void virtio_gpu_poll(void);
int virtio_gpu_match_irq(int irq);
void virtio_gpu_handle_irq(void);

#endif
