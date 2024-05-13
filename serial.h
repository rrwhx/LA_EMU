#ifndef __SERIAL_H__
#define __SERIAL_H__

uint64_t serial_ioport_read(void *opaque, long addr, unsigned size);
void serial_ioport_write(void *opaque, long addr, uint64_t val, unsigned size);

#endif
