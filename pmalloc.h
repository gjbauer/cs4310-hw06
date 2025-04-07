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
} node;

typedef struct header {
	size_t size;
} header;

pm_stats* pgetstats();
void pprintstats();

char *pstrdup(char *arg);

void* pmalloc(size_t size);
void pfree(void* item);

typedef struct size32_block {
	size_t size;
	node *next;
	size_t _unused[2];
} size32_block;

typedef struct size64_block {
	size_t size;
	struct size64_block *next;
	size_t _unused[6];
} size64_block;

typedef struct size128_block {
	size_t size;
	struct size128_block *next;
	size_t _unused[14];
} size128_block;

typedef struct size256_block {
	size_t size;
	struct size256_block *next;
	size_t _unused[30];
} size256_block;

typedef struct size512_block {
	size_t size;
	struct size512_block *next;
	size_t _unused[62];
} size512_block;

typedef struct size1024_block {
	size_t size;
	struct size1024_block *next;
	size_t _unused[126];
} size1024_block;

typedef struct size2048_block {
	size_t size;
	struct size1024_block *next;
	size_t _unused[254];
} size2048_block;

#endif
