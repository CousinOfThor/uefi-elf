extremely basic uefi bootloader which loads an elf64 kernel.

a makefile is provided: to compile it, you need clang and lld; to run it, you need quemu.

build it with `make`, run it with `make run`.

tested under linux.
