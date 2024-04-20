#include <stdio.h>
#include <stdint.h>
#include <assert.h>
// #include "util.h"

#define UART_LCR_DLAB   0x80    /* Divisor latch access bit */

#define UART_IER_MSI    0x08    /* Enable Modem status interrupt */
#define UART_IER_RLSI   0x04    /* Enable receiver line status interrupt */
#define UART_IER_THRI   0x02    /* Enable Transmitter holding register int. */
#define UART_IER_RDI    0x01    /* Enable receiver data interrupt */

#define UART_IIR_NO_INT 0x01    /* No interrupts pending */
#define UART_IIR_ID     0x06    /* Mask for the interrupt ID */

#define UART_IIR_MSI    0x00    /* Modem status interrupt */
#define UART_IIR_THRI   0x02    /* Transmitter holding register empty */
#define UART_IIR_RDI    0x04    /* Receiver data interrupt */
#define UART_IIR_RLSI   0x06    /* Receiver line status interrupt */
#define UART_IIR_CTI    0x0C    /* Character Timeout Indication */

#define UART_IIR_FENF   0x80    /* Fifo enabled, but not functioning */
#define UART_IIR_FE     0xC0    /* Fifo enabled */

/*
 * These are the definitions for the Modem Control Register
 */
#define UART_MCR_LOOP   0x10    /* Enable loopback test mode */
#define UART_MCR_OUT2   0x08    /* Out2 complement */
#define UART_MCR_OUT1   0x04    /* Out1 complement */
#define UART_MCR_RTS    0x02    /* RTS complement */
#define UART_MCR_DTR    0x01    /* DTR complement */

/*
 * These are the definitions for the Modem Status Register
 */
#define UART_MSR_DCD        0x80    /* Data Carrier Detect */
#define UART_MSR_RI         0x40    /* Ring Indicator */
#define UART_MSR_DSR        0x20    /* Data Set Ready */
#define UART_MSR_CTS        0x10    /* Clear to Send */
#define UART_MSR_DDCD       0x08    /* Delta DCD */
#define UART_MSR_TERI       0x04    /* Trailing edge ring indicator */
#define UART_MSR_DDSR       0x02    /* Delta DSR */
#define UART_MSR_DCTS       0x01    /* Delta CTS */
#define UART_MSR_ANY_DELTA  0x0F    /* Any of the delta bits! */

#define UART_LSR_TEMT       0x40    /* Transmitter empty */
#define UART_LSR_THRE       0x20    /* Transmit-hold-register empty */
#define UART_LSR_BI         0x10    /* Break interrupt indicator */
#define UART_LSR_FE         0x08    /* Frame error indicator */
#define UART_LSR_PE         0x04    /* Parity error indicator */
#define UART_LSR_OE         0x02    /* Overrun error indicator */
#define UART_LSR_DR         0x01    /* Receiver data ready */
#define UART_LSR_INT_ANY    0x1E    /* Any of the lsr-interrupt-triggering status bits */

/* Interrupt trigger levels. The byte-counts are for 16550A - in newer UARTs the byte-count for each ITL is higher. */

#define UART_FCR_ITL_1      0x00 /* 1 byte ITL */
#define UART_FCR_ITL_2      0x40 /* 4 bytes ITL */
#define UART_FCR_ITL_3      0x80 /* 8 bytes ITL */
#define UART_FCR_ITL_4      0xC0 /* 14 bytes ITL */

#define UART_FCR_DMS        0x08    /* DMA Mode Select */
#define UART_FCR_XFR        0x04    /* XMIT Fifo Reset */
#define UART_FCR_RFR        0x02    /* RCVR Fifo Reset */
#define UART_FCR_FE         0x01    /* FIFO Enable */

#define MAX_XMIT_RETRY      4

struct SerialState {
    // DeviceState parent;

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
    // qemu_irq irq;
    // CharBackend chr;
    int last_break_enable;
    // uint32_t baudbase;
    // uint32_t tsr_retry;
    // guint watch_tag;
    // bool wakeup;

