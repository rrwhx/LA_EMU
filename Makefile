CC=gcc
CFLAGS ?= -g -O2 -MMD -MP -I. -Iinclude -DCONFIG_INT128
LDFLAGS = -lm
BUILD_DIR := ./build
SRC_DIRS := ./

SOURCES := fpu_helper.c  host-utils.c  int128.c  interpreter.c  main.c  softfloat.c  tlb_helper.c  vec_helper.c
OBJS := $(addprefix $(BUILD_DIR)/, $(patsubst %.c,%.o,$(SOURCES)))
DEPS := $(OBJS:.o=.d)

$(info $$SOURCES is [${SOURCES}])
$(info $$OBJS is [${OBJS}])
$(info $$DEPS is [${DEPS}])

all: $(BUILD_DIR)/la_emu

$(BUILD_DIR)/la_emu : ${OBJS}
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o : %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -rf build