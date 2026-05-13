#include <inc/memlayout.h>
#include <kern/kheap.h>
#include <kern/memory_manager.h>


uint32 kheap_allocated_pages[(KERNEL_HEAP_MAX - KERNEL_HEAP_START) / PAGE_SIZE] = {0};
void* kmalloc(unsigned int size)
{


	uint32 rounded_size = ROUNDUP(size, PAGE_SIZE);
	uint32 req_pages = rounded_size / PAGE_SIZE;

	if (req_pages == 0) {
		return NULL;
	}


	static uint32 kheap_next_fit_ptr = KERNEL_HEAP_START;

	uint32 start_search_va = kheap_next_fit_ptr;
	uint32 current_va = start_search_va;
	uint32 free_pages_found = 0;
	uint32 allocated_start_va = 0;


	while (1) {


		uint32 *ptr_page_table = NULL;

		struct Frame_Info *ptr_frame_info = get_frame_info(ptr_page_directory, (void*) current_va, &ptr_page_table);

		if (ptr_frame_info == NULL) {

			if (free_pages_found == 0) {
				allocated_start_va = current_va;
			}
			free_pages_found++;


			if (free_pages_found == req_pages) {
				break;
			}
		} else {

			free_pages_found = 0;
		}


		current_va += PAGE_SIZE;


		if (current_va >= KERNEL_HEAP_MAX) {
			current_va = KERNEL_HEAP_START;
			free_pages_found = 0;
		}


		if (current_va == start_search_va) {
			return NULL;
		}
	}



	for (uint32 i = 0; i < req_pages; i++) {
		uint32 va_to_map = allocated_start_va + (i * PAGE_SIZE);

		struct Frame_Info *new_frame;
		int ret = allocate_frame(&new_frame);

		if (ret != E_NO_MEM) {

			map_frame(ptr_page_directory, new_frame,(void*) va_to_map, PERM_WRITEABLE | PERM_PRESENT);
		}
	}


	kheap_next_fit_ptr = allocated_start_va + rounded_size;


	if (kheap_next_fit_ptr >= KERNEL_HEAP_MAX) {
		kheap_next_fit_ptr = KERNEL_HEAP_START;
	}

	uint32 index = (allocated_start_va - KERNEL_HEAP_START) / PAGE_SIZE;
	kheap_allocated_pages[index] = req_pages;

	return (void*)allocated_start_va;
}
void kfree(void* virtual_address)
{
	uint32 va = (uint32)virtual_address;


	if (va < KERNEL_HEAP_START || va >= KERNEL_HEAP_MAX) {
		return;
	}


	uint32 index = (va - KERNEL_HEAP_START) / PAGE_SIZE;
	uint32 num_of_pages = kheap_allocated_pages[index];


	if (num_of_pages == 0) {
		return;
	}


	for (uint32 i = 0; i < num_of_pages; i++) {
		uint32 current_va = va + (i * PAGE_SIZE);



		unmap_frame(ptr_page_directory,(void*) current_va);
	}


	kheap_allocated_pages[index] = 0;
}


unsigned int kheap_virtual_address(unsigned int physical_address)
{



	uint32 offset = physical_address & 0xFFF;
	uint32 target_frame_pa = physical_address & 0xFFFFF000;


	for (uint32 va = KERNEL_HEAP_START; va < KERNEL_HEAP_MAX; va += PAGE_SIZE)
	{
		uint32 *ptr_page_table = NULL;


		struct Frame_Info *ptr_frame = get_frame_info(ptr_page_directory,(void*) va, &ptr_page_table);


		if (ptr_frame != NULL)
		{

			uint32 current_pa = to_physical_address(ptr_frame);


			if (current_pa == target_frame_pa)
			{

				return va + offset;
			}
		}
	}


	return 0;
}


unsigned int kheap_physical_address(unsigned int virtual_address)
{


	uint32 offset = virtual_address & 0xFFF;


	uint32 *ptr_page_table = NULL;
	struct Frame_Info *ptr_frame = get_frame_info(ptr_page_directory,(void*) virtual_address, &ptr_page_table);


	if (ptr_frame != NULL)
	{

		uint32 physical_address = to_physical_address(ptr_frame) + offset;
		return physical_address;
	}


	return 0;
}
