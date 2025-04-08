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
#include <string.h>

bool __thread first_run = true;

#include "pmalloc.h"

const size_t PAGE_SIZE = 8192;
size_t mul = 1;
static pm_stats stats;
static __thread node **array;

int
findlist(void* src) {
	int i;
	for(i=0;((uintptr_t)src/4096)!=((uintptr_t)array[i]/4096)&&array[i];i++);
	return i;
}

int
nextfreelist() {
	int i;
	for(i=0;array[i]!=0;i++);
	return i;
}

node*
walkp(node *block, void *list) {
	node *next = (node*)list;
	node *prev = NULL;
	if (block==NULL) {
		while (next->next>0x0) {
			next = next->next;
		}
	}
	else {
		while ((char*)block>(char*)next&&next) {
			prev = next;
			next = next->next;
		}
	}
	return prev;
}

void
addtolist(void* ptr, node** list) {
	int l = findlist(ptr);
	node *block = (node*)ptr;
	node *prev = walkp(block, list);
	if (prev) {
		block->next=prev->next;
		prev->next=block;
		return;
	} else if (array[l]) {
		block->next=array[l];
		array[l]=block;
	}
	else array[l] = block;
}

int
morecore() {
	int k = nextfreelist();
	array[k] = mmap(0, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
	stats.pages_mapped += 1;
	array[k]->size=PAGE_SIZE;
	return k;
}

int
lesscore() {
	int k = nextfreelist();
	array[k] = mmap(0, PAGE_SIZE/2, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
	stats.pages_mapped += 1;
	array[k]->size=PAGE_SIZE;
	return k;
}

void
push(int k, int size) {
	size_t s = array[k]->size;
	array[k] = (node*)((char*)array[k]+size);
 	array[k]->size = s - size;
}

void*
bucket_malloc(size_t size) {
	static int k = -1;
	if (k == -1) {
		k = morecore();
	}
	
	int i=0;
	for(; array[i] && i<=k;i++) {
		if(array[i]->size>=size+sizeof(node*)) {
			size_t *ptr = (size_t*)array[i];
			push(i, size);
			*ptr = size;
			stats.chunks_allocated+=1;
			return ptr + 1;
		}
	}
	k = morecore();
	return bucket_malloc(size);
}

void
bucket_free(void *ptr) {
	stats.chunks_freed += 1;
	addtolist(ptr, array);
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
    //stats.free_length = free_list_length();
    return &stats;
}

void pprintstats() {
    //stats.free_length = free_list_length();
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

void*
big_malloc(size_t size) {
    // Handle large allocation (>= 1 page)
    size_t pages_needed = div_up(size, 4096);
    size_t* new_block = mmap(0, pages_needed * PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (new_block == MAP_FAILED) {
        perror("mmap failed");
        exit(EXIT_FAILURE);
    }
    stats.pages_mapped += pages_needed;
    stats.chunks_allocated += 1;
    *new_block = pages_needed * 4096;
    return new_block + 1; // Return pointer after size header
}

void
big_free(void *ptr) {
	size_t *p = (size_t*)ptr;
	stats.chunks_freed += 1;
	stats.pages_unmapped+=*p/4096;
	munmap(ptr, *p);
}

void*
pmalloc_helper(size_t size) {
	if (size<=PAGE_SIZE) return bucket_malloc(size);
	else return big_malloc(size);
}

void
pfree_helper(void *ptr) {
	size_t *p = (size_t*)ptr;
	if(*p>PAGE_SIZE) big_free(ptr);
	else bucket_free(ptr);
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
	//int k = findlist(ptr, array);
	addtolist(ptr, array);
	//pnodemerge(k);
}

void* size24_malloc() {
	static int k = -1;
	static int pos = 0;
	if (pos>24) {
		size_t* ptr = (void*)array[k];
		array[k] = (node*)((char*)array[k]+24);
		array[k]->size = *ptr-24;
		pos -= 24;
		*ptr=24;
		stats.chunks_allocated += 1;
		return ptr + 1;
	} else {
		k = lesscore();
		pos = 4096;
		return size24_malloc();
	}
}

/* 40, 64, 72, 136, 264, 520, 1032 */


void
pfree(void* ap)
{
  size_t *ptr = (size_t*)ap - 1;
  //printf("xfree(%ld)\n", *ptr);
  switch (*ptr) {
  case 24:
  	return size_free(ptr);
  	break;
  default:
  	if(*ptr>PAGE_SIZE) big_free(ptr);
  	else bucket_free(ptr);
  	break;
  }
}


void*
pmalloc(size_t nbytes)
{
  if (first_run == true) {
  	personality(ADDR_NO_RANDOMIZE);
  	array=mmap(0, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
  	first_run = false;
  }
  nbytes += sizeof(size_t);
  //printf("xmalloc(%ld)\n", nbytes);
  switch (nbytes) {
  case 24:
  	return size24_malloc();
  	break;
  default:
  	return pmalloc_helper(nbytes);
  	break;
  }
}

/*int main() {
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
}*/

