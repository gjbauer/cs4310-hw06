#ifndef PMALLOC_H
#define PMALLOC_H

// Panther Malloc Interface
// cs4310 Starter Code

typedef struct pm_stats {
    long pages_mapped;
    long pages_unmapped;
    long chunks_allocated;
    long chunks_freed;
    long free_length;
} pm_stats;

typedef struct node {
	size_t size;
	struct node *next;
	struct node *prev;
} node;

typedef struct header {
	size_t size;
	struct node *ptr;
} header;

pm_stats* pgetstats();
void pprintstats();

void* pmalloc(size_t size);
void pfree(void* item);
void freeuapages(void);
//void pnodemerge(void);

/*	Size Specitic Structures...	*/

typedef struct size24_block {
	size_t size;
	struct size24_block *next;
	size_t _unused;
} size24_block;

typedef struct size32_block {
	size_t size;
	struct size32_block *next;
	size_t _unused[2];
} size32_block;

typedef struct size40_block {
	size_t size;
	struct size40_block *next;
	size_t _unused[3];
} size40_block;

typedef struct size64_block {
	size_t size;
	struct size64_block *next;
	size_t _unused[6];
} size64_block;

typedef struct size72_block {
	size_t size;
	struct size72_block *next;
	size_t _unused[7];
} size72_block;

typedef struct size136_block {
	size_t size;
	struct size136_block *next;
	size_t _unused[15];
} size136_block;

typedef struct size264_block {
	size_t size;
	struct size264_block *next;
	size_t _unused[31];
} size264_block;

typedef struct size520_block {
	size_t size;
	struct size520_block *next;
	size_t _unused[63];
} size520_block;

typedef struct size1032_block {
	size_t size;
	struct size1032_block *next;
	size_t _unused[127];
} size1032_block;

#endif
