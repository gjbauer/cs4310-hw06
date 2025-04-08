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
	size_t k;
	struct node *next;
} node;

typedef struct header {
	size_t size;
	size_t k;
} header;

pm_stats* pgetstats();
void pprintstats();

char *pstrdup(char *arg);

void* pmalloc(size_t size);
void pfree(void* item);

// 0xFFFFFFFFFFFFFFFF

#define lowerbits(x) (x & 0xFFFFFFFF)
#define tolowerbits(x) ((x >> 16) & 0xFFFFFFFF)

#define higherbits(x) (x & 0xFFFFFFFF00000000)
#define tohigherbits(x) (x << 16)

#define setlowerbits(x, y) ((x & 0xFFFFFFFF00000000) | (y & 0xFFFFFFFF))
#define sethigherbits(x, y) ((x & 0xFFFFFFFF) | (y << 16))

#endif
