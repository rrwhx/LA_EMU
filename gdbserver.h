#ifndef __GDBSERVER_H__
#define __GDBSERVER_H__

#include <stdint.h>
extern volatile int gdbserver_has_message;
void gdbserver_init(int port);
int gdbserver_loop(void);

#define GDB_FETCCH_BREAKPOINT_NUM 4
extern uint64_t gdb_fetch_breakpoint[GDB_FETCCH_BREAKPOINT_NUM];

#endif
