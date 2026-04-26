# ============================================================
# usr/Makefile — Build user libraries (libos, libc) and all apps
# ============================================================

CROSS_COMPILE ?= riscv64-linux-gnu-
HOST_CC ?= gcc
CC      := $(CROSS_COMPILE)gcc
CXX     := $(CROSS_COMPILE)g++
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

# Boot animation assets
NYANPACK_SRC := $(USR_DIR)/nyanpack.c
NYANPACK_BIN := $(BUILD_DIR)/nyanpack
BOOT_DIR := $(USR_DIR)/disk/share/boot
BOOT_NYAN_LOOP := $(BOOT_DIR)/nyan_loop.nyan
BOOT_AUDIO_INTRO := $(BOOT_DIR)/nyan_audio_intro.wav
BOOT_AUDIO_LOOP := $(BOOT_DIR)/nyan_audio_loop.wav
BOOT_ASSETS := $(BOOT_NYAN_LOOP) $(BOOT_AUDIO_INTRO) $(BOOT_AUDIO_LOOP)

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

# libam compatibility paths
LIBAM_DIR     := $(LIBS_DIR)/libam
LIBAM_INC     := $(LIBAM_DIR)/include
LIBAM_SRC     := $(LIBAM_DIR)/src
LIBAM_BUILD   := $(BUILD_DIR)/libam
LIBAM_ARCHIVE := $(BUILD_DIR)/libam.a

# libndl compatibility paths
LIBNDL_DIR     := $(LIBS_DIR)/libndl
LIBNDL_INC     := $(LIBNDL_DIR)/include
LIBNDL_SRC     := $(LIBNDL_DIR)/src
LIBNDL_BUILD   := $(BUILD_DIR)/libndl
LIBNDL_ARCHIVE := $(BUILD_DIR)/libndl.a

# libminiSDL compatibility paths
LIBMINISDL_DIR     := $(LIBS_DIR)/libminiSDL
LIBMINISDL_INC     := $(LIBMINISDL_DIR)/include
LIBMINISDL_SRC     := $(LIBMINISDL_DIR)/src
LIBMINISDL_BUILD   := $(BUILD_DIR)/libminiSDL
LIBMINISDL_ARCHIVE := $(BUILD_DIR)/libminiSDL.a

# libbmp compatibility paths
LIBBMP_DIR     := $(LIBS_DIR)/libbmp
LIBBMP_INC     := $(LIBBMP_DIR)/include
LIBBMP_SRC     := $(LIBBMP_DIR)/src
LIBBMP_BUILD   := $(BUILD_DIR)/libbmp
LIBBMP_ARCHIVE := $(BUILD_DIR)/libbmp.a

# libfixedptc compatibility paths
LIBFIXEDPTC_DIR     := $(LIBS_DIR)/libfixedptc
LIBFIXEDPTC_INC     := $(LIBFIXEDPTC_DIR)/include
LIBFIXEDPTC_SRC     := $(LIBFIXEDPTC_DIR)/src
LIBFIXEDPTC_BUILD   := $(BUILD_DIR)/libfixedptc
LIBFIXEDPTC_ARCHIVE := $(BUILD_DIR)/libfixedptc.a

# libSDL_image compatibility paths
LIBSDLIMG_DIR     := $(LIBS_DIR)/libSDL_image
LIBSDLIMG_INC     := $(LIBSDLIMG_DIR)/include
LIBSDLIMG_SRC     := $(LIBSDLIMG_DIR)/src
LIBSDLIMG_BUILD   := $(BUILD_DIR)/libSDL_image
LIBSDLIMG_ARCHIVE := $(BUILD_DIR)/libSDL_image.a

