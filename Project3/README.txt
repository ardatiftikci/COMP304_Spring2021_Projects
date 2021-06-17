Part 1 (fully working): 
Declared constants and page & offset masks to later use in calculating the logical page number and page offset from the logical address; which are done by doing a bitwise AND operation on the logical address with the mask for the offset, and <<<moving the binary number by 10 bits to the right for the page number>>>. Then, if the physical page with this logical page number exists in TLB, a TLB hit occurs and there is no page fault. If not we look at the page table, if pagetable[logical_page] returns -1, there is a page fault. Otherwise physical_page = pagetable[logical_page]. When a page fault occurs, we get the corresponding physical page number (current free_page) and add it to the pagetable and TLB. Increment free_page by 1. Then copy from backing file to main_memory array with appropriate indexing as you can see in the code.

Part 2 (we are thinking it is fully working but we do not have answers so we could not check it):
First, when memory is full turn flag variable to 1. Then, each page fault requires a page replacement. For FIFO, free_page is an unsigned char (0 to 255), so current free_page number is actually to page to replace (this is what FIFO means). Also, find logical_page corresponding to page to replace then make its entry -1. 
For LRU keep an array for physical_pages and update the array with total_addresses (like time) in each reference. Then min value of this array is Least Recent Used Page, replace it.

Arda Tiftikçi 69395
Ömer Faruk Aksoy 68640 
