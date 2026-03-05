
CROSS_COMPILE ?= riscv64-linux-gnu-
CC      := $(CROSS_COMPILE)gcc
LD      := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump

BUILD_DIR  := ../build
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin

CFLAGS  := -Wall -Werror -O2 -ffreestanding -nostdlib \
           -march=rv64gc -mabi=lp64d -mcmodel=medany \
           -I.
LDFLAGS := -T linker.ld -nostdlib

C_SRCS := $(shell find . -name '*.c')
S_SRCS := $(shell find . -name '*.S')

C_OBJS := $(patsubst ./%, $(BUILD_DIR)/%, $(C_SRCS:.c=.o))
S_OBJS := $(patsubst ./%, $(BUILD_DIR)/%, $(S_SRCS:.S=.o))
OBJS   := $(S_OBJS) $(C_OBJS)

DEPS := $(C_OBJS:.o=.d)

.PHONY: all clean run run-gui disasm

all: $(KERNEL_BIN)

$(BUILD_DIR)/%.o: ./%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/%.o: ./%.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_ELF): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo "  -> $(KERNEL_BIN) ($(shell stat -c%s $@ 2>/dev/null || echo '?') bytes)"

disasm: $(KERNEL_ELF)
	$(OBJDUMP) -d $< > $(BUILD_DIR)/kernel.asm
	@echo "  -> $(BUILD_DIR)/kernel.asm"

QEMU := qemu-system-riscv64
QEMU_ARGS := -machine virt -bios default -kernel $(KERNEL_BIN)

run: $(KERNEL_BIN)
	$(QEMU) $(QEMU_ARGS) -nographic

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
