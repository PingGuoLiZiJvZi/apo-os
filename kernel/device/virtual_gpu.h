#ifndef __VIRTUAL_GPU_H__
#define __VIRTUAL_GPU_H__

#include <stdint.h>
#include <stddef.h>

#define VIRTUAL_GPU_MAX_DISPLAYS 8
#define VIRTUAL_GPU_SHADOW_FB_MAX_PAGES 1024

typedef struct DisplayPCB DisplayPCB;

void virtual_gpu_init_displays(void);
DisplayPCB *virtual_gpu_display_pcb(int pid);
void virtual_gpu_reset_display(DisplayPCB *display);

int virtual_gpu_fbsync_read(void *buf, size_t n);
int virtual_gpu_fbsync_write(DisplayPCB *display, const void *buf, size_t n);
int virtual_gpu_fb_write(DisplayPCB *display, uint32_t *off, const void *buf, size_t n);
int virtual_gpu_fb_page(DisplayPCB *display, uint64_t offset, uint64_t *pa);
int virtual_gpu_existing_fb_page(DisplayPCB *display, uint64_t offset, uint64_t *pa);

#endif
