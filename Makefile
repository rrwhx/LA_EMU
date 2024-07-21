CC=gcc
OPT_FLAG = -O2 -flto=auto
ifeq (${DEBUG},1)
	OPT_FLAG = -Og
endif
# CFLAGS ?= -g -O3 -flto=auto -march=native -mtune=native -MMD -MP -I. -Iinclude -DCONFIG_INT128
CFLAGS ?= -g ${OPT_FLAG} -MMD -MP -I. -Iinclude -Wall -Werror
ifeq (${GDB},1)
	CFLAGS += -DCONFIG_GDB
	GDB_SOURCES := gdbserver.c
endif

ifeq (${CLI},1)
	CFLAGS += -DCONFIG_CLI
endif

ifeq (${DIFF},1)
	CFLAGS += -DCONFIG_DIFF -fPIC
endif

ifeq (${PERF},1)
	CFLAGS += -DCONFIG_PERF
endif

ifeq (${CORE},)
	CFLAGS += -D__CORE__=la464
else
	CFLAGS += -D__CORE__=$(CORE)
endif

LDFLAGS = -lm -lrt ${OPT_FLAG}
arch := $(shell gcc -dumpmachine)
ifeq ($(arch),loongarch64-linux-gnu)
   LDFLAGS+=-Wl,-Tlink_script/loongarch64.lds
endif
ifeq (${DIFF},1)
	LDFLAGS += -rdynamic -shared -fPIC -Wl,--no-undefined
endif

BUILD_DIR := ./build
SRC_DIRS := ./

USER_SOURCES := fpu_helper.c  host-utils.c  int128.c  interpreter.c  main.c  softfloat.c vec_helper.c tcg-runtime-gvec.c syscall.c ${GDB_SOURCES} debug_cli.c cpu.c checkpoint.c
USER_OBJS := $(addprefix $(BUILD_DIR)/, $(patsubst %.c,%_user.o,$(USER_SOURCES)))
USER_DEPS := $(USER_OBJS:.o=.d)

KERNEL_SOURCES := fpu_helper.c  host-utils.c  int128.c  interpreter.c  main.c  softfloat.c  tlb_helper.c cpu_helper.c vec_helper.c tcg-runtime-gvec.c serial.c serial_plus.c ${GDB_SOURCES} debug_cli.c cpu.c fifo.c checkpoint.c
KERNEL_OBJS := $(addprefix $(BUILD_DIR)/, $(patsubst %.c,%_kernel.o,$(KERNEL_SOURCES)))
KERNEL_DEPS := $(KERNEL_OBJS:.o=.d)

DIFF_SOURCES := $(KERNEL_SOURCES) difftest.c
DIFF_OBJS := $(addprefix $(BUILD_DIR)/, $(patsubst %.c,%_diff.o,$(DIFF_SOURCES)))
DIFF_DEPS := $(DIFF_OBJS:.o=.d)

$(info $$USER_SOURCES is [${USER_SOURCES}])
$(info $$USER_OBJS is [${USER_OBJS}])
$(info $$USER_DEPS is [${USER_DEPS}])

$(info $$KERNEL_SOURCES is [${KERNEL_SOURCES}])
$(info $$KERNEL_OBJS is [${KERNEL_OBJS}])
$(info $$KERNEL_DEPS is [${KERNEL_DEPS}])

$(info $$DIFF_SOURCES is [${DIFF_SOURCES}])
$(info $$DIFF_OBJS is [${DIFF_OBJS}])
$(info $$DIFF_DEPS is [${DIFF_DEPS}])

ifeq (${DIFF},1)
	TARGETS = $(BUILD_DIR)/la_emu_ref.so
else
	TARGETS = $(BUILD_DIR)/la_emu_user $(BUILD_DIR)/la_emu_kernel
endif

all: $(TARGETS)

$(BUILD_DIR)/la_emu_user : ${USER_OBJS}
	$(CC) $(USER_OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%_user.o : %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DCONFIG_USER_ONLY=1 -c -o $@ $<

$(BUILD_DIR)/la_emu_kernel : ${KERNEL_OBJS}
	$(CC) $(KERNEL_OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%_kernel.o : %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/la_emu_ref.so : ${DIFF_OBJS}
	$(CC) $(DIFF_OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%_diff.o : %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf build

.EXTRA_PREREQS = Makefile
-include $(USER_DEPS)
-include $(KERNEL_DEPS)
