#include <efi.h>
#include <efilib.h>
#include <elf.h>

typedef unsigned long long size_t;

EFI_FILE* load_file(EFI_FILE* dir, CHAR16* path, EFI_HANDLE img_handle, EFI_SYSTEM_TABLE* sys_table)
{
	EFI_FILE* file;

	EFI_LOADED_IMAGE_PROTOCOL* loaded_img;
	sys_table->BootServices->HandleProtocol(img_handle, &gEfiLoadedImageProtocolGuid, (void**)&loaded_img);
	
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* file_sys;
	sys_table->BootServices->HandleProtocol(loaded_img->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (void**)&file_sys);

	if (!dir) file_sys->OpenVolume(file_sys, &dir);

	EFI_STATUS status = dir->Open(dir, &file, path, EFI_FILE_MODE_READ, EFI_FILE_READ_ONLY);

	if (status != EFI_SUCCESS) return NULL;

	return file;
}

int memcmp(const void* left, const void* right, size_t n)
{
	const unsigned char* a = left, *b = right;
	for (size_t i = 0; i < n; ++i)
	{
		if (a[i] < b[i]) return -1;
		else if (a[i] > b[i]) return 1;
	}
	return 0;
}

EFI_STATUS efi_main(EFI_HANDLE img_handle, EFI_SYSTEM_TABLE* sys_table)
{
	InitializeLib(img_handle, sys_table);
	
	EFI_FILE* kernel = load_file(NULL, L"kernel.elf", img_handle, sys_table);
	if (!kernel)
		Print(L"Could not load kernel\n\r");
	else
		Print(L"Kernel loaded successfully\n\r");

	Elf64_Ehdr header;
	{
		UINTN file_info_size;
		EFI_FILE_INFO* file_info;
		kernel->GetInfo(kernel, &gEfiFileInfoGuid, &file_info_size, NULL);
		sys_table->BootServices->AllocatePool(EfiLoaderData, file_info_size, (void**)&file_info);
		kernel->GetInfo(kernel, &gEfiFileInfoGuid, &file_info_size, (void**)&file_info);

		UINTN size = sizeof(header);
		kernel->Read(kernel, &size, &header);
	}

	if (memcmp(&header.e_ident[EI_MAG0], ELFMAG, SELFMAG) != 0
		|| header.e_ident[EI_CLASS] != ELFCLASS64
		|| header.e_ident[EI_DATA] != ELFDATA2LSB
		|| header.e_type != ET_EXEC
		|| header.e_machine != EM_X86_64
		|| header.e_version != EV_CURRENT)
		Print(L"kernel format is bad\r\n");
	else
		Print(L"kernel header successfully verified\r\n");

	Elf64_Phdr* phdrs;
	{
		kernel->SetPosition(kernel, header.e_phoff);
		UINTN size = header.e_phnum * header.e_phentsize;
		sys_table->BootServices->AllocatePool(EfiLoaderData, size, (void**)&phdrs);
		kernel->Read(kernel, &size, phdrs);
	}

	for (Elf64_Phdr* phdr = phdrs;
		(char*)phdr < (char*)phdrs + header.e_phnum * header.e_phentsize;
		phdr = (Elf64_Phdr*)((char*)phdr + header.e_phentsize))
	{
		switch (phdr->p_type)
		{
		case PT_LOAD:
		{
			int pages = (phdr->p_memsz + 0x1000 - 1) / 0x1000;
			Elf64_Addr segment = phdr->p_paddr;
			sys_table->BootServices->AllocatePages(AllocateAddress, EfiLoaderData, pages, &segment);

			kernel->SetPosition(kernel, phdr->p_offset);
			UINTN size = phdr->p_filesz;
			kernel->Read(kernel, &size, (void*)segment);
			break;
		}
		}
	}

	Print(L"Kernel loaded\n\r");

	int (*kernel_start)(void) = ((__attribute__((sysv_abi)) int (*)()) header.e_entry);

	Print(L"Start: %d\r\n", kernel_start());

	return EFI_SUCCESS; // Exit the UEFI application
}