# Common compiler flags
CFLAGS := -Wall -O2 -ffreestanding -nostdlib \
          -march=rv64gc -mabi=lp64d -mcmodel=medany \
          -I$(LIBC_INC) \
		  -I$(LIBAM_INC) \
		  -I$(LIBNDL_INC) \
		  -I$(LIBMINISDL_INC) \
		  -I$(LIBBMP_INC) \
		  -I$(LIBFIXEDPTC_INC) \
		  -I$(LIBSDLIMG_INC) \
          -DNO_FLOATING_POINT -DHAVE_INITFINI_ARRAY \
          -U_FORTIFY_SOURCE -fno-stack-protector

CXXFLAGS := $(CFLAGS) -std=gnu++11 -fno-exceptions -fno-rtti

# ============================================================
# libc: collect all .c and .S sources (exclude .cpp and .t)
# ============================================================
LIBC_C_SRCS := $(shell find $(LIBC_SRC) -name '*.c')
LIBC_CPP_SRCS := $(shell find $(LIBC_SRC) -name '*.cpp')
LIBC_S_SRCS := $(shell find $(LIBC_SRC) -name '*.S' \
                 ! -name 'amd64.S' ! -name 'x86.S' \
                 ! -name 'mips32.S' ! -name 'loonarch32r.S')

LIBC_C_OBJS := $(patsubst $(LIBC_SRC)/%, $(LIBC_BUILD)/%, $(LIBC_C_SRCS:.c=.o))
LIBC_CPP_OBJS := $(patsubst $(LIBC_SRC)/%, $(LIBC_BUILD)/%, $(LIBC_CPP_SRCS:.cpp=.o))
LIBC_S_OBJS := $(patsubst $(LIBC_SRC)/%, $(LIBC_BUILD)/%, $(LIBC_S_SRCS:.S=.o))
LIBC_OBJS   := $(LIBC_C_OBJS) $(LIBC_CPP_OBJS) $(LIBC_S_OBJS)

# ============================================================
# libos: explicit sources (exclude native.cpp)
# ============================================================
LIBOS_C_SRCS := $(LIBOS_SRC)/syscall.c $(LIBOS_SRC)/crt0/crt0.c
LIBOS_S_SRCS := $(LIBOS_SRC)/crt0/start.S

LIBOS_C_OBJS := $(patsubst $(LIBOS_SRC)/%, $(LIBOS_BUILD)/%, $(LIBOS_C_SRCS:.c=.o))
LIBOS_S_OBJS := $(patsubst $(LIBOS_SRC)/%, $(LIBOS_BUILD)/%, $(LIBOS_S_SRCS:.S=.o))
LIBOS_OBJS   := $(LIBOS_C_OBJS) $(LIBOS_S_OBJS)

# ============================================================
# libam: compatibility layer sources
# ============================================================
LIBAM_C_SRCS := $(shell find $(LIBAM_SRC) -name '*.c')
LIBAM_C_OBJS := $(patsubst $(LIBAM_SRC)/%, $(LIBAM_BUILD)/%, $(LIBAM_C_SRCS:.c=.o))
LIBAM_OBJS   := $(LIBAM_C_OBJS)

# ============================================================
# libndl: compatibility layer sources
# ============================================================
LIBNDL_C_SRCS := $(shell find $(LIBNDL_SRC) -name '*.c')
LIBNDL_C_OBJS := $(patsubst $(LIBNDL_SRC)/%, $(LIBNDL_BUILD)/%, $(LIBNDL_C_SRCS:.c=.o))
LIBNDL_OBJS   := $(LIBNDL_C_OBJS)

# ============================================================
# libminiSDL: compatibility layer sources
# ============================================================
LIBMINISDL_C_SRCS := $(shell find $(LIBMINISDL_SRC) -name '*.c')
LIBMINISDL_C_OBJS := $(patsubst $(LIBMINISDL_SRC)/%, $(LIBMINISDL_BUILD)/%, $(LIBMINISDL_C_SRCS:.c=.o))
LIBMINISDL_OBJS   := $(LIBMINISDL_C_OBJS)

