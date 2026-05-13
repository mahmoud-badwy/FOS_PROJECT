#include <inc/memlayout.h>
#include <kern/kheap.h>
#include <kern/memory_manager.h>

//2022: NOTE: All kernel heap allocations are multiples of PAGE_SIZE (4KB)
uint32 kheap_allocated_pages[(KERNEL_HEAP_MAX - KERNEL_HEAP_START) / PAGE_SIZE] = {0};
void* kmalloc(unsigned int size)
{
	// [1] Calculate the number of pages needed
	// Round up the size to the nearest multiple of PAGE_SIZE (4096 bytes)
	uint32 rounded_size = ROUNDUP(size, PAGE_SIZE);
	uint32 req_pages = rounded_size / PAGE_SIZE;

	if (req_pages == 0) {
		return NULL;
	}

	// Static pointer to remember where we left off (Next Fit strategy)
	static uint32 kheap_next_fit_ptr = KERNEL_HEAP_START;

	uint32 start_search_va = kheap_next_fit_ptr;
	uint32 current_va = start_search_va;
	uint32 free_pages_found = 0;
	uint32 allocated_start_va = 0;

	// [2] Search for continuous empty pages
	while (1) {

		// Check if the current virtual address is empty (not mapped to a frame)
		uint32 *ptr_page_table = NULL;

		struct Frame_Info *ptr_frame_info = get_frame_info(ptr_page_directory, (void*) current_va, &ptr_page_table);

		if (ptr_frame_info == NULL) {
			// This page is empty!
			if (free_pages_found == 0) {
				allocated_start_va = current_va; // Mark this as the potential start address
			}
			free_pages_found++;

			// Did we find enough consecutive pages?
			if (free_pages_found == req_pages) {
				break;
			}
		} else {
			// Page is occupied, we must reset our search because we need CONTIGUOUS memory
			free_pages_found = 0;
		}

		// Move to the next page
		current_va += PAGE_SIZE;

		// [3] Wrap around if we hit the upper limit of the kernel heap
		if (current_va >= KERNEL_HEAP_MAX) {
			current_va = KERNEL_HEAP_START;
			free_pages_found = 0; // Reset, because it's broken across the boundary
		}

		// If we looped all the way back to where we started, there is no enough memory
		if (current_va == start_search_va) {
			return NULL;
		}
	}

	// [4] Allocation and Mapping
	// We found the space! Now allocate physical frames and map them.
	for (uint32 i = 0; i < req_pages; i++) {
		uint32 va_to_map = allocated_start_va + (i * PAGE_SIZE);

		struct Frame_Info *new_frame;
		int ret = allocate_frame(&new_frame);

		if (ret != E_NO_MEM) {
			// Map the frame with Read/Write permissions
			map_frame(ptr_page_directory, new_frame,(void*) va_to_map, PERM_WRITEABLE | PERM_PRESENT);
		}
	}

	// [5] Update the Next Fit pointer for the next time kmalloc is called
	kheap_next_fit_ptr = allocated_start_va + rounded_size;

	// If the pointer reaches the end, wrap it back to the start
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

	// [1] Check if the address is within the valid Kernel Heap boundaries
	if (va < KERNEL_HEAP_START || va >= KERNEL_HEAP_MAX) {
		return;
	}

	// [2] Calculate the index to find out how many pages were allocated here
	uint32 index = (va - KERNEL_HEAP_START) / PAGE_SIZE;
	uint32 num_of_pages = kheap_allocated_pages[index];

	// If the size is 0, it means this address wasn't the start of an allocation
	if (num_of_pages == 0) {
		return;
	}

	// [3] Loop through the pages and free them
	for (uint32 i = 0; i < num_of_pages; i++) {
		uint32 current_va = va + (i * PAGE_SIZE);

		// unmap_frame automatically removes the page table entry
		// and frees the physical frame if its references drop to 0
		unmap_frame(ptr_page_directory,(void*) current_va);
	}

	// [4] Clear the size record from our tracking array
	kheap_allocated_pages[index] = 0;
}


unsigned int kheap_virtual_address(unsigned int physical_address)
{
	// [1] Calculate the offset and the aligned physical frame address
	// 0xFFF is 4095 (the offset inside a 4KB page)
	// 0xFFFFF000 clears the last 12 bits to get the start of the frame
	uint32 offset = physical_address & 0xFFF;
	uint32 target_frame_pa = physical_address & 0xFFFFF000;

	// [2] Loop through all virtual pages in the Kernel Heap
	for (uint32 va = KERNEL_HEAP_START; va < KERNEL_HEAP_MAX; va += PAGE_SIZE)
	{
		uint32 *ptr_page_table = NULL;

		// [3] Get the frame info for the current virtual address
		struct Frame_Info *ptr_frame = get_frame_info(ptr_page_directory,(void*) va, &ptr_page_table);

		// If the virtual page is actually mapped to a frame
		if (ptr_frame != NULL)
		{
			// Get the physical address this frame represents
			uint32 current_pa = to_physical_address(ptr_frame);

			// [4] Compare it with the frame we are looking for
			if (current_pa == target_frame_pa)
			{
				// [5] We found it! Add the exact offset to the starting virtual address
				return va + offset;
			}
		}
	}

	// Return 0 if the physical address is not found in the Kernel Heap
	return 0;
}


unsigned int kheap_physical_address(unsigned int virtual_address)
{
	// [1] Get the offset of the given virtual address inside its 4KB page
	// 0xFFF (4095) masks the lowest 12 bits
	uint32 offset = virtual_address & 0xFFF;

	// [2] Find the Frame_Info mapped to this virtual address
	uint32 *ptr_page_table = NULL;
	struct Frame_Info *ptr_frame = get_frame_info(ptr_page_directory,(void*) virtual_address, &ptr_page_table);

	// [3] Check if the virtual address is actually mapped to a physical frame
	if (ptr_frame != NULL)
	{
		// Convert the frame info into a physical base address, then add the offset
		uint32 physical_address = to_physical_address(ptr_frame) + offset;
		return physical_address;
	}

	// [4] Return 0 if the virtual address is not mapped to anything
	return 0;
}
