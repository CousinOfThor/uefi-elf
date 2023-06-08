#include </usr/include/elf.h> // otherwise clang doesn't find it (???)
#include <stdint.h>

#define _STDINT_H 1 // otherwise clang complains about int8_t redefinition in UEFI.h
#include "UEFI.h"

#define puts(str) SystemTable->ConOut->OutputString(SystemTable->ConOut, str)

typedef void (__attribute__((sysv_abi)) *kmain_t)(efi_gop_t *);

void *memset(void *ptr, int value, size_t size) {
	char *to = ptr;
	for (size_t i = 0; i < size; ++i)
		*to++ = value;
	return ptr;
}

void *memcpy(void *dst, const void *src, size_t size) {
	const char *from = src;
	char *to = dst;
	for (size_t i = 0; i < size; ++i)
		*to++ = *from++;
	return dst;
}

efi_status_t Read(FILE *file, uint64_t offset, size_t size, void *dst) {
	efi_status_t status = file->SetPosition(file, offset);
	if (status)
		return status;
	uint8_t *buf  = dst;
	uintn_t  read = 0;
	while (read < size) {
		uintn_t remains = size - read;
		status = file->Read(file, &remains, buf + read);
		if (status)
			return status;
		read += remains;
	}
	return EFI_SUCCESS;
}

efi_status_t VerifyELF(efi_system_table_t *SystemTable, Elf64_Ehdr *header) {
	if (header->e_ident[EI_MAG0] != ELFMAG0
	||  header->e_ident[EI_MAG1] != ELFMAG1
	||  header->e_ident[EI_MAG2] != ELFMAG2
	||  header->e_ident[EI_MAG3] != ELFMAG3)
	{
		puts(u"no elf magic sequence\r\n");
		return EFI_UNSUPPORTED;
	}

	if (header->e_ident[EI_CLASS] != ELFCLASS64) {
		puts(u"unsupported elf class\r\n");
		return EFI_UNSUPPORTED;
	}

	if (header->e_type != ET_EXEC) {
		puts(u"unsupported elf type\r\n");
		return EFI_UNSUPPORTED;
	}

	if (header->e_phnum == 0) {
		puts(u"eLF header does not contain any program headers\r\n");
		return EFI_UNSUPPORTED;
	}

	if (header->e_phentsize != sizeof(Elf64_Phdr)) {
		puts(u"unsupported program header size\r\n");
		return EFI_UNSUPPORTED;
	}

	return EFI_SUCCESS;
}

efi_status_t EFIAPI main(efi_handle_t Handle, efi_system_table_t *SystemTable) {
    efi_status_t status;

    status = SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
    if (status)
        return status;

    status = puts(u"hello, uefi!\r\n");
    if (status)
        return status;

	efi_loaded_image_protocol_t *image;
    efi_guid_t imguid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	status = SystemTable->BootServices->OpenProtocol(Handle, &imguid, (void **) &image, Handle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
	if (status)
        return status;

	efi_simple_file_system_protocol_t *rootfs;
	efi_handle_t root_device = image->DeviceHandle;
    efi_guid_t fsguid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    status = SystemTable->BootServices->OpenProtocol(root_device, &fsguid, (void **) &rootfs, Handle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
	if (status)
        return status;

	FILE *rootdir;
	status = rootfs->OpenVolume(rootfs, &rootdir);
	if (status)
        return status;

	puts(u"setting up elf kernel...\r\n");

	FILE *kernel;
	status = rootdir->Open(rootdir, &kernel, u"EFI\\Boot\\kernel", EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);
	if (status)
        return status;

	Elf64_Ehdr elf_header;
	status = Read(kernel, 0, sizeof(elf_header), &elf_header);
	if (status)
        return status;

	status = VerifyELF(SystemTable, &elf_header);
	if (status)
        return status;

	Elf64_Phdr *program_headers;
	uintn_t ph_size = elf_header.e_phnum * elf_header.e_phentsize;
	status = SystemTable->BootServices->AllocatePool(EfiLoaderData, ph_size, (void **) &program_headers);
	if (status)
        return status;

	status = Read(kernel, elf_header.e_phoff, ph_size, (void *) program_headers);
	if (status)
        return status;

	/* calculate program memory size */
	uint64_t image_begin = UINT64_MAX;
	uint64_t image_end   = 0;
	for (int i = 0; i < elf_header.e_phnum; i++) {
		Elf64_Phdr phdr = program_headers[i];
		if (phdr.p_type != PT_LOAD)
			continue;
		uint64_t phdr_begin = phdr.p_vaddr;
		phdr_begin &= ~(phdr.p_align - 1);
		if (phdr_begin < image_begin)
			image_begin = phdr_begin;
		uint64_t phdr_end = phdr.p_vaddr + phdr.p_memsz;
		phdr_end = (phdr_end + phdr.p_align - 1) & ~(phdr.p_align - 1);
		if (phdr_end > image_end)
			image_end = phdr_end;
	}

	puts(u"loading elf kernel image...\r\n");

	uint64_t page_size = 4096;
	uint64_t size = page_size + (image_end - image_begin);
	efi_physical_address_t addr;
	status = SystemTable->BootServices->AllocatePages(AllocateAnyPages, EfiLoaderData, size / page_size, &addr);
	if (status)
        return status;

	memset((void *) addr, 0, size);

	for (int i = 0; i < elf_header.e_phnum; i++) {
		Elf64_Phdr phdr = program_headers[i];
		if (phdr.p_type != PT_LOAD)
			continue;
		uint64_t phdr_addr = addr + page_size + phdr.p_vaddr - image_begin;
		status = Read(kernel, phdr.p_offset, phdr.p_filesz, (void *) phdr_addr);
		if (status)
			return status;
	}

	puts(u"accessing framebuffer...\r\n");

	efi_gop_t *gop;
    efi_guid_t gopguid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	status = SystemTable->BootServices->LocateProtocol(&gopguid, NULL, (void **) &gop);
	if (status)
        return status;

	puts(u"shutting down boot services and starting the kernel...\r\n");

	uintn_t MemoryMapSize = 4096;
	efi_memory_descriptor_t *MemoryMap;
	uintn_t  MemoryMapKey;
	uintn_t  DescriptorSize;
	uint32_t DescriptorVersion;

	while (1) {
		status = SystemTable->BootServices->AllocatePool(EfiLoaderData, MemoryMapSize, (void **) &MemoryMap);
		if (status != EFI_SUCCESS)
			return status;
		status = SystemTable->BootServices->GetMemoryMap(&MemoryMapSize, MemoryMap, &MemoryMapKey,
														 &DescriptorSize, &DescriptorVersion);
		if (status == EFI_SUCCESS) {
			status = SystemTable->BootServices->ExitBootServices(Handle, MemoryMapKey);
			if (status == EFI_SUCCESS)
				break;
		}
		SystemTable->BootServices->FreePool(MemoryMap);
		if (status == EFI_BUFFER_TOO_SMALL)
			MemoryMapSize *= 2; /* the pool size must be greater than the memory map size */
		else
			return status;
	}

	kmain_t kmain = (kmain_t) (addr + page_size + elf_header.e_entry - image_begin);
	kmain(gop);

	while (1) {};

    return EFI_LOAD_ERROR;
}
