# 4MegaOS

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Architecture: x86 (IA-32)](https://img.shields.io/badge/Architecture-x86%2032--bit-orange.svg)](#architecture)
[![Target RAM: 4MB](https://img.shields.io/badge/Target%20RAM-4%20MB-success.svg)](#4mb-memory-architecture)

> **4MegaOS** is a lightweight, bare-metal 32-bit x86 operating system designed specifically to boot and operate entirely within a **4 Megabyte (4096 KB)** RAM footprint.

---

## ⚡ Highlights

- **Ultra-low Memory Footprint:** Operates inside an exact 4 MB RAM constraint (`qemu -m 4M`).
- **Physical Memory Manager (PMM):** Uses a 128-byte bitmap to manage all 1,024 physical 4KB frames.
- **Dynamic Kernel Heap:** Dynamic block allocator (`kmalloc`, `kfree`) with coalescing and corruption detection.
- **Multiboot Compliant:** Boots natively via GRUB or QEMU direct kernel boot.
- **Hardware Drivers:**
  - **VGA Text Mode Console:** 80x25 character mode with 16 colors, hardware cursor manipulation, and scrolling.
  - **Serial COM1 Driver:** Mirrored console output for headless operation, automated testing, and host terminal logging.
  - **PS/2 Keyboard Driver:** Interrupt-driven keyboard controller with scancode translation and key buffer.
  - **8254 PIT (Programmable Interval Timer):** 100 Hz system tick for timing, delays, and uptime.
  - **8259 PIC (Programmable Interrupt Controller):** Cleanly remapped IRQs 0-15 to IDT gates 32-47.
  - **GDT & IDT:** 32-bit protected mode descriptor tables and exception handlers.
- **Interactive 4MegaShell:** Built-in command shell with system inspection tools, memory allocation testing, and text-mode games.

---

## 🧠 4MB Memory Architecture

In a 4 MB physical RAM space, there are exactly **1,024 page frames** (4096 bytes each), totaling **4,194,304 bytes**.

```
+-----------------------------------+ 0x400000 (4 MB)
|     Reserved for Kernel Stack     |
+-----------------------------------+ 0x3F0000 (~3.93 MB)
|                                   |
|         Kernel Heap Space         | (~2.9 MB dynamic heap)
|                                   |
+-----------------------------------+ 0x10D000 (Kernel End)
|         Kernel Executable         | (~52 KB code, data, bss)
+-----------------------------------+ 0x100000 (1 MB)
|       BIOS ROM & Video RAM        | (VGA text buffer at 0xB8000)
+-----------------------------------+ 0x080000 (EBDA)
|          Low Memory / IVT         | (Interrupt Vector Table, BDA)
+-----------------------------------+ 0x000000 (0 MB)
```

### Physical Memory Manager (PMM)
- **Bitmap Size:** `1024 bits / 8 = 128 bytes`.
- The entire 4MB memory map is tracked with only **128 bytes** of kernel memory.
- Frames `0` through `255` (0 to 1MB) and kernel frames are marked reserved; the remaining frames are made available for dynamic allocation.

---

## 🚀 Quick Start

### 1. Run Prebuilt ISO with QEMU
A pre-built bootable image `4megaos.iso` is included in this repository.

To boot the OS with exactly 4MB of RAM:
```bash
qemu-system-x86_64 -m 4M -cdrom 4megaos.iso
```

Or run directly in your terminal with serial console:
```bash
qemu-system-x86_64 -m 4M -cdrom 4megaos.iso -serial stdio -display none
```

---

## 🛠️ Building from Source

### Prerequisites
On Ubuntu / Debian / Arch / Fedora:
- `gcc` (with 32-bit multilib support)
- `binutils` (`as`, `ld`)
- `xorriso`
- `grub-pc-bin` / `grub` (provides `grub-mkrescue`)
- `qemu-system-x86`

On Arch Linux:
```bash
sudo pacman -S gcc binutils xorriso grub mtools qemu-system-x86
```

On Ubuntu / Debian:
```bash
sudo apt-get install gcc-multilib binutils grub-pc-bin grub-common xorriso mtools qemu-system-x86
```

### Build the Kernel and ISO
```bash
# Clean and compile the ISO
make clean
make

# Launch QEMU with 4MB of RAM
make run

# Or launch QEMU with serial console
make run-serial
```

---

## 💻 4MegaShell Commands

4MegaOS boots into an interactive shell with built-in commands:

| Command | Description |
|---|---|
| `help` | Display list of available commands |
| `info` | Show OS architecture, memory limits, and build specifications |
| `mem` | Display 4MB physical frame allocator and heap statistics |
| `alloc <bytes>` | Dynamically allocate bytes from kernel heap |
| `free <hex_addr>` | Free previously allocated heap memory block |
| `testmem` | Run stress tests verifying frame allocation, heap allocation, and block coalescing |
| `cpu` | Inspect CPU vendor string (CPUID), family, model, and instruction set flags |
| `uptime` | Display system uptime (`hh:mm:ss`) and PIT clock ticks |
| `calc <a> <op> <b>` | Built-in integer arithmetic calculator (`+`, `-`, `*`, `/`) |
| `color <0-15>` | Change VGA text foreground color |
| `clear` | Clear the VGA console screen |
| `snake` | Play classic Snake game in 80x25 text mode (W/A/S/D keys) |
| `matrix` | Retro green digital rain animation |
| `reboot` | Soft reset via the 8042 keyboard controller |
| `halt` | Power off / halt CPU execution |

---

## 📁 Project Structure

```
4megaOS/
├── Makefile                # Build system configuration
├── README.md               # Documentation and guides
├── LICENSE                 # MIT License
├── linker.ld               # Linker script (1MB kernel entry)
├── 4megaos.bin             # Compiled 32-bit ELF kernel binary
├── 4megaos.iso             # Bootable hybrid ISO image
├── isodir/
│   └── boot/
│       └── grub/
│           └── grub.cfg    # GRUB bootloader configuration
└── src/
    ├── boot.s              # Multiboot header & 32-bit entrypoint
    ├── interrupts.s        # ISR and IRQ assembly wrappers
    ├── kernel.c            # Kernel initialization sequence
    ├── shell.c / .h        # Interactive shell, snake, matrix
    ├── pmm.c / .h          # 4MB Physical Memory Manager (128-byte bitmap)
    ├── kheap.c / .h        # Dynamic kernel heap allocator
    ├── gdt.c / .h          # Global Descriptor Table
    ├── idt.c / .h          # Interrupt Descriptor Table
    ├── pic.c / .h          # 8259 PIC IRQ remapping
    ├── timer.c / .h        # 8254 PIT (100 Hz timer)
    ├── keyboard.c / .h     # PS/2 keyboard driver
    ├── vga.c / .h          # VGA 80x25 text mode console & kprintf
    ├── serial.c / .h       # COM1 serial communication driver
    ├── string.c / .h       # Freestanding C string & memory functions
    ├── types.h             # Standard integer & boolean types
    ├── io.h                # Port I/O inline assembly (inb, outb)
    └── multiboot.h         # Multiboot structure definitions
```

---

## 📜 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