    // /* Time when the last byte was successfully sent out of the tsr */
    // uint64_t last_xmit_ts;
    // Fifo8 recv_fifo;
    // Fifo8 xmit_fifo;
    // /* Interrupt trigger level for recv_fifo */
    // uint8_t recv_fifo_itl;

    // QEMUtimer_counter *fifo_timeout_timer_counter;
    // int timeout_ipending;           /* timeout interrupt pending state */

    // uint64_t char_transmit_time;    /* time to transmit a char in ticks */
    // int poll_msl;

    // QEMUtimer_counter *modem_status_poll;
    // MemoryRegion io;
};
typedef struct SerialState SerialState;

SerialState s;

uint64_t serial_ioport_read(void *opaque, long addr, unsigned size) {
    uint32_t ret = 0;
    switch (addr)
    {
    case 0: fprintf(stderr, "serial_ioport_read, addr:%lx, data:%x, size:%d\n", addr, ret, size); break;
    case 1:
        if (s.lcr & UART_LCR_DLAB) {
            ret = (s.divider >> 8) & 0Xff;
        } else {
            ret = s.ier;
        }
        break;
    case 2: 
        ret = UART_IIR_NO_INT;
        ;break;
    case 3: fprintf(stderr, "serial_ioport_read, addr:%lx, data:%x, size:%d\n", addr, ret, size);break;
    case 4: fprintf(stderr, "serial_ioport_read, addr:%lx, data:%x, size:%d\n", addr, ret, size);break;
    case 5: 
        ret = s.lsr;
        break;
    case 6: 
        ret = UART_MSR_DCD | UART_MSR_DSR | UART_MSR_CTS;
        break;
    case 7: fprintf(stderr, "serial_ioport_read, addr:%lx, data:%x, size:%d\n", addr, ret, size);break;
    default:
        assert(0);
        break;
    }
    // fprintf(stderr, "serial_ioport_read, addr:%lx, data:%x, size:%d\n", addr, ret, size);
    return ret;
}
void serial_ioport_write(void *opaque, long addr, uint64_t val, unsigned size) {
    switch (addr)
    {
    case 0:
        if (s.lcr & UART_LCR_DLAB) {
            s.divider = (s.divider & 0xff00) | (val & 0xff) ;
        } else {
            fprintf(stdout, "%c", (char)val);
            fflush(stdout);
            s.lsr |= (UART_LSR_TEMT | UART_LSR_THRE);
        }
        break;
    case 1: 
        if (s.lcr & UART_LCR_DLAB) {
            s.divider = (s.divider & 0xff) | ((val << 8) & 0xff) ;
        } else {
            uint8_t changed = (s.ier ^ val) & 0x0f;
            if (changed) {
                fprintf(stderr, "serial ier changed\n");
            }
            s.ier = val & 0x0f;
        }
        break;
    case 2:
        uint8_t changed = (s.fcr ^ val) & 0xff;
        if (changed) {
            fprintf(stderr, "serial ier changed\n");
        }
        if (val & UART_FCR_RFR) {
            fprintf(stderr, "serial fcr RCVR Fifo Reset\n");
        }
        if (val & UART_FCR_XFR) {
            fprintf(stderr, "serial fcr XMIT Fifo Reset\n");
        }
        s.fcr = val & 0xC9;
        break;
    case 3:
        s.lcr = val;
        s.last_break_enable = (val >> 6) & 1;
        break;
    case 4: {
            int old_mcr = s.mcr;
            s.mcr = val & 0x1f;
            if (val & UART_MCR_LOOP)
                break;
        }
        break;
    case 5: fprintf(stderr, "serial_ioport_write, addr:%lx, data:%lx, size:%d\n", addr, val, size);break;
    case 6: fprintf(stderr, "serial_ioport_write, addr:%lx, data:%lx, size:%d\n", addr, val, size);break;
    case 7: fprintf(stderr, "serial_ioport_write, addr:%lx, data:%lx, size:%d\n", addr, val, size);break;
    default:
        assert(0);
        break;
    }
    // fprintf(stderr, "serial_ioport_write, addr:%lx, data:%lx, size:%d\n", addr, val, size);
}