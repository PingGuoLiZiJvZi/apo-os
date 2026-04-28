# APO-OS

APO-OS is a small RISC-V operating system that runs on QEMU. It provides the
basic kernel services needed for a desktop-style game launcher, including
process management, virtual memory, a file system, Virtio devices, graphics,
audio, keyboard, mouse, and a simple desktop environment.

The current desktop can launch several user programs and games, including
Snake, Flappy Bird, PAL, and a NES emulator running Mario.

## Features

- RISC-V64 kernel for QEMU `virt`
- Multitasking user processes
- Virtual memory and user/kernel address separation
- Timer interrupt based scheduling
- System call layer for user programs
- Basic file system backed by a generated disk image
- Virtio block, GPU, input, tablet, and sound devices
- Desktop environment with windows, icons, focus, and app launching
- User-space compatibility libraries for AM, NDL, miniSDL, BMP, fixed point,
  and SDL_image-style programs
- Example apps and smoke tests for kernel/user interfaces

## Repository Layout

```text
.
├── kernel/              # Kernel source
│   ├── main/            # Boot code and kernel entry point
│   ├── system/          # System call layer and kernel services
│   ├── proc/            # Process management and scheduler
│   ├── memory/          # Physical and virtual memory management
│   ├── irq/             # Interrupt and exception handling
│   ├── device/          # Virtio and device-file implementations
│   ├── disk/            # Virtio block device I/O
│   ├── fs/              # File system implementation
│   ├── libc/            # Kernel-side freestanding C helpers
│   └── docs/            # Kernel notes and milestones
├── usr/                 # User-space build, libraries, apps, and disk root
│   ├── apps/            # Desktop, games, demos, and smoke tests
│   ├── libs/            # User-space libc and compatibility libraries
│   ├── disk/            # Files copied into the generated disk image
│   ├── mkfs.c           # Host tool for building the disk image
│   └── Makefile         # User-space build system
├── resource/            # Project resources
├── build/               # Generated kernel, tools, and disk image
└── README.md            # Project documentation
```

## Kernel Overview

The kernel is intentionally compact and organized by subsystem:

- `kernel/main`: boot setup and transition into kernel initialization
- `kernel/system`: system calls and core kernel service helpers
- `kernel/proc`: PCB table, process lifecycle, fork/exec/wait, and scheduling
- `kernel/memory`: page allocation, user mappings, and address translation
- `kernel/irq`: trap handling, timer interrupts, and device interrupts
- `kernel/device`: device files and Virtio GPU/input/sound integration
- `kernel/disk`: Virtio block transport
- `kernel/fs`: file abstraction and disk-backed file access
- `kernel/libc`: freestanding string, memory, and formatting helpers

## User Space Overview

User programs are built as freestanding RISC-V binaries and installed into
`usr/disk/bin`.

Important components:

- `usr/apps/desktop`: desktop shell, compositor, input routing, and launcher
- `usr/apps/files`: graphical file manager
- `usr/apps/snake`: simple game used as a lightweight app
- `usr/apps/bird`: Flappy Bird port
- `usr/apps/pal`: PAL game port
- `usr/apps/fceux`: NES emulator port
- `usr/apps/*-smoke`: tests and demos for kernel/user interfaces
- `usr/libs/libos`: syscall stubs and user startup code
- `usr/libs/libc`: user-space C library
- `usr/libs/libam`, `libndl`, `libminiSDL`: compatibility layers for ported apps

Desktop launch entries live in `usr/disk/desktop/*.desk`.

## Requirements

Install the following host tools:

- `make`
- `gcc` for host tools such as `mkfs`
- `riscv64-linux-gnu-gcc`
- `riscv64-linux-gnu-g++`
- `riscv64-linux-gnu-ld`
- `riscv64-linux-gnu-objcopy`
- `riscv64-linux-gnu-objdump`
- `qemu-system-riscv64`

The default run target uses QEMU SDL audio:

```text
-audiodev sdl,id=audio0
```

Make sure your QEMU build supports SDL audio, or adjust `kernel/Makefile` for
your local audio backend.

## Build

Build user-space libraries and applications:

```bash
make -C usr all
```

Build the kernel:

```bash
make -C kernel all
```

The main generated files are:

```text
build/kernel.elf
build/kernel.bin
usr/disk/bin/*
```

## Run

Run the full system:

```bash
make -C kernel run
```

This builds user programs, creates `build/disk.img`, builds the kernel, and
starts QEMU with:

- Virtio block device
- Virtio GPU at 640x480
- Virtio keyboard
- Virtio tablet
- Virtio sound
- Serial output on the terminal

Run with an existing disk image:

```bash
make -C kernel run-no-disk
```

Debug with GDB:

```bash
make -C kernel debug
```

Then connect from another terminal:

```bash
gdb-multiarch build/kernel.elf -ex 'target remote :1234'
```

Clean generated files:

```bash
make -C kernel clean
```

## Disk Image

The disk image is generated from `usr/disk` by `usr/mkfs.c`.

Common locations:

```text
usr/disk/bin/          # User binaries installed by the build
usr/disk/desktop/      # Desktop launcher entries
usr/disk/share/        # Shared game data, icons, and boot assets
build/disk.img         # Generated disk image consumed by QEMU
```

To add a new desktop application:

1. Add or port the app under `usr/apps/<name>`.
2. Make sure it is built into `usr/disk/bin/<name>`.
3. Add `usr/disk/desktop/<name>.desk`.
4. Re-run `make -C kernel run` to rebuild the disk image.

Example `.desk` entry:

```ini
type=exec
icon=/share/desktop/icon-example.bmp
path=/bin/example
arg=NULL
w=320
l=240
centered=1
name=example
```

## Known Limitations

1. The kernel does not have permission management or a security isolation
   policy. The Desktop currently exposes only a very limited set of UI
   operations to users to avoid destructive behavior.
2. To support the Desktop and simplify the implementation, the kernel contains
   non-standard desktop-specific changes. In particular, the File layer
   redirects framebuffer access by PID between the real GPU and the virtual GPU.

## Third-Party Code & Reference

The file system design refers to the implementation style of xv6. The user-space
architecture refers to the design of nanos.

The Bird, FCEUX, PAL, and Snake applications, as well as most user-space
libraries, are based on versions completed by NJU and open-source contributors
after porting specific APIs such as AM, NDL, and SDL to constrained hardware
resources.

For this part of the code, the author adapted it to APO-OS and improved
performance by modifying and rewriting parts of the implementation using the
resources and system calls provided by APO-OS, while keeping the overall
runtime environment mostly unchanged.

## Contributors

AI assistance was used for parts of the kernel driver work. AI assistance was
also used for user-space library refactoring and porting, as well as for writing
new applications.

## License
APO-OS-specific code is licensed under the MIT License, unless otherwise noted.
Third-party and ported components retain their original licenses.