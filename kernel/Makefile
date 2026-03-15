
CROSS_COMPILE ?= riscv64-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump

BUILD_DIR  := ../build
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin

CFLAGS  := -Wall -O -g -ffreestanding -nostdlib \
           -march=rv64gc -mabi=lp64d -mcmodel=medany \
           -I. -Idtc-1.7.2/libfdt -Ilibc
LDFLAGS := -T main/linker.ld -nostdlib

# 查找当前目录下所有 C 文件
C_SRCS_BASE := $(shell find . -name '*.c')

C_SRCS := $(C_SRCS_BASE) 


S_SRCS := $(shell find . -name '*.S' ! -path './dtc-1.7.2/*')

C_OBJS := $(patsubst ./%, $(BUILD_DIR)/%, $(C_SRCS:.c=.o))
S_OBJS := $(patsubst ./%, $(BUILD_DIR)/%, $(S_SRCS:.S=.o))
OBJS   := $(S_OBJS) $(C_OBJS)

DEPS := $(C_OBJS:.o=.d)

.PHONY: all clean run disasm debug

all: $(KERNEL_BIN)

$(BUILD_DIR)/%.o: ./%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: ./%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(OBJS) main/linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo "  -> $(KERNEL_BIN) ($(shell stat -c%s $@ 2>/dev/null || echo '?') bytes)"

disasm: $(KERNEL_ELF)
	$(OBJDUMP) -d $< > $(BUILD_DIR)/kernel.asm
	@echo "  -> $(BUILD_DIR)/kernel.asm"

QEMU := qemu-system-riscv64
DISK_IMG := $(BUILD_DIR)/disk.img
QEMU_COMMON := -machine virt -bios default -kernel $(KERNEL_BIN) \
			   -drive file=$(DISK_IMG),format=raw,id=hd0 \
			   -device virtio-blk-device,drive=hd0 \
			   -device virtio-gpu-device \
			   -device virtio-keyboard-device \
			   -device virtio-mouse-device
QEMU_ARGS := $(QEMU_COMMON)
GDB_PORT ?= 1234

MKFS_SRC := ../usr/mkfs.c
MKFS_BIN := $(BUILD_DIR)/mkfs
DISK_ROOT := ../usr/disk
USR_DIR  := ../usr

.PHONY: user-apps user-clean

user-apps:
	$(MAKE) -C $(USR_DIR) all

user-clean:
	$(MAKE) -C $(USR_DIR) clean

$(MKFS_BIN): $(MKFS_SRC)
	@mkdir -p $(dir $@)
	gcc -Wall -O -o $@ $<

$(DISK_IMG): $(MKFS_BIN) user-apps $(shell find $(DISK_ROOT) -type f -o -type d)
	$(MKFS_BIN) $@ $(DISK_ROOT)

run: $(KERNEL_BIN) $(DISK_IMG)
	$(QEMU) $(QEMU_COMMON) -display gtk,gl=off -serial mon:stdio


debug: $(KERNEL_BIN)
	@echo "Starting QEMU with GDB server on port $(GDB_PORT) (paused, waiting for GDB)..."
	@echo "Connect with: gdb-multiarch $(KERNEL_ELF) -ex 'target remote :$(GDB_PORT)'"
	$(QEMU) $(QEMU_ARGS) -nographic -S -gdb tcp::$(GDB_PORT)

clean: user-clean
	rm -rf $(BUILD_DIR)

-include $(DEPS)
