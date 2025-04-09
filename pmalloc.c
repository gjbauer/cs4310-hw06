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

const size_t PAGE_SIZE = 1.3*4096;
static pm_stats stats;
static __thread node **array;
static __thread node **size24s;

int
nextfreelist(node **list) {
	int i;
	for(i=0;list[i]!=0;i++);
	return i;
}

void
pnodemerge(node **list, int l) {
	node *curr = list[l];
	node *prev = NULL;
	while (curr) {
		if (((char*)curr+curr->size)==((char*)curr->next)&&curr->size<PAGE_SIZE) {
			if (curr->next) {
				curr->size+=curr->next->size;
				curr->next=curr->next->next;
			}
		}
		else curr = curr->next;
	}
}

void
addtolist(void* ptr, int l) {
	node *block = (node*)ptr;
	node *curr = array[l];
	node *prev = NULL;
	while ((void*)block>(void*)curr&&curr) {	// Kepp the blocks sorted by where they appear in memory ;)
		prev = curr;
		curr = curr->next;
	}
	if (prev) {
		prev->next = block;
		block->next = curr;
	}
	else {
		block->next = array[l];
		array[l] = block;
	}
	pnodemerge(array, l);	// Merge
	node *p = array[l];
	if (p->size == PAGE_SIZE) {
		stats.pages_unmapped += 1;
		munmap(&p, p->size);
		array[l]=0;
	}
}

int
morecore() {
	int k = nextfreelist(array);
	array[k] = mmap(0, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
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

char *pstrdup(char *arg) {
        int i=0;
        for(; arg[i]!=0; i++);
        char *buf = pmalloc(i+1);
        for(int j=0; j<=i; j++) buf[j]=arg[j];
        return buf;
}

long list_length(node *k) {
    long length = 0;
    while (k) {
        length++;
        k = k->next;
    }
    return length;
}

long free_list_length() {
    long length = 0;
    for (int i=0;array[i]; i++) {
        length+=list_length(array[i]);
    }
    for (int i=0;size24s[i]; i++) {
        length+=list_length(size24s[i]);
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
    if (stats.pages_unmapped > 600) stats.pages_unmapped/=2;
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
    return new_block + 2; // Return pointer after size header
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
	if (size<=PAGE_SIZE) {
			static int k = -1;
		if (k == -1) {
			k = morecore();
		}
	
		int i=0;
		for(; array[i] && i<=k;i++) {
			if(array[i]->size>=size) {
				node *ptr = array[i];
				push(i, size);
				ptr->size = size;
				ptr->k = k;
				stats.chunks_allocated+=1;
				return (size_t*)ptr + 2;
			}
		}
		k = morecore();
		return pmalloc_helper(size);
	}
	else return big_malloc(size);
}

void
pfree_helper(void *ptr) {
	size_t *p = (size_t*)ptr;
	if(*p<=PAGE_SIZE) {
		stats.chunks_freed += 1;
		node *p = (node*)ptr;
		addtolist(ptr, p->k);
	}
	else big_free(ptr);
}

/* - Size Specific Allocs and Frees - */

int
findlist(void* src, node **list) {
	int i;
	for(i=0;((uintptr_t)src/4096)!=((uintptr_t)list[i]/4096)&&list[i];i++);
	return i;
}

int
lesscore(node** list) {
	int k = nextfreelist(list);
	list[k] = mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
	stats.pages_mapped += 1;
	list[k]->size=4096;
	return k;
}

void
addto24s(void* ptr) {
	int k = findlist(ptr, size24s);
	node *block = (node*)ptr;
	node *curr = size24s[k];
	node *prev = NULL;
	while ((void*)block>(void*)curr&&curr) {	// Kepp the blocks sorted by where they appear in memory ;)
		prev = curr;
		curr = curr->next;
	}
	if (prev) {
		prev->next = block;
		block->next = curr;
	}
	else {
		block->next = size24s[k];
		size24s[k] = block;
	}
	pnodemerge(size24s, k);	// Run this command everytime you call free to merge mergeable sections...
	node *p = size24s[k];
	if (p->size == 4096) {
		stats.pages_unmapped += 1;
		munmap(&p, 4096);
		if (k==1)
			size24s[k]=0;
	}
}

void size_free(void* ptr, int l) {
	stats.chunks_freed += 1;
	switch (l) {
		case 24:
			addto24s(ptr);
			break;
	}
}

void* size24_malloc() {
	static int k = -1;
	size_t* ptr;
	if (k==-1) {
		k = lesscore(size24s);
	}
	ptr = (size_t*)size24s[k];
	if (size24s[k]->size>40) {
		size24s[k] = (node*)((char*)size24s[k]+24);
		size24s[k]->size = *ptr-24;
		*ptr=24;
		stats.chunks_allocated += 1;
		return ptr + 1;
	}
	k = lesscore(size24s);
	return size24_malloc();
}

/* 40, 64, 72, 136, 264, 520, 1032 */


void
pfree(void* ap)
{
  size_t *ptr = (size_t*)ap - 1;
  //printf("xfree(%ld)\n", *ptr);
  switch (*ptr) {
  case 24:
  	return size_free(ptr, *ptr);
  	break;
  default:
  	*ptr--;
  	pfree_helper(ptr);
  	break;
  }
}


void*
pmalloc(size_t nbytes)
{
  if (first_run == true) {
  	personality(ADDR_NO_RANDOMIZE);
  	array=mmap(0, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
  	size24s=mmap(0, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
  	first_run = false;
  }
  nbytes += sizeof(size_t);
  //printf("xmalloc(%ld)\n", nbytes);
  switch (nbytes) {
  	case 24:
  		return size24_malloc();
  		break;
  	default:
  		nbytes -= sizeof(size_t);
  		nbytes += sizeof(header);
  		return pmalloc_helper(nbytes);
  	break;
  }
}

