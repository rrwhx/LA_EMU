CC=gcc
# CFLAGS ?= -g -O3 -flto=auto -march=native -mtune=native -MMD -MP -I. -Iinclude -DCONFIG_INT128
CFLAGS ?= -g -O2 -MMD -MP -I. -Iinclude -DCONFIG_INT128
LDFLAGS = -lm
BUILD_DIR := ./build
SRC_DIRS := ./

USER_SOURCES := fpu_helper.c  host-utils.c  int128.c  interpreter.c  main.c  softfloat.c  tlb_helper.c  vec_helper.c syscall.c
USER_OBJS := $(addprefix $(BUILD_DIR)/, $(patsubst %.c,%_user.o,$(USER_SOURCES)))
USER_DEPS := $(USER_OBJS:.o=.d)

KERNEL_SOURCES := fpu_helper.c  host-utils.c  int128.c  interpreter.c  main.c  softfloat.c  tlb_helper.c  vec_helper.c
KERNEL_OBJS := $(addprefix $(BUILD_DIR)/, $(patsubst %.c,%_kernel.o,$(KERNEL_SOURCES)))
KERNEL_DEPS := $(KERNEL_OBJS:.o=.d)

$(info $$USER_SOURCES is [${USER_SOURCES}])
$(info $$USER_OBJS is [${USER_OBJS}])
$(info $$USER_DEPS is [${USER_DEPS}])

$(info $$KERNEL_SOURCES is [${KERNEL_SOURCES}])
$(info $$KERNEL_OBJS is [${KERNEL_OBJS}])
$(info $$KERNEL_DEPS is [${KERNEL_DEPS}])

all: $(BUILD_DIR)/la_emu_user $(BUILD_DIR)/la_emu_kernel

$(BUILD_DIR)/la_emu_user : ${USER_OBJS}
	$(CC) $(USER_OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%_user.o : %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DUSER_MODE=1 -c -o $@ $<

$(BUILD_DIR)/la_emu_kernel : ${KERNEL_OBJS}
	$(CC) $(KERNEL_OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%_kernel.o : %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf build

.EXTRA_PREREQS = Makefile
-include $(USER_DEPS)
-include $(KERNEL_DEPS)
