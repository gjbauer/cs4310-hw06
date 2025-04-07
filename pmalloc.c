#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/personality.h>
#endif
#include <stdbool.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <stdint.h>

bool first_run = true;

#include "pmalloc.h"

const size_t PAGE_SIZE = 4096;
static pm_stats stats;  
static __thread void *mem = NULL;
static node **array = NULL;

char *pstrdup(char *arg) {
        int i=0;
        for(; arg[i]!=0; i++);
        char *buf = pmalloc(i+1);
        for(int j=0; j<=i; j++) buf[j]=arg[j];
        return buf;
}

long free_list_length() {
    long length = 0;
    while (array[0]) {
    	//printf("node at : %u\n", ((char*)array));
        length++;
        array[0] = array[0]->next;
    }
    stats.free_length += length;
    return length;
}

pm_stats* pgetstats() {
    stats.free_length = free_list_length();
    return &stats;
}

void pprintstats() {
    stats.free_length = free_list_length();
    fprintf(stderr, "\n== Panther Malloc Stats ==\n");
    fprintf(stderr, "Mapped:   %ld\n", stats.pages_mapped);
    fprintf(stderr, "Unmapped: %ld\n", stats.pages_unmapped);
    fprintf(stderr, "Allocs:   %ld\n", stats.chunks_allocated);
    fprintf(stderr, "Frees:    %ld\n", stats.chunks_freed);
    fprintf(stderr, "Freelen:  %ld\n", stats.free_length);
}

static size_t div_up(size_t xx, size_t yy) {
    size_t zz = xx / yy;
    if (zz * yy == xx) {
        return zz;
    } else {
        return zz + 1;
    }
}

int
findlist(void *list) {
	int i;
	for(i=0;((uintptr_t)list/4096)!=((uintptr_t)array[i]/4096)&&array[i];i++);
	return i;
}

int
nextfreelist() {
	int i;
	for(i=0;array[i]!=0;i++);
	return i;
}

node*
walk(node *block, void *list) {
	node *next = (node*)list;
	while ((char*)block>(char*)next&&next) {	// Kepp the blocks sorted by where they appear in memory ;)
		next = next->next;
	}
	return next;
}

void
addtolist(void* ptr, void* list) {
	int l = findlist(list);
	node *block = (node*)ptr;
	node *next = walk(block, list);
	if (next) {
		if (next->prev) {
			block->prev=next->prev;
			block->next=next;
			block->prev->next=block;
			block->next->prev=block;
			return;
		}
	}
	block->next = next;
	array[l] = block;
}

bool
canbump(void* ptr, void* list, size_t size) {
	return ((char*)ptr)==((char*)list+size) ? true : false;
}

bool
canbumpd(void* ptr, void* list, size_t size) {
	node *l = (node*)list;
	return ((char*)ptr+size)==((char*)l+l->size) ? true : false;
}

void
bump(void* list, size_t size) {
	node *l = (node*)list;
	int k = l->size+size;
	l = (node*)((char*)list+size);
	l->size=k;
}

void
bumpd(void* list, size_t size) {
	node *l = (node*)list;
	l->size+=size;
}

int
mapnextpage() {
	int k = nextfreelist();
	array[k]=mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
	array[k]->size = 4096;
	stats.pages_mapped += 1;
	return k;
}

void
pnodemerge(int list) {
	node *curr = array[list];
	node *prev = NULL;
	while (curr) {
		if ((node*)((char*)curr+curr->size)==(node*)((char*)curr->next)&&curr->size<PAGE_SIZE) {
			if (curr->next) {
				curr->size+=curr->next->size;
				curr->next=curr->next->next;
			}
		}
		else {
			curr = curr->next;
		}
	}
}

void
freepages(void* list) {
	node *temp = (node*)list;
	if (temp->size==4096) {
		list = temp->next;
		munmap(temp, 4096);
	}
}

void pfree_helper(void* item) {
    stats.chunks_freed += 1;
    node* block = (node*)item; // Get the header part
    int k = findlist(item);
    if (block->size<PAGE_SIZE) {
	addtolist(item, array[k]);
	pnodemerge(k);	// Run this command everytime you call free to merge mergeable sections...
    } else {
    	stats.pages_unmapped+=block->size/PAGE_SIZE;
    	munmap(&block, block->size);
    }
}

void
climb(int k, int bytes) {
	int size=array[k]->size-bytes;
	array[k] = (node*)((char*)array[k]+bytes);
	array[k]->size=size;
}

