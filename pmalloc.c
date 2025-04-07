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
pownec(int bytes) {
	int i;
	for(i=0; pow(2, i) < bytes; i++);
	return i;
}

int
nextfreelist() {
	int i;
	for(i=0;array[i]!=0;i++);
	return i;
}

int
nextbucket(int pow) {
	int i;
	for(i=pow;!(powers[i]>0x0)&&(i<8); i++);
	if(i==8) return -1;
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
	int l = findlist(ptr, list);
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
mapnextpage() {
	int k = nextfreelist();
	array[k]=mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
	array[k]->size = 4096;
	stats.pages_mapped += 1;
	return k;
}

void
bucketpage() {
	powers[7]=mmap(0, 4096, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_SHARED, -1, 0);
	powers[7]->size = 4096;
	stats.pages_mapped += 1;
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
	size_t* b = (size_t*)powers[n] + bytes/8;
	*b=bytes;
}

void
divide(int top, int dest) {
	if (top==dest) return;
	else {
		node *k = powers[top];
		powers[top]=powers[top]->next;
		int s = pow(2, top+4);
		size_t* b = (size_t*)k + s;
		node *l = (node*)b;
		
		l->size=s;
		k->size=s;
		
		k->next=l;
		powers[top-1]=k;
		
		divide(top-1, dest);
	}
}

void
bucket_merge(int p) {
	node *curr = powers[p];
	node *prev = NULL;
	while (curr) {
		if ((node*)((char*)curr+curr->size)==(node*)((char*)curr->next)&&curr->size<PAGE_SIZE) {
			curr->size+=curr->next->size;
			curr->next=curr->next->next;
		} else {
			curr = curr->next;
		}
	}
}

void
bucket_insert(node *block, int b) {
	node *prev = walkp(block, powers[b]);
	if (prev) {
		block->next=prev->next;
		prev->next=block;
		bucket_merge(b);
	}
	else
		powers[b] = block;
}

void*
bucket_malloc(size_t size) {
	size_t *r;
	int p = pownec(size)-4;
	int n = nextbucket(p);
	if(n==-1) n=7, bucketpage();
	if (n!=p) {
		divide(n, p);
		r = (size_t*)powers[p];
		size_t k=pow(2, p+4);
		powers[p]=powers[p]->next, powers[p]->size = k, *r=k;
		stats.chunks_allocated += 1;
		return r + 1;
	}
	else {
		r=(size_t*)powers[p], powers[p]=powers[p]->next;
		return r+1;
	}
}

void
bucket_free(void *ptr) {
	stats.chunks_freed += 1;
	node *k = (node*)ptr;
	int p = pownec(k->size)-4;
	bucket_insert(k, p);
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

void*
big_malloc(size_t size) {
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

void
big_free(void *ptr) {
	size_t *p = (size_t*)ptr;
	stats.chunks_freed += 1;
	stats.pages_unmapped+=*p/PAGE_SIZE;
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

/*
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
*/

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

/*void* size32_malloc() {
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
}*/

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

/*void* size64_malloc() {
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
}*/

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
  //printf("xfree(%ld)\n", *ptr);
  switch (*ptr) {
  case 24:
  	size_free(ptr);
  	break;
  case 40:
  	size_free(ptr);
  	break;
  //case 63:
  //	size_free(ptr);
  //	break;
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
  nbytes += sizeof(size_t);
  if (nbytes<24) nbytes=24;
  printf("xmalloc(%ld)\n", nbytes);
  switch (nbytes) {
  case 24:
  	return size24_malloc();
  	break;
  case 40:
  	return size40_malloc();
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

