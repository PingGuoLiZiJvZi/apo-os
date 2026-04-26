#ifndef __DEVICE_H__
#define __DEVICE_H__

#include <stdint.h>
#include <stddef.h>

#define UART0_BASE      0x10000000UL
#define UART0_IRQ       10
#define VIRTIO0_IRQ     1
#define VIRTIO_IRQ_MIN  1
#define VIRTIO_IRQ_MAX  8

#define UART_RHR        0   // Receive Holding Register (read)  
#define UART_THR        0   // Transmit Holding Register (write) 
#define UART_IER        1   // Interrupt Enable Register 
#define UART_FCR        2   // FIFO Control Register (write) 
#define UART_ISR        2   // Interrupt Status Register (read) 
#define UART_LCR        3   // Line Control Register 
#define UART_MCR        4   // Modem Control Register 
#define UART_LSR        5   // Line Status Register 

#define UART_LCR_8BIT   0x03 // 8-bit word length 
#define UART_LCR_DLAB   0x80 // Divisor Latch Access Bit 

#define UART_LSR_RX_READY  0x01 // Data ready 
#define UART_LSR_TX_IDLE   0x20 // THR empty 

#define UART_IER_RX_ENABLE 0x01 // Enable receive interrupt 

#define UART_FCR_FIFO_ENABLE  0x01
#define UART_FCR_FIFO_CLEAR   0x06

void uart_init();
void uart_putchar(char c);
int  uart_getchar();

#define PLIC_BASE       0x0C000000UL
#define PLIC_PRIORITY(src)           (PLIC_BASE + (src) * 4)
#define PLIC_SENABLE(hart)           (PLIC_BASE + 0x2080 + (hart) * 0x100)
#define PLIC_SPRIORITY(hart)         (PLIC_BASE + 0x201000 + (hart) * 0x2000)
#define PLIC_SCLAIM(hart)            (PLIC_BASE + 0x201004 + (hart) * 0x2000)

void plic_init();
int  plic_claim();
void plic_complete(int irq);

#define TIMER_INTERVAL  10000000UL  
void timer_init();
void timer_set_next();
uint64_t timer_get_time();

int device_fs_read(const char *name, uint32_t *off, void *buf, size_t n);
int device_fs_write(const char *name, uint32_t *off, const void *buf, size_t n);
uint64_t device_mmap_size(const char *name);
int device_mmap_page(const char *name, uint64_t offset, uint64_t *pa);

void init_device();
void device_poll();
int device_handle_irq(int irq);

#endif /* __DEVICE_H__ */
