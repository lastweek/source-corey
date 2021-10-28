extern "C" {
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/jnic.h>
#include <test.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <lwipinc/lwipinit.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <machine/x86.h>
#include <string.h>
#include <stdio.h>
}

#include <inc/error.hh>
#include <inc/errno.hh>

enum { max_dir_count = 15 };
enum { max_file_count = 15 };
enum { file_size = 128 * 1024 };

static int fds[max_file_count];
static struct fs_handle fhs[max_file_count];

static void
test_mkdir(void)
{
    uint64_t time_fd = 0, time_rawfs = 0, start;
    echeck(mkdir("/x/test_mkdir", 0));
    struct fs_handle dir, workdir, subdir;
    echeck(fs_namei("/x", &dir));
    echeck(fs_mkdir(&dir, "test_fs_mkdir", &workdir));
    for (uint64_t i = 0; i < max_dir_count ; i++) {
	char buf[32];
	snprintf(buf, sizeof(buf), "dir%ld", i);
	start = read_tsc();
	echeck(fs_mkdir(&workdir, buf, &subdir));
	time_rawfs += read_tsc() - start;

	snprintf(buf, sizeof(buf), "/x/test_mkdir/dir%ld", i);
	start = read_tsc();
	echeck(mkdir(buf, 0));
	time_fd += read_tsc() - start;
    }
    cprintf("cycles per operation: mkdir vs fs_mkdir =  %lu vs %lu\n", 
	    time_fd / max_dir_count, time_rawfs / max_dir_count );
}

static void
test_creat(void)
{
    uint64_t time_fd = 0, time_rawfs = 0, start;
    echeck(mkdir("/x/test_creat", 0));
    struct fs_handle dir, workdir;
    echeck(fs_namei("/x", &dir));
    echeck(fs_mkdir(&dir, "test_fs_creat", &workdir));
    for (uint64_t i = 0; i < max_file_count ; i++) {
	char buf[32];
	snprintf(buf, sizeof(buf), "/x/test_creat/foo.%ld", i);
	start = read_tsc();
	echeck(fds[i] = creat(buf, 0));
	time_fd += read_tsc() - start;

	snprintf(buf, sizeof(buf), "foo.%ld", i);
	start = read_tsc();
	echeck(fs_create(&workdir, buf, &fhs[i]));
	time_rawfs += read_tsc() - start;
    }
    cprintf("cycles per operation: creat vs fs_create =  %lu vs %lu\n", 
	    time_fd / max_dir_count, time_rawfs / max_dir_count );
}

static void
test_open(void)
{
    uint64_t time_fd = 0, time_rawfs = 0, start;
    struct fs_handle workdir;
    echeck(fs_namei("/x/test_fs_creat", &workdir));
    for (uint64_t i = 0; i < max_file_count ; i++) {
	char buf[32];
	snprintf(buf, sizeof(buf), "/x/test_creat/foo.%ld", i);
	// close the original fd in case of the fd got exhausted
	close(fds[i]);
	start = read_tsc();
	echeck(fds[i] = open(buf, O_RDWR));
	time_fd += read_tsc() - start;

	snprintf(buf, sizeof(buf), "foo.%ld", i);
	start = read_tsc();
	echeck(fs_dlookup(&workdir, buf, &fhs[i]));
	time_rawfs += read_tsc() - start;
    }
    cprintf("cycles per operation: open vs fs_dlookup =  %lu vs %lu\n", 
	    time_fd / max_dir_count, time_rawfs / max_dir_count );
}

static void
test_write(void)
{
    uint64_t time_fd = 0, time_rawfs = 0, start;
    char page[PGSIZE];
    memset(page, 'A', sizeof(page));
    for (uint64_t i = 0; i < max_file_count ; i++) {
	start = read_tsc();
	// write file
	for (int j = 0; j < file_size / PGSIZE; j++) {
	    echeck(write(fds[i], page, PGSIZE));
	}
	time_fd += read_tsc() - start;

	start = read_tsc();
	// write file
	for (int j = 0; j < file_size / PGSIZE; j++) {
	    echeck(fs_pwrite(&fhs[i], page, PGSIZE, j * PGSIZE));
	}
	time_rawfs += read_tsc() - start;
    }
    cprintf("cycles per operation: write vs fs_pwrite(one page) =  %lu vs %lu\n", 
	    time_fd / max_dir_count, time_rawfs / max_dir_count );
}

static void
test_read(void)
{
    uint64_t time_fd = 0, time_rawfs = 0, start;
    char page[PGSIZE];
    for (uint64_t i = 0; i < max_file_count ; i++) {
	// set offset to 0 so that we can read through the whole file
	echeck(lseek(fds[i], 0, SEEK_SET));
	start = read_tsc();
	for (int j = 0; j < file_size / PGSIZE; j++) {
	    echeck(read(fds[i], page, PGSIZE));
	}
	time_fd += read_tsc() - start;

	start = read_tsc();
	for (int j = 0; j < file_size / PGSIZE; j++) {
	    echeck(fs_pread(&fhs[i], page, PGSIZE, j * PGSIZE));
	}
	time_rawfs += read_tsc() - start;
    }
    cprintf("cycles per operation: read vs fs_pread(one page) =  %lu vs %lu. ignore this %c\n", 
	    time_fd / max_dir_count, time_rawfs / max_dir_count, page[0] );
}

void
fs_fd_perf_test(void)
{
    test_mkdir();
    test_creat();
    test_open();
    test_write();
    test_read();
}

