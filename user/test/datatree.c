#include <test.h>

#include <fs/datatree.h>
#include <malloc.h>

static void *
data_block_alloc(void *x)
{
    return malloc(DATA_BLK_BYTES);
}

static void
data_block_free(void *x, void *va)
{
    free(va);
}

void
datatree_test(void)
{
    cprintf("test4\n");
    struct datatree dt;
    datatree_init(&dt, data_block_alloc, data_block_free, 0);
    
    for (int i = 0; i < 1000000; i++) {
	void *va = data_block_alloc(0);
	memset(va, i, DATA_BLK_BYTES);
	assert(datatree_put(&dt, i, va) == 0);
    }

    uint64_t s = read_tsc();
    for (int i = 0; i < 1000000; i++) {
	char buf[DATA_BLK_BYTES];
	memset(buf, i, DATA_BLK_BYTES);
	void *va;
	assert(datatree_get(&dt, i, &va) == 0);
	if (memcmp(buf, va, DATA_BLK_BYTES) != 0)
	    panic("i %d\n", i);
	//assert(memcmp(buf, va, 4096) == 0);
    }

    datatree_free(&dt);

    cprintf("test4 done: %ld!\n", read_tsc() - s);
}
