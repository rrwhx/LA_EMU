#ifndef QEMU_IRQ_H
#define QEMU_IRQ_H

#include <stdlib.h>

typedef void (*qemu_irq_handler)(void *opaque, int n, int level);
typedef struct IRQState {
    // Object parent_obj;

    qemu_irq_handler handler;
    void *opaque;
    int n;
}IRQState;
typedef struct IRQState *qemu_irq;

static inline qemu_irq qemu_allocate_irq(qemu_irq_handler handler, void *opaque, int n) {
    struct IRQState *irq;

    irq = (IRQState *)malloc(sizeof(IRQState));
    irq->handler = handler;
    irq->opaque = opaque;
    irq->n = n;

    return irq;
}

static inline void qemu_free_irq(qemu_irq irq) {
    free(irq);
}

static inline void qemu_set_irq(qemu_irq irq, int level)
{
    if (!irq)
        return;

    irq->handler(irq->opaque, irq->n, level);
}

static inline void qemu_irq_raise(qemu_irq irq)
{
    qemu_set_irq(irq, 1);
}

static inline void qemu_irq_lower(qemu_irq irq)
{
    qemu_set_irq(irq, 0);
}

#endif