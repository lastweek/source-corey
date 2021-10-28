#include <machine/param.h>
#include <machine/x86.h>
#include <inc/syscall.h>
#include <inc/fs.h>
#include <inc/intmacro.h>
#include <inc/lib.h>
#include <test.h>

#include <string.h>
#include <stdio.h>

enum { max_file_count = 15 };
enum { stress_file_size = 128 * 1024 };

static struct {
    volatile char start;
    volatile char done;
    
    union {
	struct {
	    volatile uint64_t count;
	};
	char pad[JOS_CLINE];
    } cpu[JOS_NCPU];
} *shared;

static uint64_t
namei_random(struct fs_handle *f)
{
    uint64_t x = jrand();
    x *= (core_env->pid + 1);
    uint64_t fnum = x % max_file_count;

    char fname[64];
    snprintf(fname, sizeof(fname), "/x/foo.%ld", fnum);
    echeck(fs_namei(fname, f));
    return x;
}

static void
fs_child (proc_id_t pid)
{
    char buf[128];
    struct fs_handle file;

    uint64_t s = 0;
    if (pid == 0) { 
	s = read_tsc();
	shared->start = 1;
    }
    else
	while (!shared->start);

    for (int i = 0; !shared->done; i++) {
	uint64_t k = namei_random(&file);
	k = k % max_file_count;
	
	echeck(fs_pread(&file, buf, sizeof(buf), k * PGSIZE));
	shared->cpu[pid].count++;
	assert(buf[0] == (char)k);

	if (pid == 0 && shared->cpu[pid].count == 10) {
	    shared->done = 1;
	    
	    uint64_t count = 0;
	    for (int j = 0; j < JOS_NCPU; j++)
		count += shared->cpu[j].count;

	    cprintf("%lu complete in %lu cycles\n", count, read_tsc() - s);
	    return;
	}
    }
    
    processor_halt();
}

void
dbfs_test(void)
{
    struct fs_handle dbfs_fh;
    echeck(dbfs_init(&dbfs_fh, 1));
    echeck(fs_mount(core_env->mtab, &core_env->rhand, "dbfs", &dbfs_fh));
    struct fs_handle dbfs_root;
    echeck(fs_namei("/dbfs", &dbfs_root));

    enum { ncpu = 4 };

    struct sobj_ref seg;
    echeck(segment_alloc(core_env->sh, sizeof(*shared), &seg, 
			 (void **)&shared, SEGMAP_SHARED, 
			 "shared-seg", core_env->pid));

    struct fs_handle dir = dbfs_root;
    //for (uint64_t i = 0; i < max_file_count; i++) {
    for (uint64_t i = 0; i < 2; i++) {
	struct fs_handle file;
	char buf[32];
	snprintf(buf, sizeof(buf), "foo.%ld", i);
	echeck(fs_create(&dir, buf, &file));
        for (uint64_t iter = 0; iter < 2; iter++) {
            for (uint64_t k = 0; k < i + 1; k++) {
                char page[PGSIZE];
                memset(page, k, sizeof(page));
                cprintf("write page %ld\n", k);
                echeck(fs_pwrite(&file, page, sizeof(page), k * PGSIZE));
                cprintf("wrote page: %d %d %d ... %d %d\n",
                        page[0], page[1], page[2],
                        page[PGSIZE - 2], page[PGSIZE - 1]);
            }
        }
        for (uint64_t k = 0; k < i + 1; k++) {
            char page[PGSIZE];
            char c = (char) k;
            cprintf("reading page %ld\n", k);
            echeck(fs_pread(&file, page, sizeof page, k * PGSIZE));
            cprintf("read page: %d %d %d ... %d %d\n",
                    page[0], page[1], page[2],
                    page[PGSIZE - 2], page[PGSIZE - 1]);
            cprintf("%d\n", *page);
            for (char *p = page; p < page + sizeof page; p++) {
                if (*p != c) {
                    panic("expecting %d but got %d at %lx", c, *p, p - page);
                }
            }
        }
	cprintf("\n");
    }

    dbfs_dump();

    return;

    memset(shared, 0, sizeof(*shared));

    for (int i = 1; i < ncpu; i++) {
	int64_t r = pfork(i);
	assert(r >= 0);
	if (r == 0)
	    fs_child(i);
    }

    fs_child(0);

    cprintf("cleaning up...\n");
    as_unmap(shared);
    sys_share_unref(seg);
    cprintf("all done!\n");
}