# ============================================================
# libbmp: compatibility layer sources
# ============================================================
LIBBMP_C_SRCS := $(shell find $(LIBBMP_SRC) -name '*.c')
LIBBMP_C_OBJS := $(patsubst $(LIBBMP_SRC)/%, $(LIBBMP_BUILD)/%, $(LIBBMP_C_SRCS:.c=.o))
LIBBMP_OBJS   := $(LIBBMP_C_OBJS)

# ============================================================
# libfixedptc: compatibility layer sources
# ============================================================
LIBFIXEDPTC_C_SRCS := $(shell find $(LIBFIXEDPTC_SRC) -name '*.c')
LIBFIXEDPTC_C_OBJS := $(patsubst $(LIBFIXEDPTC_SRC)/%, $(LIBFIXEDPTC_BUILD)/%, $(LIBFIXEDPTC_C_SRCS:.c=.o))
LIBFIXEDPTC_OBJS   := $(LIBFIXEDPTC_C_OBJS)

# ============================================================
# libSDL_image: compatibility layer sources
# ============================================================
LIBSDLIMG_C_SRCS := $(shell find $(LIBSDLIMG_SRC) -name '*.c')
LIBSDLIMG_C_OBJS := $(patsubst $(LIBSDLIMG_SRC)/%, $(LIBSDLIMG_BUILD)/%, $(LIBSDLIMG_C_SRCS:.c=.o))
LIBSDLIMG_OBJS   := $(LIBSDLIMG_C_OBJS)

# ============================================================
# Apps: each subdirectory under apps/ with a .c file
# ============================================================
APP_DIRS := $(shell find $(APPS_DIR) -mindepth 1 -maxdepth 1 -type d)
APP_NAMES := $(filter-out bird pal fceux desktop,$(notdir $(APP_DIRS)))
APP_ELFS  := $(foreach name,$(APP_NAMES),$(BUILD_DIR)/apps/$(name)/$(name).elf)
APP_BINS  := $(foreach name,$(APP_NAMES),$(DISK_BIN)/$(name))

