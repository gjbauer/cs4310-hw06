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

void* pmalloc(size_t size);
void pfree(void* item);
void pnodemerge(void);

#endif
