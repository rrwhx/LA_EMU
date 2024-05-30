/*
 * QEMU 16550A UART emulation
 *
 * Copyright (c) 2003-2004 Fabrice Bellard
 * Copyright (c) 2008 Citrix Systems, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef HW_SERIAL_H
#define HW_SERIAL_H

#include "qemu/osdep.h"
#include "qemu/fifo8.h"
#include "irq.h"

#define hwaddr uint64_t

#define UART_FIFO_LENGTH    16      /* 16550A Fifo Length */

struct SerialState {
    int infd;
    int outfd;
    uint16_t divider;
    uint8_t rbr; /* receive register */
    uint8_t thr; /* transmit holding register */
    uint8_t tsr; /* transmit shift register */
    uint8_t ier;
    uint8_t iir; /* read only */
    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr; /* read only */
    uint8_t msr; /* read only */
    uint8_t scr;
    uint8_t fcr;
    uint8_t fcr_vmstate; /* we can't write directly this value
                            it has side effects */
    /* NOTE: this hidden state is necessary for tx irq generation as
       it can be reset while reading iir */
    int thr_ipending;
    int last_break_enable;
    int baudbase;
    uint32_t tsr_retry;
    guint watch_tag;
    uint32_t wakeup;

    /* Time when the last byte was successfully sent out of the tsr */
    uint64_t last_xmit_ts;
    Fifo8 recv_fifo;
    Fifo8 xmit_fifo;
    /* Interrupt trigger level for recv_fifo */
    uint8_t recv_fifo_itl;

    // QEMUTimer *fifo_timeout_timer;
    int timeout_ipending;           /* timeout interrupt pending state */

    uint64_t char_transmit_time;    /* time to transmit a char in ticks */
    int poll_msl;
    qemu_irq irq;

    // QEMUTimer *modem_status_poll;
    // MemoryRegion io;
};
typedef struct SerialState SerialState;

#define CHR_IOCTL_SERIAL_SET_PARAMS   1
typedef struct {
    int speed;
    int parity;
    int data_bits;
    int stop_bits;
} QEMUSerialSetParams;

#define CHR_IOCTL_SERIAL_SET_BREAK    2

#define CHR_IOCTL_SERIAL_SET_TIOCM   13
#define CHR_IOCTL_SERIAL_GET_TIOCM   14

#define CHR_TIOCM_CTS   0x020
#define CHR_TIOCM_CAR   0x040
#define CHR_TIOCM_DSR   0x100
#define CHR_TIOCM_RI    0x080
#define CHR_TIOCM_DTR   0x002
#define CHR_TIOCM_RTS   0x004



#define NANOSECONDS_PER_SECOND 1000000000LL

/* timers */

#define SCALE_MS 1000000
#define SCALE_US 1000
#define SCALE_NS 1

SerialState *simple_serial_init(int base, qemu_irq irq, int baudbase);
SerialState *simple_serial_restore(int base, qemu_irq irq, int baudbase, const char* filename);
uint64_t serial_plus_ioport_read(void *opaque, hwaddr addr, unsigned size);
void serial_plus_ioport_write(void *opaque, hwaddr addr, uint64_t val, unsigned size);
void serial_check_io(SerialState *s);

#endif
