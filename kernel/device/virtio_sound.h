#ifndef __VIRTIO_SOUND_H__
#define __VIRTIO_SOUND_H__

#include <stddef.h>

int virtio_sound_init(void);
int virtio_sound_is_ready(void);
int virtio_sound_write(const void *buf, size_t n);
void virtio_sound_poll(void);
int virtio_sound_match_irq(int irq);
void virtio_sound_handle_irq(void);

#endif
