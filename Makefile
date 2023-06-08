CFLAGS := -ggdb -ffreestanding -MMD -mno-red-zone -nostdlib

BOOTLOADER_CFLAGS := -target x86_64-unknown-windows -fuse-ld=lld-link \
	-Wl,-subsystem:efi_application -Wl,-entry:main

SRCS := booloader.c kernel.c

EFIDIR := ESP/EFI/Boot
OVMF := OVMF.fd

default: all

BootX64.efi: bootloader.c
	clang $(CFLAGS) $(BOOTLOADER_CFLAGS) $^ -o $@

kernel: kernel.c
	clang $(CFLAGS) -c $< -o kernel.o
	lld -flavor ld -e main kernel.o -o $@

prepare: BootX64.efi kernel
	mkdir -p $(EFIDIR)
	cp BootX64.efi kernel $(EFIDIR)

run: prepare
	qemu-system-x86_64 \
		-drive if=pflash,format=raw,file=$(OVMF) \
		-drive format=raw,file=fat:rw:ESP \
		-net none \
		-gdb tcp::1234 \
		-S

-include $(SRCS:.c=.d)

.PHONY: clean all default

all: BootX64.efi kernel

clean:
	rm -rf BootX64.efi *.o *.d *.lib ESP
