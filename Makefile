CC = gcc
AS = as
LD = ld

CFLAGS = -m32 -std=gnu99 -ffreestanding -fno-pie -no-pie -fno-stack-protector -fno-builtin -nostdlib -mno-sse -mno-sse2 -mno-mmx -mno-3dnow -Wall -Wextra -O2 -Isrc
ASFLAGS = --32
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

C_SRCS = $(wildcard src/*.c)
S_SRCS = $(wildcard src/*.s)
OBJS = $(C_SRCS:.c=.o) $(S_SRCS:.s=.o)

KERNEL = 4megaos.bin
ISO = 4megaos.iso

.PHONY: all clean run run-serial run-curses iso

all: $(ISO)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(AS) $(ASFLAGS) $< -o $@

$(KERNEL): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $(KERNEL)

$(ISO): $(KERNEL) isodir/boot/grub/grub.cfg
	mkdir -p isodir/boot/grub
	cp $(KERNEL) isodir/boot/4megaos.bin
	grub-mkrescue -d /usr/lib/grub/i386-pc -o $(ISO) isodir

run: $(ISO)
	qemu-system-x86_64 -m 4M -cdrom $(ISO)

run-serial: $(ISO)
	qemu-system-x86_64 -m 4M -cdrom $(ISO) -serial stdio

run-curses: $(ISO)
	qemu-system-x86_64 -m 4M -cdrom $(ISO) -curses

clean:
	rm -f src/*.o $(KERNEL) $(ISO) isodir/boot/4megaos.bin