static void __attribute__((unused))
fs_stress_read(uint32_t cpu)
{
    if (cpu == 0)
	shared->start = 1;
    else
	while (!shared->start)
	    nop_pause();
    
    for (uint64_t c = 0; c < 2; c++) {
	cprintf("fs_stress_test: checking iteration %lu\n", c);
	for (uint64_t i = 0; i < max_file_count; i++) {
	    struct fs_handle file;
	    namei_random(&file);
	    for (uint64_t k = 0; k < stress_file_size / PGSIZE; k++) {
		char page0[PGSIZE];
		memset(page0, 'A' + k, sizeof(page0));
		char page1[PGSIZE];
		echeck(fs_pread(&file, page1, sizeof(page1), k * PGSIZE));
		assert(memcmp(page0, page1, PGSIZE) == 0);
	    }
	}
    }
}

static void __attribute__((unused))
fs_stress_random(uint32_t cpu)
{
    if (cpu == 0)
	shared->start = 1;
    else
	while (!shared->start)
	    nop_pause();
    
    for (uint64_t c = 0; c < 1000; c++) {
	struct fs_handle file;
	uint64_t x = namei_random(&file);
	x = x % stress_file_size;
	x = x / PGSIZE;
	
	char page1[PGSIZE];
	echeck(fs_pread(&file, page1, sizeof(page1), x * PGSIZE));
    }
}


void __attribute__((unused))
fs_stress_test(void)
{
    enum { ncpu = 2 };

    struct sobj_ref seg;
    echeck(segment_alloc(core_env->sh, sizeof(*shared), &seg, 
			 (void **)&shared, SEGMAP_SHARED, 
			 "shared-seg", core_env->pid));
    memset(shared, 0, sizeof(*shared));

    struct fs_handle dir;
    echeck(fs_namei("/x", &dir));
    for (uint64_t i = 0; i < max_file_count; i++) {
	struct fs_handle file;
	char buf[32];
	snprintf(buf, sizeof(buf), "foo.%ld", i);
	echeck(fs_create(&dir, buf, &file));
	for (uint64_t k = 0; k < stress_file_size / PGSIZE; k++) {
	    char page[PGSIZE];
	    memset(page, 'A' + k, sizeof(page));
	    echeck(fs_pwrite(&file, page, sizeof(page), k * PGSIZE));
	}
    }

    cprintf("fs_stress_test: done filling file contents\n");

    //fs_stress_random(0);
    shared->start = 0;
    for (int i = 1; i < ncpu; i++) {
	int64_t r = pfork(i);
	assert(r >= 0);
	if (r == 0) {
	    jrand_init();
	    fs_stress_random(i);
	    //fs_stress_read(i);
	    processor_halt();
	}
    }
    //fs_stress_read(0);
    
    cprintf("cleaning up...\n");
    as_unmap(shared);
    sys_share_unref(seg);
    cprintf("all done!\n");
}

void
fs_test(void)
{
    enum { ncpu = 4 };

    struct sobj_ref seg;
    echeck(segment_alloc(core_env->sh, sizeof(*shared), &seg, 
			 (void **)&shared, SEGMAP_SHARED, 
			 "shared-seg", core_env->pid));

    struct fs_handle dir;
    echeck(fs_namei("/x", &dir));
    for (uint64_t i = 0; i < max_file_count; i++) {
	struct fs_handle file;
	char buf[32];
	snprintf(buf, sizeof(buf), "foo.%ld", i);
	echeck(fs_create(&dir, buf, &file));
	for (uint64_t k = 0; k < i + 1; k++) {
	    char page[PGSIZE];
	    memset(page, k, sizeof(page));
	    echeck(fs_pwrite(&file, page, sizeof(page), k * PGSIZE));
	}
    }

    memset(shared, 0, sizeof(*shared));

    for (int i = 1; i < ncpu; i++) {
	int64_t r = pfork(i);
	assert(r >= 0);
	if (r == 0)
	    fs_child(i);
    }

    fs_child(0);

    cprintf("cleaning up...\n");
    as_unmap(shared);
    sys_share_unref(seg);
    cprintf("all done!\n");
}
