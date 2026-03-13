# ============================================================
# usr/Makefile — Build user libraries (libos, libc) and all apps
# ============================================================

CROSS_COMPILE ?= riscv64-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
AR      := $(CROSS_COMPILE)ar
LD      := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump

# Directories
USR_DIR    := $(shell pwd)
LIBS_DIR   := $(USR_DIR)/libs
APPS_DIR   := $(USR_DIR)/apps
BUILD_DIR  := $(USR_DIR)/build
DISK_BIN   := $(USR_DIR)/disk/bin
LINKER_LD  := $(APPS_DIR)/user-linker.ld

# libc paths
LIBC_DIR     := $(LIBS_DIR)/libc
LIBC_INC     := $(LIBC_DIR)/include
LIBC_SRC     := $(LIBC_DIR)/src
LIBC_BUILD   := $(BUILD_DIR)/libc
LIBC_ARCHIVE := $(BUILD_DIR)/libc.a

# libos paths
LIBOS_DIR     := $(LIBS_DIR)/libos
LIBOS_SRC     := $(LIBOS_DIR)/src
LIBOS_BUILD   := $(BUILD_DIR)/libos
LIBOS_ARCHIVE := $(BUILD_DIR)/libos.a

# Common compiler flags
CFLAGS := -Wall -O2 -ffreestanding -nostdlib \
          -march=rv64gc -mabi=lp64d -mcmodel=medany \
          -I$(LIBC_INC) \
          -DNO_FLOATING_POINT -DHAVE_INITFINI_ARRAY \
          -U_FORTIFY_SOURCE -fno-stack-protector

# ============================================================
# libc: collect all .c and .S sources (exclude .cpp and .t)
# ============================================================
LIBC_C_SRCS := $(shell find $(LIBC_SRC) -name '*.c')
LIBC_S_SRCS := $(shell find $(LIBC_SRC) -name '*.S' \
                 ! -name 'amd64.S' ! -name 'x86.S' \
                 ! -name 'mips32.S' ! -name 'loonarch32r.S')

LIBC_C_OBJS := $(patsubst $(LIBC_SRC)/%, $(LIBC_BUILD)/%, $(LIBC_C_SRCS:.c=.o))
LIBC_S_OBJS := $(patsubst $(LIBC_SRC)/%, $(LIBC_BUILD)/%, $(LIBC_S_SRCS:.S=.o))
LIBC_OBJS   := $(LIBC_C_OBJS) $(LIBC_S_OBJS)

# ============================================================
# libos: explicit sources (exclude native.cpp)
# ============================================================
LIBOS_C_SRCS := $(LIBOS_SRC)/syscall.c $(LIBOS_SRC)/crt0/crt0.c
LIBOS_S_SRCS := $(LIBOS_SRC)/crt0/start.S

LIBOS_C_OBJS := $(patsubst $(LIBOS_SRC)/%, $(LIBOS_BUILD)/%, $(LIBOS_C_SRCS:.c=.o))
LIBOS_S_OBJS := $(patsubst $(LIBOS_SRC)/%, $(LIBOS_BUILD)/%, $(LIBOS_S_SRCS:.S=.o))
LIBOS_OBJS   := $(LIBOS_C_OBJS) $(LIBOS_S_OBJS)

# ============================================================
# Apps: each subdirectory under apps/ with a .c file
# ============================================================
APP_DIRS := $(shell find $(APPS_DIR) -mindepth 1 -maxdepth 1 -type d)
APP_NAMES := $(notdir $(APP_DIRS))
APP_ELFS  := $(foreach name,$(APP_NAMES),$(BUILD_DIR)/apps/$(name)/$(name).elf)
APP_BINS  := $(foreach name,$(APP_NAMES),$(DISK_BIN)/$(name))

# ============================================================
# Targets
# ============================================================
.PHONY: all clean libs apps

all: libs apps

libs: $(LIBC_ARCHIVE) $(LIBOS_ARCHIVE)

apps: libs $(APP_BINS)

# ---------- libc ----------

$(LIBC_BUILD)/%.o: $(LIBC_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBC_BUILD)/%.o: $(LIBC_SRC)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBC_ARCHIVE): $(LIBC_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^
	@echo "  -> $@ ($(words $^) objects)"

# ---------- libos ----------

$(LIBOS_BUILD)/%.o: $(LIBOS_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBOS_BUILD)/%.o: $(LIBOS_SRC)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBOS_ARCHIVE): $(LIBOS_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^
	@echo "  -> $@ ($(words $^) objects)"

# ---------- apps ----------
# Generic rule: for each app, compile its .c, link with libos + libc,
# then strip to binary and place in disk/bin/

define BUILD_APP
# $(1) = app name (e.g. hello-apple)
$(BUILD_DIR)/apps/$(1)/$(1).o: $(APPS_DIR)/$(1)/$(1).c
	@mkdir -p $$(dir $$@)
	$$(CC) $$(CFLAGS) -c $$< -o $$@

$(BUILD_DIR)/apps/$(1)/$(1).elf: $(BUILD_DIR)/apps/$(1)/$(1).o $(LIBOS_ARCHIVE) $(LIBC_ARCHIVE) $(LINKER_LD)
	$$(LD) -T $(LINKER_LD) -nostdlib \
		$(LIBOS_BUILD)/crt0/start.o \
		$(LIBOS_BUILD)/crt0/crt0.o \
		$$< \
		$(LIBOS_BUILD)/syscall.o \
		--start-group $(LIBC_ARCHIVE) --end-group \
		-o $$@
	@echo "  -> $$@"

$(DISK_BIN)/$(1): $(BUILD_DIR)/apps/$(1)/$(1).elf
	@mkdir -p $$(dir $$@)
	cp $$< $$@
	@echo "  -> $$@ (installed)"
endef

$(foreach name,$(APP_NAMES),$(eval $(call BUILD_APP,$(name))))

# ---------- clean ----------

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(DISK_BIN)/*
	@echo "Cleaned user build artifacts."
