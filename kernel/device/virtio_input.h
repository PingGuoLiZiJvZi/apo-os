#ifndef __VIRTIO_INPUT_H__
#define __VIRTIO_INPUT_H__

#include <stdint.h>

typedef struct {
  uint16_t type;
  uint16_t code;
  uint32_t value;
} VirtioInputEvent;

int virtio_input_init(void);
void virtio_input_poll(void);
int virtio_input_get_event(VirtioInputEvent *ev);
int virtio_input_get_event_filtered(int include_pointer, int include_keyboard, VirtioInputEvent *ev);
int virtio_input_match_irq(int irq);
void virtio_input_handle_irq(void);

#endif