void* pmalloc_helper(size_t size) {
    #ifdef __linux__
    personality(ADDR_NO_RANDOMIZE);
    #endif
    size += sizeof(size_t); // Add space for storing the size.
    static int k = 0;
    size_t* ptr=0;
    
    if ((char*)array[k]==0x0&&size<=4096) {
    	k = mapnextpage();
    } 

    if (size < PAGE_SIZE) {
        // Try to find a free block in the list
        node* prev = NULL;
        node* curr = array[k]; // Head of free list
        if (curr->size<sizeof(node*)+size&&curr->next==0) k = mapnextpage();
        while (curr) {
            if (curr->size > size) {	// Found a large enough block
                if (prev) {
                    if (curr->next==NULL&&curr->size-size<=sizeof(node*)) k = mapnextpage();
                    else if (curr->next==NULL) climb(k, size);
		    else prev->next = curr->next;
                } else {
                    if (curr->size-size<=sizeof(node*)) k = mapnextpage();
                    else {
                    	ptr = (void*)array[k];
                    	climb(k, size);
                    	*ptr=size;
                    }
                }
                stats.chunks_allocated += 1;
                return ptr + 1;
            }
            prev = curr;
            curr = curr->next;
        }

        // No suitable block found, allocate new page
        k = mapnextpage();
        
        ptr = (void*)array[k];
	climb(k, size);
	*ptr=size;
	
	stats.pages_mapped += 1;
	stats.chunks_allocated += 1;
	return ptr + 1;
    }

    // Handle large allocation (>= 1 page)
    size_t pages_needed = div_up(size, PAGE_SIZE);
    node* new_block = mmap(0, pages_needed * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (new_block == MAP_FAILED) {
        perror("mmap failed");
        exit(EXIT_FAILURE);
    }
    stats.pages_mapped += pages_needed;
    new_block->size = pages_needed * PAGE_SIZE;
    stats.chunks_allocated += 1;
    return new_block + 1; // Return pointer after size header
}

/*void printflist() {
	node *curr = mem;
	while (curr) {
		printf("node at : %u\n", (unsigned int)((char*)curr));
		printf("reporting size : %lu\n", curr->size);
		curr=curr->next;
	}
}*/

/* - Size Specific Allocs and Frees - */

// (uintptr_t)a / 4096 == ( uintptr_t ) b / 4096

void size32_free(void* ptr) {
	stats.chunks_freed += 1;
	int k = findlist(ptr);
	addtolist(ptr, array[k]);
	pnodemerge(k);
}

void* size32_malloc() {
	//printf("32malloc\n");
	static int k = 0;
	if (array[k]==0) {
		k = mapnextpage();
	}
	if (array[k]->size>32) {
		size_t* ptr = (void*)array[k];
		array[k] = (node*)((char*)array[k]+32);
		array[k]->size = *ptr-32;
		*ptr=32;
		stats.chunks_allocated += 1;
		return ptr + 1;
	} else {
		k = mapnextpage();
		return size32_malloc();
	}
}

/* 40, 64, 72, 136, 264, 520, 1032 */


void
pfree(void* ap)
{
  size_t *ptr = (size_t*)ap - 1;
  switch (*ptr) {
  case 32:
  	size32_free(ptr);
  	break;
  /*case 40:
  	size40_free(ptr);
  	break;
  case 48:
  case 72:
  	size64_free(ptr);
  	break;
  case 80:
  	size72_free(ptr);
  	break;
  case 144:
  	size136_free(ptr);
  	break;
  case 272:
  	size264_free(ptr);
  	break;
  case 528:
  	size520_free(ptr);
  	break;
  case 1040:
  	size1032_free(ptr);
  	break;*/
  default:
  	//printf("size : %d\n", *ptr);
  	pfree_helper(ptr);
  	break;
  }
}


void*
pmalloc(size_t nbytes)
{
  if (first_run == true) {
  	personality(ADDR_NO_RANDOMIZE);
  	array=mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
  	for(int i=0;i<4096/sizeof(node*);i++) array[i]=0;
  	first_run = false;
  }
  nbytes += sizeof(header);
  //printf("xmalloc(%ld)\n", nbytes);
  switch (nbytes) {
  case 32:
  	return size32_malloc();
  	break;
  /*case 40:
  	return size40_malloc();
  	break;
  case 64:
  	return size64_malloc();
  	break;
  case 72:
  	return size72_malloc();
  	break;
  case 136:
  	return size136_malloc();
  	break;
  case 264:
  	return size264_malloc();
  	break;
  case 520:
  	return size520_malloc();
  	break;
  case 1032:
  	return size1032_malloc();
  	break;*/
  default:
  	return pmalloc_helper(nbytes);
  	break;
  }
}


/*int main() {
    // Example usage of the custom malloc/free
    void* p1 = pmalloc(100);
    void* p2 = pmalloc(200);
    pfree(p1);
    pfree(p2);
	printflist();
	//pnodemerge();
	//printflist();
    // Print stats
    pprintstats();
    return 0;
}*/