DESKTOP_DIR  := $(APPS_DIR)/desktop
DESKTOP_SRCS := $(wildcard $(DESKTOP_DIR)/*.c)
DESKTOP_OBJS := $(patsubst $(DESKTOP_DIR)/%.c,$(BUILD_DIR)/apps/desktop/%.o,$(DESKTOP_SRCS))
DESKTOP_ELF  := $(BUILD_DIR)/apps/desktop/desktop.elf
DESKTOP_BIN  := $(DISK_BIN)/desktop

BIRD_REPO_DIR := $(APPS_DIR)/bird/repo
BIRD_INC      := $(BIRD_REPO_DIR)/include
BIRD_CPP_SRCS := $(shell find $(BIRD_REPO_DIR)/src -name '*.cpp')
BIRD_CPP_OBJS := $(patsubst $(BIRD_REPO_DIR)/src/%, $(BUILD_DIR)/apps/bird/%, $(BIRD_CPP_SRCS:.cpp=.o))
BIRD_ELF      := $(BUILD_DIR)/apps/bird/bird.elf
BIRD_BIN      := $(DISK_BIN)/bird

PAL_REPO_DIR := $(APPS_DIR)/pal/repo
PAL_INC      := $(PAL_REPO_DIR)/include
PAL_C_SRCS   := $(shell find $(PAL_REPO_DIR)/src -name '*.c')
PAL_CPP_SRCS := $(shell find $(PAL_REPO_DIR)/src -name '*.cpp')
PAL_C_OBJS   := $(patsubst $(PAL_REPO_DIR)/src/%, $(BUILD_DIR)/apps/pal/%, $(PAL_C_SRCS:.c=.o))
PAL_CPP_OBJS := $(patsubst $(PAL_REPO_DIR)/src/%, $(BUILD_DIR)/apps/pal/%, $(PAL_CPP_SRCS:.cpp=.o))
PAL_OBJS     := $(PAL_C_OBJS) $(PAL_CPP_OBJS)
PAL_ELF      := $(BUILD_DIR)/apps/pal/pal.elf
PAL_BIN      := $(DISK_BIN)/pal
PAL_DATA_SRC := $(PAL_REPO_DIR)/data
PAL_DATA_DST := $(USR_DIR)/disk/share/games/pal

FCEUX_REPO_DIR := $(APPS_DIR)/fceux/repo
FCEUX_INC      := $(FCEUX_REPO_DIR)/src
FCEUX_C_SRCS   := $(shell find $(FCEUX_REPO_DIR)/src -name '*.c')
FCEUX_CPP_SRCS := $(shell find $(FCEUX_REPO_DIR)/src -name '*.cpp')
FCEUX_C_OBJS   := $(patsubst $(FCEUX_REPO_DIR)/src/%, $(BUILD_DIR)/apps/fceux/%, $(FCEUX_C_SRCS:.c=.o))
FCEUX_CPP_OBJS := $(patsubst $(FCEUX_REPO_DIR)/src/%, $(BUILD_DIR)/apps/fceux/%, $(FCEUX_CPP_SRCS:.cpp=.o))
FCEUX_OBJS     := $(FCEUX_C_OBJS) $(FCEUX_CPP_OBJS)
FCEUX_ELF      := $(BUILD_DIR)/apps/fceux/fceux.elf
FCEUX_BIN      := $(DISK_BIN)/fceux

# ============================================================
# Targets
# ============================================================
.PHONY: all clean libs apps boot-assets nyanpack

all: libs apps

libs: $(LIBC_ARCHIVE) $(LIBOS_ARCHIVE) $(LIBAM_ARCHIVE) $(LIBNDL_ARCHIVE) $(LIBMINISDL_ARCHIVE) $(LIBBMP_ARCHIVE) $(LIBFIXEDPTC_ARCHIVE) $(LIBSDLIMG_ARCHIVE)

apps: libs boot-assets $(APP_BINS) $(DESKTOP_BIN) $(BIRD_BIN) $(PAL_BIN) $(FCEUX_BIN) pal-data

boot-assets:
	@for f in $(BOOT_ASSETS); do \
		if [ ! -f "$$f" ]; then \
			echo "missing boot resource: $$f"; \
			exit 1; \
		fi; \
	done
	@echo "  -> boot assets ready"

nyanpack: $(NYANPACK_BIN)

$(NYANPACK_BIN): $(NYANPACK_SRC)
	@mkdir -p $(dir $@)
	$(HOST_CC) -Wall -Wextra -O2 $< -o $@

# ---------- libc ----------

$(LIBC_BUILD)/%.o: $(LIBC_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBC_BUILD)/%.o: $(LIBC_SRC)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBC_BUILD)/%.o: $(LIBC_SRC)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

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

# ---------- libam ----------

$(LIBAM_BUILD)/%.o: $(LIBAM_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBAM_ARCHIVE): $(LIBAM_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^
	@echo "  -> $@ ($(words $^) objects)"

# ---------- libndl ----------

$(LIBNDL_BUILD)/%.o: $(LIBNDL_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBNDL_ARCHIVE): $(LIBNDL_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^
	@echo "  -> $@ ($(words $^) objects)"

# ---------- libminiSDL ----------

$(LIBMINISDL_BUILD)/%.o: $(LIBMINISDL_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBMINISDL_ARCHIVE): $(LIBMINISDL_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^
	@echo "  -> $@ ($(words $^) objects)"

# ---------- libbmp ----------

$(LIBBMP_BUILD)/%.o: $(LIBBMP_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBBMP_ARCHIVE): $(LIBBMP_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^
	@echo "  -> $@ ($(words $^) objects)"

# ---------- libfixedptc ----------

$(LIBFIXEDPTC_BUILD)/%.o: $(LIBFIXEDPTC_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBFIXEDPTC_ARCHIVE): $(LIBFIXEDPTC_OBJS)
	@mkdir -p $(dir $@)
	$(AR) rcs $@ $^
	@echo "  -> $@ ($(words $^) objects)"

# ---------- libSDL_image ----------

$(LIBSDLIMG_BUILD)/%.o: $(LIBSDLIMG_SRC)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBSDLIMG_ARCHIVE): $(LIBSDLIMG_OBJS)
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

$(BUILD_DIR)/apps/$(1)/$(1).elf: $(BUILD_DIR)/apps/$(1)/$(1).o $(LIBOS_ARCHIVE) $(LIBAM_ARCHIVE) $(LIBMINISDL_ARCHIVE) $(LIBBMP_ARCHIVE) $(LIBNDL_ARCHIVE) $(LIBC_ARCHIVE) $(LINKER_LD)
	$$(LD) -T $(LINKER_LD) -nostdlib \
		$(LIBOS_BUILD)/crt0/start.o \
		$(LIBOS_BUILD)/crt0/crt0.o \
		$$< \
		$(LIBOS_BUILD)/syscall.o \
		$(LIBAM_ARCHIVE) \
		--start-group $(LIBMINISDL_ARCHIVE) $(LIBBMP_ARCHIVE) $(LIBNDL_ARCHIVE) $(LIBC_ARCHIVE) --end-group \
		-o $$@
	@echo "  -> $$@"

$(DISK_BIN)/$(1): $(BUILD_DIR)/apps/$(1)/$(1).elf
	@mkdir -p $$(dir $$@)
	cp $$< $$@
	@echo "  -> $$@ (installed)"
endef

$(foreach name,$(APP_NAMES),$(eval $(call BUILD_APP,$(name))))

# ---------- desktop ----------

$(BUILD_DIR)/apps/desktop/%.o: $(DESKTOP_DIR)/%.c $(wildcard $(DESKTOP_DIR)/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(DESKTOP_ELF): $(DESKTOP_OBJS) $(LIBOS_ARCHIVE) $(LIBAM_ARCHIVE) $(LIBMINISDL_ARCHIVE) $(LIBBMP_ARCHIVE) $(LIBNDL_ARCHIVE) $(LIBC_ARCHIVE) $(LINKER_LD)
	$(LD) -T $(LINKER_LD) -nostdlib \
		$(LIBOS_BUILD)/crt0/start.o \
		$(LIBOS_BUILD)/crt0/crt0.o \
		$(DESKTOP_OBJS) \
		$(LIBOS_BUILD)/syscall.o \
		$(LIBAM_ARCHIVE) \
		--start-group $(LIBMINISDL_ARCHIVE) $(LIBBMP_ARCHIVE) $(LIBNDL_ARCHIVE) $(LIBC_ARCHIVE) --end-group \
		-o $@
	@echo "  -> $@"

$(DESKTOP_BIN): $(DESKTOP_ELF)
	@mkdir -p $(dir $@)
	cp $< $@
	@echo "  -> $@ (installed)"

# ---------- bird (C++) ----------

$(BUILD_DIR)/apps/bird/%.o: $(BIRD_REPO_DIR)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -D__NAVY__ -I$(BIRD_INC) -c $< -o $@

$(BIRD_ELF): $(BIRD_CPP_OBJS) $(LIBOS_ARCHIVE) $(LIBAM_ARCHIVE) $(LIBMINISDL_ARCHIVE) $(LIBSDLIMG_ARCHIVE) $(LIBFIXEDPTC_ARCHIVE) $(LIBBMP_ARCHIVE) $(LIBNDL_ARCHIVE) $(LIBC_ARCHIVE) $(LINKER_LD)
	$(LD) -T $(LINKER_LD) -nostdlib \
		$(LIBOS_BUILD)/crt0/start.o \
		$(LIBOS_BUILD)/crt0/crt0.o \
		$(BIRD_CPP_OBJS) \
		$(LIBOS_BUILD)/syscall.o \
		$(LIBAM_ARCHIVE) \
		--start-group $(LIBMINISDL_ARCHIVE) $(LIBSDLIMG_ARCHIVE) $(LIBFIXEDPTC_ARCHIVE) $(LIBBMP_ARCHIVE) $(LIBNDL_ARCHIVE) $(LIBC_ARCHIVE) --end-group \
		-o $@
	@echo "  -> $@"

$(BIRD_BIN): $(BIRD_ELF)
	@mkdir -p $(dir $@)
	cp $< $@
	@echo "  -> $@ (installed)"

# ---------- pal ----------

$(BUILD_DIR)/apps/pal/%.o: $(PAL_REPO_DIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -D__NAVY__ -I$(PAL_INC) -c $< -o $@

$(BUILD_DIR)/apps/pal/%.o: $(PAL_REPO_DIR)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -D__NAVY__ -I$(PAL_INC) -c $< -o $@

$(PAL_ELF): $(PAL_OBJS) $(LIBOS_ARCHIVE) $(LIBAM_ARCHIVE) $(LIBMINISDL_ARCHIVE) $(LIBFIXEDPTC_ARCHIVE) $(LIBBMP_ARCHIVE) $(LIBNDL_ARCHIVE) $(LIBC_ARCHIVE) $(LINKER_LD)
	$(LD) -T $(LINKER_LD) -nostdlib \
		$(LIBOS_BUILD)/crt0/start.o \
		$(LIBOS_BUILD)/crt0/crt0.o \
		$(PAL_OBJS) \
		$(LIBOS_BUILD)/syscall.o \
		$(LIBAM_ARCHIVE) \
		--start-group $(LIBMINISDL_ARCHIVE) $(LIBFIXEDPTC_ARCHIVE) $(LIBBMP_ARCHIVE) $(LIBNDL_ARCHIVE) $(LIBC_ARCHIVE) --end-group \
		-o $@
	@echo "  -> $@"

$(PAL_BIN): $(PAL_ELF)
	@mkdir -p $(dir $@)
	cp $< $@
	@echo "  -> $@ (installed)"

# ---------- fceux ----------

$(BUILD_DIR)/apps/fceux/%.o: $(FCEUX_REPO_DIR)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -D__NAVY__ -D__PLATFORM_QEMU -DPSS_STYLE=1 -DFRAMESKIP -I$(FCEUX_INC) -c $< -o $@

$(BUILD_DIR)/apps/fceux/%.o: $(FCEUX_REPO_DIR)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -D__NAVY__ -D__PLATFORM_QEMU -DPSS_STYLE=1 -DFRAMESKIP -I$(FCEUX_INC) -c $< -o $@

$(FCEUX_ELF): $(FCEUX_OBJS) $(LIBOS_ARCHIVE) $(LIBAM_ARCHIVE) $(LIBMINISDL_ARCHIVE) $(LIBFIXEDPTC_ARCHIVE) $(LIBBMP_ARCHIVE) $(LIBNDL_ARCHIVE) $(LIBC_ARCHIVE) $(LINKER_LD)
	$(LD) -T $(LINKER_LD) -nostdlib \
		$(LIBOS_BUILD)/crt0/start.o \
		$(LIBOS_BUILD)/crt0/crt0.o \
		$(FCEUX_OBJS) \
		$(LIBOS_BUILD)/syscall.o \
		$(LIBAM_ARCHIVE) \
		--start-group $(LIBMINISDL_ARCHIVE) $(LIBFIXEDPTC_ARCHIVE) $(LIBBMP_ARCHIVE) $(LIBNDL_ARCHIVE) $(LIBC_ARCHIVE) --end-group \
		-o $@
	@echo "  -> $@"

$(FCEUX_BIN): $(FCEUX_ELF)
	@mkdir -p $(dir $@)
	cp $< $@
	@echo "  -> $@ (installed)"

.PHONY: pal-data
pal-data:
	@mkdir -p $(dir $(PAL_DATA_DST))
	rm -rf $(PAL_DATA_DST)
	cp -a $(PAL_DATA_SRC) $(PAL_DATA_DST)
	@echo "  -> $(PAL_DATA_DST) (installed)"

# ---------- clean ----------

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(DISK_BIN)/*
	@echo "Cleaned user build artifacts."
