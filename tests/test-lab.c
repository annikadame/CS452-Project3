#include <assert.h>
#include <stdlib.h>
#include <time.h>
#ifdef __APPLE__
#include <sys/errno.h>
#else
#include <errno.h>
#endif
#include "harness/unity.h"
#include "../src/lab.h"


void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}



/**
 * Check the pool to ensure it is full.
 */
void check_buddy_pool_full(struct buddy_pool *pool)
{
  //A full pool should have all values 0-(kval-1) as empty
  for (size_t i = 0; i < pool->kval_m; i++)
    {
      assert(pool->avail[i].next == &pool->avail[i]);
      assert(pool->avail[i].prev == &pool->avail[i]);
      assert(pool->avail[i].tag == BLOCK_UNUSED);
      assert(pool->avail[i].kval == i);
    }

  //The avail array at kval should have the base block
  assert(pool->avail[pool->kval_m].next->tag == BLOCK_AVAIL);
  assert(pool->avail[pool->kval_m].next->next == &pool->avail[pool->kval_m]);
  assert(pool->avail[pool->kval_m].prev->prev == &pool->avail[pool->kval_m]);

  //Check to make sure the base address points to the starting pool
  //If this fails either buddy_init is wrong or we have corrupted the
  //buddy_pool struct.
  assert(pool->avail[pool->kval_m].next == pool->base);
}

/**
 * Check the pool to ensure it is empty.
 */
void check_buddy_pool_empty(struct buddy_pool *pool)
{
  //An empty pool should have all values 0-(kval) as empty
  for (size_t i = 0; i <= pool->kval_m; i++)
    {
      assert(pool->avail[i].next == &pool->avail[i]);
      assert(pool->avail[i].prev == &pool->avail[i]);
      assert(pool->avail[i].tag == BLOCK_UNUSED);
      assert(pool->avail[i].kval == i);
    }
}

/**
 * Test allocating 1 byte to make sure we split the blocks all the way down
 * to MIN_K size. Then free the block and ensure we end up with a full
 * memory pool again
 */
void test_buddy_malloc_one_byte(void)
{
  fprintf(stderr, "->Test allocating and freeing 1 byte\n");
  struct buddy_pool pool;
  int kval = MIN_K;
  size_t size = UINT64_C(1) << kval;
  buddy_init(&pool, size);
  void *mem = buddy_malloc(&pool, 1);
  //Make sure correct kval was allocated
  buddy_free(&pool, mem);
  check_buddy_pool_full(&pool);
  buddy_destroy(&pool);
}

/**
 * Tests the allocation of one massive block that should consume the entire memory
 * pool and makes sure that after the pool is empty we correctly fail subsequent calls.
 */
void test_buddy_malloc_one_large(void)
{
  fprintf(stderr, "->Testing size that will consume entire memory pool\n");
  struct buddy_pool pool;
  size_t bytes = UINT64_C(1) << MIN_K;
  buddy_init(&pool, bytes);

  //Ask for an exact K value to be allocated. This test makes assumptions on
  //the internal details of buddy_init.
  size_t ask = bytes - sizeof(struct avail);
  void *mem = buddy_malloc(&pool, ask);
  assert(mem != NULL);

  //Move the pointer back and make sure we got what we expected
  struct avail *tmp = (struct avail *)mem - 1;
  assert(tmp->kval == MIN_K);
  assert(tmp->tag == BLOCK_RESERVED);
  check_buddy_pool_empty(&pool);

  //Verify that a call on an empty tool fails as expected and errno is set to ENOMEM.
  void *fail = buddy_malloc(&pool, 5);
  assert(fail == NULL);
  assert(errno = ENOMEM);

  //Free the memory and then check to make sure everything is OK
  buddy_free(&pool, mem);
  check_buddy_pool_full(&pool);
  buddy_destroy(&pool);
}

/**
 * Tests to make sure that the struct buddy_pool is correct and all fields
 * have been properly set kval_m, avail[kval_m], and base pointer after a
 * call to init
 */
void test_buddy_init(void)
{
  fprintf(stderr, "->Testing buddy init\n");
  //Loop through all kval MIN_k-DEFAULT_K and make sure we get the correct amount allocated.
  //We will check all the pointer offsets to ensure the pool is all configured correctly
  for (size_t i = MIN_K; i <= DEFAULT_K; i++)
    {
      size_t size = UINT64_C(1) << i;
      struct buddy_pool pool;
      buddy_init(&pool, size);
      check_buddy_pool_full(&pool);
      buddy_destroy(&pool);
    }
}


/**
 * Repeatedly allocates and frees small blocks.
 * After all frees, the pool should be completely restored.
 */


void test_small_allocs_and_frees(void)
{
    struct buddy_pool pool;
    size_t size = UINT64_C(1) << MIN_K;
    buddy_init(&pool, size);


    void *ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = buddy_malloc(&pool, 1);
        assert(ptrs[i] != NULL);
    }


    for (int i = 0; i < 10; i++) {
        buddy_free(&pool, ptrs[i]);
    }


    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}


/**
 * Allocates two adjacent buddy blocks, frees them,
 * and verifies that they coalesce into a larger block.
 */
void test_coalescing_buddies(void)
{
    struct buddy_pool pool;
    buddy_init(&pool, UINT64_C(1) << MIN_K);


    void *a = buddy_malloc(&pool, (UINT64_C(1) << (MIN_K - 1)) - sizeof(struct avail));
    void *b = buddy_malloc(&pool, (UINT64_C(1) << (MIN_K - 1)) - sizeof(struct avail));


    assert(a != NULL && b != NULL);


    buddy_free(&pool, a);
    buddy_free(&pool, b);


    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}


/**
 * Same as above, but frees in reverse order.
 * Verifies that coalescing works regardless of free order.
 */
void test_reverse_order(void)
{
    struct buddy_pool pool;
    buddy_init(&pool, UINT64_C(1) << MIN_K);


    void *a = buddy_malloc(&pool, (UINT64_C(1) << (MIN_K - 1)) - sizeof(struct avail));
    void *b = buddy_malloc(&pool, (UINT64_C(1) << (MIN_K - 1)) - sizeof(struct avail));


    buddy_free(&pool, b);
    buddy_free(&pool, a);


    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}




/**
 * Allocate the smallest possible block allowed by the allocator.
 * Confirms that allocator takes the minimum size (SMALLEST_K).
 */
void test_minimum_k_block_allocation(void)
{
    struct buddy_pool pool;
    buddy_init(&pool, UINT64_C(1) << MIN_K);


    void *ptr = buddy_malloc(&pool, 1);
    struct avail *block = (struct avail *)ptr - 1;
    assert(block->kval == SMALLEST_K);


    buddy_free(&pool, ptr);
    check_buddy_pool_full(&pool);
    buddy_destroy(&pool);
}


int main(void) {
  time_t t;
  unsigned seed = (unsigned)time(&t);
  fprintf(stderr, "Random seed:%d\n", seed);
  srand(seed);
  printf("Running memory tests.\n");

  UNITY_BEGIN();
  RUN_TEST(test_buddy_init);
  RUN_TEST(test_buddy_malloc_one_byte);
  RUN_TEST(test_buddy_malloc_one_large);
  RUN_TEST(test_small_allocs_and_frees);
  RUN_TEST(test_coalescing_buddies);
  RUN_TEST(test_reverse_order);
  RUN_TEST(test_minimum_k_block_allocation);




 
return UNITY_END();
}
