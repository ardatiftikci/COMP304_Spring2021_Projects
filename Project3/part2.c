/**
 * virtmem.c 
 */

#include <stdio.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define TLB_SIZE 16
#define VIRTUAL_PAGES 1024
#define PHYSICAL_PAGES 256
#define PAGE_MASK 0xffc00

#define PAGE_SIZE 1024
#define OFFSET_BITS 10
#define OFFSET_MASK 0x003ff

#define MEMORY_SIZE PHYSICAL_PAGES * PAGE_SIZE

// Max number of characters per line of input file to read.
#define BUFFER_SIZE 10

int flag = 0;
struct tlbentry {
	int logical;
	int physical;
};

// TLB is kept track of as a circular array, with the oldest element being overwritten once the TLB is full.
struct tlbentry tlb[TLB_SIZE];
// number of inserts into TLB that have been completed. Use as tlbindex % TLB_SIZE for the index of the next TLB line to use.
int tlbindex = 0;

// pagetable[logical_page] is the physical page number for logical page. Value is -1 if that logical page isn't yet in the table.
int pagetable[VIRTUAL_PAGES];

//keep total_address number (like current time) when a page is referenced
int counter_pagetable[PHYSICAL_PAGES];

signed char main_memory[MEMORY_SIZE];

// Pointer to memory mapped backing file
signed char *backing;

void fifo_page_replacement(unsigned char free_page){
	//find corresponding entry and make -1
	for (int i = 0; i < VIRTUAL_PAGES; i++) {
		if(pagetable[i]==free_page){
			pagetable[i]=-1;
			break;
		}
	}
}

unsigned char lru_page_replacement(){
	int min = counter_pagetable[0];
	unsigned char min_index = 0;
	//find least recent used
	for (int i = 0; i < PHYSICAL_PAGES; i++) {
		if(counter_pagetable[i]<min){
			min_index=i;
			min = counter_pagetable[i];
		}
	}
	//find corresponding entry and make -1
	for (int i = 0; i < VIRTUAL_PAGES; i++) {
		if(pagetable[i]==min_index){
			pagetable[i]=-1;
			break;
		}
	}
	
	return min_index;
}
int max(int a, int b)
{
	if (a > b) return a;
  	return b;
}

/* Returns the physical address from TLB or -1 if not present. */
int search_tlb(int logical_page) {
    for(int i = 0; i < TLB_SIZE; i++){
    	if(tlb[i].logical == logical_page) return tlb[i].physical;
    }
    return -1;
}

/* Adds the specified mapping to the TLB, replacing the oldest mapping (FIFO replacement). */
void add_to_tlb(int logical, int physical) {
	struct tlbentry new_entry = {logical, physical};
    tlb[tlbindex++ % TLB_SIZE] = new_entry;
}

int main(int argc, const char *argv[])
{
	if (argc != 5 && strcmp(argv[3],"-p")!=0) {
	fprintf(stderr, "Usage ./part2 backingstore input -p * (0 for FIFO or 1 for LRU)\n");
	exit(1);
	}
	int p = atoi(argv[4]);
	const char *backing_filename = argv[1]; 
	int backing_fd = open(backing_filename, O_RDONLY);
	backing = mmap(0, MEMORY_SIZE, PROT_READ, MAP_PRIVATE, backing_fd, 0); 

	const char *input_filename = argv[2];
	FILE *input_fp = fopen(input_filename, "r");

	// Fill page table entries with -1 for initially empty table.
	int i;
	for (i = 0; i < VIRTUAL_PAGES; i++) {
		pagetable[i] = -1;
	}
	
	for (i = 0; i < PHYSICAL_PAGES; i++) {
		counter_pagetable[i] = INT_MAX;
	}


	// Character buffer for reading lines of input file.
	char buffer[BUFFER_SIZE];

	// Data we need to keep track of to compute stats at end.
	int total_addresses = 0;
	int tlb_hits = 0;
	int page_faults = 0;
	// Number of the next unallocated physical page in main memory
	unsigned char free_page = 0;

	while (fgets(buffer, BUFFER_SIZE, input_fp) != NULL) {
		total_addresses++;
		int logical_address = atoi(buffer);

		/* Calculate the page offset and logical page number from logical_address */
		int offset = logical_address & OFFSET_MASK;
		int logical_page = (logical_address & PAGE_MASK) >> OFFSET_BITS;
		int physical_page = search_tlb(logical_page);

		// TLB hit
		if (physical_page != -1) {
	  		tlb_hits++;
	  		// TLB miss
		} else {
	  		physical_page = pagetable[logical_page];
	 		// Page fault
	 		if (physical_page == -1) {
		 		page_faults++;
	  	 		if(flag){
	  	 		//page replacement
	  	 			if(p){
	  	 				free_page = lru_page_replacement();
	  	 				//below is same with part 1
						memcpy(main_memory + free_page*PAGE_SIZE, backing + logical_page * PAGE_SIZE, PAGE_SIZE);
						physical_page = free_page;
						pagetable[logical_page] = physical_page;
	  	 			}else{
	  	 				fifo_page_replacement(free_page);
	  	 				//below is same with part 1
	  	 				memcpy(main_memory + free_page*PAGE_SIZE, backing + logical_page * PAGE_SIZE, PAGE_SIZE);
	  	 				physical_page = free_page++;
						pagetable[logical_page] = physical_page;
	  	 			}
	  	 		}else{
		  	 		memcpy(main_memory + free_page*PAGE_SIZE, backing + logical_page * PAGE_SIZE, PAGE_SIZE);
		  	 		if(free_page==255) flag = 1;
		  			physical_page = free_page++;
					pagetable[logical_page] = physical_page;
				}
			}
			add_to_tlb(logical_page, physical_page);
		}
		counter_pagetable[physical_page] = total_addresses;//like counter implementation of LRU.
		int physical_address = (physical_page << OFFSET_BITS) | offset;
		signed char value = main_memory[physical_address];
		printf("Virtual address: %d Physical address: %d Value: %d\n", logical_address, physical_address, value);

	}


	printf("Number of Translated Addresses = %d\n", total_addresses);
	printf("Page Faults = %d\n", page_faults);
	printf("Page Fault Rate = %.3f\n", page_faults / (1. * total_addresses));
	printf("TLB Hits = %d\n", tlb_hits);
	printf("TLB Hit Rate = %.3f\n", tlb_hits / (1. * total_addresses));

	return 0;
}
