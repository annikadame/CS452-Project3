#include <stdio.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <time.h>
#ifdef __APPLE__
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include "lab.h"
#define handle_error_and_die(msg) \
do \
{ \
perror(msg); \
raise(SIGKILL); \
} while (0)
/**
* @brief Convert bytes to the correct K value
*
* @param bytes the number of bytes
* @return size_t the K value that will fit bytes
*/
size_t btok(size_t bytes)
{
    size_t k = SMALLEST_K;
    while ((UINT64_C(1) << k) < bytes && k < MAX_K)
    {
        k++;
    }
    return k;
}
struct avail *buddy_calc(struct buddy_pool *pool, struct avail *buddy)
{
    uintptr_t offset = (uintptr_t)buddy - (uintptr_t)pool->base;
    uintptr_t buddy_offset = offset ^ (UINT64_C(1) << buddy->kval);
    return (struct avail *)((uintptr_t)pool->base + buddy_offset);
}
void *buddy_malloc(struct buddy_pool *pool, size_t size)
{
    if (pool == NULL || size == 0)
        return NULL;
    size += sizeof(struct avail);
    size_t k = btok(size);
    size_t i = k;
    // R1: Find a block
    while (i <= pool->kval_m && pool->avail[i].next == &pool->avail[i])
    {
        i++;
    }
    // No block found
    if (i > pool->kval_m)
    {
        errno = ENOMEM;
        return NULL;
    }
    // R2: Remove block from list
    struct avail *block = pool->avail[i].next;
    block->prev->next = block->next;
    block->next->prev = block->prev;
    // R3/R4: Split if required until we get to requested size
    while (i > k)
    {
        i--;
        uintptr_t addr = (uintptr_t)block;
        uintptr_t buddy_addr = addr + (UINT64_C(1) << i);
        struct avail *buddy = (struct avail *)buddy_addr;
        buddy->kval = i;
        buddy->tag = BLOCK_AVAIL;
        // Insert buddy into list
        buddy->next = pool->avail[i].next;
        buddy->prev = &pool->avail[i];
        pool->avail[i].next->prev = buddy;
        pool->avail[i].next = buddy;
        block->kval = i;
    }
    block->tag = BLOCK_RESERVED;
    return (void *)(block + 1);
}
void buddy_free(struct buddy_pool *pool, void *ptr)
{
    if (pool == NULL || ptr == NULL)
        return;
    struct avail *block = (struct avail *)ptr - 1;
    size_t k = block->kval;
    block->tag = BLOCK_AVAIL;
    while (k < pool->kval_m)
    {
        struct avail *buddy = buddy_calc(pool, block);
        // Check if buddy is available and same size
        if (buddy->tag != BLOCK_AVAIL || buddy->kval != k)
            break;
        // Remove buddy from its free list
        buddy->prev->next = buddy->next;
        buddy->next->prev = buddy->prev;
        // Merge buddy and block
        if ((uintptr_t)buddy < (uintptr_t)block)
        {
            block = buddy;
        }
        k++;
        block->kval = k;
    }
    // Insert block into appropriate free list
    block->tag = BLOCK_AVAIL;
    block->next = pool->avail[k].next;
    block->prev = &pool->avail[k];
    pool->avail[k].next->prev = block;
    pool->avail[k].next = block;
}


void buddy_init(struct buddy_pool *pool, size_t size)
{
size_t kval = 0;
if (size == 0)
kval = DEFAULT_K;
else
kval = btok(size);
if (kval < MIN_K)
kval = MIN_K;
if (kval > MAX_K)
kval = MAX_K - 1;
//make sure pool struct is cleared out
memset(pool,0,sizeof(struct buddy_pool));
pool->kval_m = kval;
pool->numbytes = (UINT64_C(1) << pool->kval_m);
//Memory map a block of raw memory to manage
pool->base = mmap(
NULL, /*addr to map to*/
pool->numbytes, /*length*/
PROT_READ | PROT_WRITE, /*prot*/
MAP_PRIVATE | MAP_ANONYMOUS, /*flags*/
-1, /*fd -1 when using MAP_ANONYMOUS*/
0 /* offset 0 when using MAP_ANONYMOUS*/
);
if (MAP_FAILED == pool->base)
{
handle_error_and_die("buddy_init avail array mmap failed");
}
//Set all blocks to empty. We are using circular lists so the first elements
//just point
//to an available block. Thus the tag, and kval feild are unused burning a
//small bit of
//memory but making the code more readable. We mark these blocks as UNUSED to
//aid in debugging.
for (size_t i = 0; i <= kval; i++)
{
pool->avail[i].next = pool->avail[i].prev = &pool->avail[i];
pool->avail[i].kval = i;
pool->avail[i].tag = BLOCK_UNUSED;
}
//Add in the first block
pool->avail[kval].next = pool->avail[kval].prev = (struct avail *)pool->base;
struct avail *m = pool->avail[kval].next;
m->tag = BLOCK_AVAIL;
m->kval = kval;
m->next = m->prev = &pool->avail[kval];
}
void buddy_destroy(struct buddy_pool *pool)
{
int rval = munmap(pool->base, pool->numbytes);
if (-1 == rval)
{
handle_error_and_die("buddy_destroy avail array");
}
//Zero out the array so it can be reused it needed
memset(pool,0,sizeof(struct buddy_pool));
}
#define UNUSED(x) (void)x
/**
* This function can be useful to visualize the bits in a block. This can
* help when figuring out the buddy_calc function!
*/
__attribute__((unused))
static void printb(unsigned long int b)
{
size_t bits = sizeof(b) * 8;
unsigned long int curr = UINT64_C(1) << (bits - 1);
for (size_t i = 0; i < bits; i++)
{
if (b & curr)
{
printf("1");
}
else
{
printf("0");
}
curr >>= 1L;
}
}
