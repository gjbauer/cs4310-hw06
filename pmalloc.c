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
#include <math.h>

bool __thread first_run = true;

#include "pmalloc.h"

const size_t PAGE_SIZE = 4096;
static pm_stats stats;
static __thread node **array = NULL;
static __thread node *powers[8] = {0};

int
findlist(void* src, node **list) {
	int i;
	for(i=0;((uintptr_t)src/4096)!=((uintptr_t)list[i]/4096)&&list[i];i++);
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
	if (block==NULL) {
		while (next->next>0x0) {
			next = next->next;
		}
	}
	else {
		while ((char*)block>(char*)next&&next) {
			next = next->next;
		}
	}
	return next;
}

void
addtolist(void* ptr, node** list) {
	int l = findlist(ptr, list);
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
climb(int n, int bytes) {
	int size=array[n]->size-bytes;
	array[n] = (node*)((char*)array[n]+bytes);
	array[n]->size=size;
}

void
pclimb(int n, int bytes) {
	int size=powers[n]->size-bytes;
	powers[n] = (node*)((char*)powers[n]+bytes);
	powers[n]->size=size;
}

node*
pop(node* list) {
	node *block = walk(NULL, list);
	block->prev->next = NULL;
	return block;
}

void
bucketpage() {
	powers[7]=mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
	powers[7]->size = 4096;
	stats.pages_mapped += 1;
}

int
pownec(int bytes) {
	int i;
	for(i=0; pow(2, i) < bytes; i++);
	return i;
}

int
nextbucket(int pow) {
	int i;
	for(i=pow;!(powers[i]>0x0)&&(i<8); i++);
	if(i==8) return -1;
	return i;
}

void
divide(int top, int dest) {
	if (top==dest) return;
	node *k = powers[top];
	int s = pow(2, top+3);
	pclimb(top, s);
	k->next=powers[top];
	powers[top-1]=k;
	powers[top]=NULL;
	divide(top-1, dest);
}

void*
bucket_malloc(size_t size) {
	int p = pownec(size)-4;
	int n = nextbucket(p);
	if(n==-1) n=7, bucketpage();
	if (n!=p) divide(n, p);
	size_t *r = (size_t*)powers[p];
	powers[p]=powers[p]->next;
	return r + 1;
}

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

void pfree_helper(void* item) {
    stats.chunks_freed += 1;
    node* block = (node*)item;
    int k = findlist(item, array);
    if (block->size<PAGE_SIZE) {
	addtolist(item, array);
	//pnodemerge(k);	// Merge
    } else {
    	stats.pages_unmapped+=block->size/PAGE_SIZE;
    	munmap(&block, block->size);
    }
}

void* pmalloc_helper(size_t size) {
    #ifdef __linux__
    personality(ADDR_NO_RANDOMIZE);
    #endif
    size += sizeof(size_t); // Header.
    static int k = 0;
    size_t* ptr=0;
    
    if (array[k]==0&&size<=4096) {
    	k = mapnextpage();
    } 

    if (size < PAGE_SIZE) {
        node* prev = NULL;
        node* curr = array[k]; // Head of free list
        while (curr) {
            if (curr->size > size) {	// Found a large enough block
                if (prev) {
                    if (curr->next==NULL&&curr->size-size<=sizeof(node*)) k = mapnextpage();
                    else if (curr->next==NULL) climb(k, size);
		    else prev->next = curr->next;
                } else {
                    if (curr->size-size<=sizeof(node*)) k = mapnextpage();
                    else {
                    	ptr = (size_t*)array[k];
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
        
        ptr = (size_t*)array[k];
	climb(k, size);
	*ptr=size;
	
	stats.pages_mapped += 1;
	stats.chunks_allocated += 1;
	return ptr + 1;
    }

    // Handle large allocation (>= 1 page)
    size_t pages_needed = div_up(size, PAGE_SIZE);
    size_t* new_block = mmap(0, pages_needed * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (new_block == MAP_FAILED) {
        perror("mmap failed");
        exit(EXIT_FAILURE);
    }
    stats.pages_mapped += pages_needed;
    stats.chunks_allocated += 1;
    *new_block = pages_needed * PAGE_SIZE;
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

void size_free(void* ptr) {
	stats.chunks_freed += 1;
	int k = findlist(ptr, array);
	addtolist(ptr, array);
	//pnodemerge(k);
}

void* size24_malloc() {
	static int k = 0;
	if (array[k]==0) {
		k = mapnextpage();
	}
	if (array[k]->size>24) {
		size_t* ptr = (void*)array[k];
		array[k] = (node*)((char*)array[k]+24);
		array[k]->size = *ptr-24;
		*ptr=24;
		stats.chunks_allocated += 1;
		return ptr + 1;
	} else {
		k = mapnextpage();
		return size24_malloc();
	}
}

void* size32_malloc() {
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

void* size40_malloc() {
	static int k = 0;
	if (array[k]==0) {
		k = mapnextpage();
	}
	if (array[k]->size>40) {
		size_t* ptr = (void*)array[k];
		array[k] = (node*)((char*)array[k]+40);
		array[k]->size = *ptr-40;
		*ptr=40;
		stats.chunks_allocated += 1;
		return ptr + 1;
	} else {
		k = mapnextpage();
		return size40_malloc();
	}
}

void* size64_malloc() {
	static int k = 0;
	if (array[k]==0) {
		k = mapnextpage();
	}
	if (array[k]->size>64) {
		size_t* ptr = (void*)array[k];
		array[k] = (node*)((char*)array[k]+64);
		array[k]->size = *ptr-64;
		*ptr=64;
		stats.chunks_allocated += 1;
		return ptr + 1;
	} else {
		k = mapnextpage();
		return size64_malloc();
	}
}

void* size72_malloc() {
	static int k = 0;
	if (array[k]==0) {
		k = mapnextpage();
	}
	if (array[k]->size>72) {
		size_t* ptr = (void*)array[k];
		array[k] = (node*)((char*)array[k]+72);
		array[k]->size = *ptr-72;
		*ptr=72;
		stats.chunks_allocated += 1;
		return ptr + 1;
	} else {
		k = mapnextpage();
		return size72_malloc();
	}
}

void* size136_malloc() {
	static int k = 0;
	if (array[k]==0) {
		k = mapnextpage();
	}
	if (array[k]->size>136) {
		size_t* ptr = (void*)array[k];
		array[k] = (node*)((char*)array[k]+136);
		array[k]->size = *ptr-136;
		*ptr=136;
		stats.chunks_allocated += 1;
		return ptr + 1;
	} else {
		k = mapnextpage();
		return size136_malloc();
	}
}

void* size264_malloc() {
	static int k = 0;
	if (array[k]==0) {
		k = mapnextpage();
	}
	if (array[k]->size>264) {
		size_t* ptr = (void*)array[k];
		array[k] = (node*)((char*)array[k]+264);
		array[k]->size = *ptr-264;
		*ptr=264;
		stats.chunks_allocated += 1;
		return ptr + 1;
	} else {
		k = mapnextpage();
		return size264_malloc();
	}
}

void* size520_malloc() {
	static int k = 0;
	if (array[k]==0) {
		k = mapnextpage();
	}
	if (array[k]->size>520) {
		size_t* ptr = (void*)array[k];
		array[k] = (node*)((char*)array[k]+520);
		array[k]->size = *ptr-520;
		*ptr=520;
		stats.chunks_allocated += 1;
		return ptr + 1;
	} else {
		k = mapnextpage();
		return size520_malloc();
	}
}

/* 40, 64, 72, 136, 264, 520, 1032 */


void
pfree(void* ap)
{
  size_t *ptr = (size_t*)ap - 1;
  switch (*ptr) {
  case 32:
  	size_free(ptr);
  	break;
  case 40:
  	size_free(ptr);
  	break;
  case 63:
  	size_free(ptr);
  	break;
  case 72:
  	size_free(ptr);
  	break;
  case 136:
  	size_free(ptr);
  	break;
  case 264:
  	size_free(ptr);
  	break;
  case 520:
  	size_free(ptr);
  	break;
  default:
  	//pfree_helper(ptr);
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
  case 24:
  	return size24_malloc();
  	break;
  case 32:
  	return size32_malloc();
  	break;
  case 40:
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
  default:
  	return bucket_malloc(nbytes);
  	break;
  }
}

int main() {
    // Example usage of the custom malloc/free
    char* p1 = pmalloc(100);
    void* p2 = pmalloc(200);
    for(int i=0; i<50; i++) p1[i]='h';
    printf("%s\n", p1);
    //pfree(p1);
    //pfree(p2);
	//printflist();
	//pnodemerge();
	//printflist();
    // Print stats
    pprintstats();
    return 0;
}

