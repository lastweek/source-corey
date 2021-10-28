#include <machine/mmu.h>
#include <inc/lib.h>
#include <inc/syscall.h>
#include <inc/error.h>
#include <inc/fs.h>
#include <fs/dbfs.h>
#include <fs/dev.h>
#include <string.h>

#include <math.h>
#include <inc/device.h>
#include <inc/stdio.h>
#include <inc/assert.h>

/**
 * - The superblock is just a free block list (a bitmap) and is one block.
 * - Flat name space (a root directory only)
 * - Inodes are a block each.
 * - Inode numbers are the block numbers where the inodes reside.
 * - Block allocation for inodes and data blocks are the same: first-avail.
 *   from the free list.
 * - Ultra inefficient!
 */

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))

#define DBFS_NDENT 16
enum { SUPERBLOCK_ADDR = 0 };
enum { SUPERBLOCK_SIZE = 4096 };
enum { BLOCKSIZE = 4096 };
enum { INODESIZE = 4096 };
/** The inode of the root directory. */
enum { ROOTINODE = 1 };

#define ALLOC_BLOCK(type, name)                                \
    char name##_buf[BLOCKSIZE];                                \
    type *name = (type*) name##_buf;

#define ALLOC_INODE(ino)                                       \
    char ino##_buf[INODESIZE];                                 \
    dbfs_inode_t *ino = (dbfs_inode_t *) ino##_buf

#define READ_INODE_INTO(ino, num)                              \
    echeck(dbfs_read_inode(num, ino))

#define READ_INODE(ino, num)                                   \
    ALLOC_INODE(ino);                                          \
    READ_INODE_INTO(ino, num);

#define READ_HANDLE_INODE_INTO(ino, handle)                    \
    READ_INODE_INTO(ino, handle->fh_dbfs.inode)

#define READ_HANDLE_INODE(handle)                              \
    READ_INODE(ino, handle->fh_dbfs.inode)

#define FOR_DBLOCK(i,ino)                                      \
    for (uint64_t i = 0; i < nblocks(ino->len); i++)

#define FOR_DENT(i,j,ino)                                      \
    for (uint64_t j = 0;                                       \
         j < BLOCKSIZE / sizeof(dbfs_dent_t) &&                \
         BLOCKSIZE * i + sizeof(dbfs_dent_t) * j < ino->len;   \
         j++)

typedef struct dbfs_dent {
    char name[FS_NAME_LEN];
    uint64_t inode;
} dbfs_dent_t;

typedef struct dbfs_dir {
    struct dbfs_dent ent[0];
} dbfs_dir_t;

struct dbfs_file {
    char type;
    thread_mutex_t ram_mu;
    char contents[0];
};

enum { dir_type, file_type };

enum { DBLOCKS = 256 };
/* enum { INDIRECT_BLOCKS = 256 }; */

typedef struct dbfs_inode {
    char type;
    uint64_t len;
    uint64_t dblocks[DBLOCKS];
    /* struct indirect_block[INDIRECT_BLOCKS]; */
} dbfs_inode_t;

static const uint64_t nbufs_ = 16;

static struct sobj_ref disk_dev_;

static struct sobj_ref buf_seg_;
struct diskbuf_hdr *buf_base_;

static uint64_t bytes_;

#if 1

static int dlevel = 0;

#define dprintf(args...) { \
    for (int di = 0; di < dlevel; di++) \
        cprintf("  "); \
    cprintf(args); \
}

#define tracemsg(fmt, msg...) { \
    dprintf("%s: " fmt "\n", __func__, ## msg); \
    ++dlevel; \
}

#define trace() tracemsg("")

#define dreturn(x) { --dlevel; return x; }

#else

#define dprintf(args...)
#define tracemsg(fmt, msg...)
#define trace()
#define dreturn(x) return x

#endif

/**
 * Calculate the number of blocks in a segment of length \a i.
 * Mathematically, this is \code ceil(i / BLOCKSIZE) \endcode.
 * Equivalent to \code (i - 1) / BLOCKSIZE + 1 \endcode, but without the
 * arithmetic underflow.
 *
 * E.g., with a BLOCKSIZE of 10:
 *
 * \code
 * nblocks( 0) == 0 &&
 * nblocks( 1) == 1 && nblocks(10) == 1 &&
 * nblocks(11) == 2 && ...
 * \endcode
 */
inline static uint64_t
nblocks(uint64_t i)
{
    return (i + BLOCKSIZE - 1) / BLOCKSIZE;
}

static int64_t
dbfs_disk_read(uint64_t offset, uint64_t nbytes, void *buf)
{
    tracemsg("off %ld len %ld", offset, nbytes);
    if (offset % 512)
	dreturn(-E_INVAL);	// throw error(-E_INVAL, "offset not 512-byte aligned");
    if (nbytes % 512)
	dreturn(-E_INVAL);	// throw error(-E_INVAL, "nbytes not 512-byte aligned");

    uint64_t max =
	ROUNDDOWN((nbufs_ * PGSIZE) - sizeof(struct diskbuf_hdr), 512);

    uint64_t cc = 0;
    while (nbytes) {
	uint64_t bytes = JMIN(max, nbytes);
	buf_base_->address = offset + cc;
	buf_base_->count = bytes;

	echeck(sys_device_buf(disk_dev_, buf_seg_, 0, devio_in));
	while (!(buf_base_->count & DISKHDR_COUNT_DONE)) ;
	if (buf_base_->count & DISKHDR_COUNT_ERR)
	    dreturn(-E_IO);	// throw error(-E_IO, "disk error");

	void *va = (buf_base_ + 1);
	memcpy(&((char *) buf)[cc], va, bytes);

	nbytes -= bytes;
	cc += bytes;
    }

    dreturn(cc);
}

static int64_t
dbfs_disk_write(uint64_t offset, uint64_t nbytes, const void *buf)
{
    tracemsg("off %ld len %ld", offset, nbytes);
    if (offset % 512)
	dreturn(-E_INVAL);	// throw error(-E_INVAL, "offset not 512-byte aligned");
    if (nbytes % 512)
	dreturn(-E_INVAL);	// throw error(-E_INVAL, "nbytes not 512-byte aligned");

    uint64_t max =
	ROUNDDOWN((nbufs_ * PGSIZE) - sizeof(struct diskbuf_hdr), 512);

    uint64_t cc = 0;
    while (nbytes) {
	uint64_t bytes = JMIN(max, nbytes);
	void *va = (buf_base_ + 1);
	memcpy(va, &((char *) buf)[cc], bytes);

	buf_base_->address = offset + cc;
	buf_base_->count = bytes;

	nbytes -= bytes;
	cc += bytes;

	echeck(sys_device_buf(disk_dev_, buf_seg_, 0, devio_out));
	while (!(buf_base_->count & DISKHDR_COUNT_DONE)) ;
	if (buf_base_->count & DISKHDR_COUNT_ERR)
	    dreturn(-E_IO);	// error(-E_IO, "disk error");
    }

    dreturn(cc);
}

static int
dbfs_read_inode(uint64_t inode, dbfs_inode_t * ino)
{
    tracemsg("inode %ld", inode);
    echeck(dbfs_disk_read(BLOCKSIZE * inode, BLOCKSIZE, ino));
    dreturn(0);
}

#define echeck2(expr)					\
    ({							\
	int64_t __c = (expr);				\
	if (__c < 0)  				        \
            panic("%s: %s", #expr, e2s(__c));           \
	__c;						\
    })

static int
dbfs_read_block(uint64_t block, void *buf, int off, int count)
{
    trace();
    assert(off <= BLOCKSIZE && count <= BLOCKSIZE);
    dprintf("reading block\n");
    if (off % 512 == 0 && count % 512 == 0) {
	dreturn(dbfs_disk_read(BLOCKSIZE * block + off, count, buf));
    } else {
	char tmp[BLOCKSIZE];
	echeck(dbfs_disk_read(BLOCKSIZE * block + off, BLOCKSIZE, tmp));
	memcpy(buf, tmp, count);
	dreturn(0);
    }
}

static int
dbfs_write_block(uint64_t block, const void *buf, int off, int count)
{
    tracemsg("block %ld off %d len %d", block, off, count);
    assert(off <= BLOCKSIZE && count <= BLOCKSIZE);
    if (off % 512 == 0 && count % 512 == 0) {
	echeck(dbfs_disk_write(BLOCKSIZE * block + off, count, buf));
	dreturn(0);
    } else {
	char tmp[BLOCKSIZE];
	echeck2(dbfs_read_block(block, tmp, 0, BLOCKSIZE));
	memcpy(tmp + off, buf, count);
	echeck(dbfs_disk_write(BLOCKSIZE * block, BLOCKSIZE, tmp));
	dreturn(0);
    }
}

/** Take a free block from the free block list, and update the list. */
static int64_t
alloc_block()
{
    trace();
    uint8_t superblock[SUPERBLOCK_SIZE];
    echeck(dbfs_disk_read(SUPERBLOCK_ADDR, sizeof superblock, superblock));
    for (int i = 0; i < SUPERBLOCK_SIZE; i++) {
	uint8_t c = superblock[i];
	dprintf("scanning byte %02x\n", c);
	for (int j = 0; j < 8; j++) {
	    if (((c >> j) & 1) == 0) {
		superblock[i] |= (1 << j);
		echeck(dbfs_disk_write
		       (SUPERBLOCK_ADDR, sizeof superblock, superblock));
		dprintf("allocated block %d\n", 8 * i + j);
		dreturn(8 * i + j);
	    }
	}
    }
    dreturn(-E_NO_SPACE);
}

static int
free_block(uint64_t block)
{
    char superblock[SUPERBLOCK_SIZE];
    echeck(dbfs_disk_read(SUPERBLOCK_ADDR, sizeof superblock, superblock));
    superblock[block / 8] &= 0xff ^ (1 << (block % 8));
    echeck(dbfs_disk_write(SUPERBLOCK_ADDR, sizeof superblock, superblock));
    return 0;
}

static void
show_inode(dbfs_inode_t * ino)
{
    cprintf("{ .len = %ld, .dblocks = [ ", ino->len);
    FOR_DBLOCK(i, ino) {
	cprintf("%ld ", ino->dblocks[i]);
    }
    cprintf("] }\n");
}

static void
show_dir(dbfs_inode_t * ino)
{
    cprintf("directory\n");
    cprintf("---------\n");
    show_inode(ino);
    ALLOC_BLOCK(dbfs_dir_t, dir);
    FOR_DBLOCK(i, ino) {
	echeck(dbfs_read_block(ino->dblocks[i], dir, 0, BLOCKSIZE));
	FOR_DENT(i, j, ino) {
	    cprintf("  %s: %ld\n", dir->ent[j].name, dir->ent[j].inode);
	}
    }
}

static void
show_file(dbfs_inode_t * ino)
{
    cprintf("file     \n");
    cprintf("---------\n");
    show_inode(ino);
    FOR_DBLOCK(i, ino) {
	char b[4096];
	echeck(dbfs_read_block(ino->dblocks[i], b, 0, BLOCKSIZE));
	for (uint64_t j = 0; j < 3; j++) {
	    cprintf("%02x ", b[j]);
	}
	cprintf("... ");
	for (uint64_t j = 0; j < 3; j++) {
	    cprintf("%02x ", b[j]);
	}
	cprintf("\n");
    }
}

int
dbfs_dump(void)
{
    trace();
    uint8_t superblock[SUPERBLOCK_SIZE];
    echeck(dbfs_disk_read(SUPERBLOCK_ADDR, sizeof superblock, superblock));

    cprintf("superblock\n");
    cprintf("----------\n");
    for (int i = 0; i < 10; i++) {
	cprintf("%02x ", superblock[i]);
    }
    cprintf("\n");

    READ_INODE(root, ROOTINODE);
    show_dir(root);
    FOR_DBLOCK(i, root) {
	ALLOC_BLOCK(dbfs_dir_t, dir);
	echeck(dbfs_read_block(root->dblocks[i], dir, 0, BLOCKSIZE));
	FOR_DENT(i, j, root) {
	    dprintf("iterating i %ld j %ld\n", i, j);
	    READ_INODE(ino, dir->ent[j].inode);
	    show_file(ino);
	}
    }

    dreturn(0);
}

/** \param[in] reset Wipe out the disk? */
int
dbfs_init(struct fs_handle *h, int reset)
{
    trace();

    // Device setup.

    uint64_t disk_num = 0;
    uint64_t sh = core_env->sh;
    proc_id_t disk_pid = core_env->pid;

    struct u_device_list udl;
    echeck(sys_device_list(&udl));

    uint64_t did = UINT64(~0);
    uint64_t disk_count = 0;
    for (uint64_t i = 0; i < udl.ndev; i++) {
	if (udl.dev[i].type == device_disk) {
	    if (disk_num == disk_count) {
		did = udl.dev[i].id;
		break;
	    }
	    disk_count++;
	}
    }

    if (did == UINT64(~0)) {
	panic("unable to find disk %lu", disk_num);
    }

    int64_t dv;
    echeck(dv = sys_device_alloc(sh, did, disk_pid));
    disk_dev_ = SOBJ(core_env->sh, dv);

    struct u_device_conf udc;
    udc.irq.irq_pid = disk_pid;
    udc.irq.enable = 1;
    echeck(sys_device_conf(disk_dev_, &udc));

    struct u_device_stat uds;
    echeck(sys_device_stat(disk_dev_, &uds));
    bytes_ = uds.disk.bytes;

    // Initialize the buffers.

    echeck(segment_alloc(sh, nbufs_ * PGSIZE,
			 &buf_seg_, (void **) &buf_base_, 0,
			 "disk rw buf", disk_pid));
    memset(buf_base_, 0, nbufs_ * PGSIZE);

    if (reset) {
	// Initialize the superblock (free list).
	// TODO: bound the free list by the actual capacity of the disk.

	uint8_t superblock[SUPERBLOCK_SIZE];
	bzero(superblock, sizeof superblock);
	superblock[0] = 0x03;
	echeck(dbfs_disk_write
	       (SUPERBLOCK_ADDR, sizeof superblock, superblock));

	// Create the root dir.

	ALLOC_INODE(root_inode);
	root_inode->len = 0;
	echeck(dbfs_write_block(ROOTINODE, root_inode, 0, BLOCKSIZE));
    }

    h->fh_dev_id = 'd';
    h->fh_dbfs.inode = ROOTINODE;

    dreturn(0);
}

static void
foo(struct fs_handle *h)
{
    //READ_HANDLE_INODE(h);
    //show_dir(ino);
}

static int64_t
dbfs_truncate(struct fs_handle *h, uint64_t new_size)
{
    trace();
    READ_HANDLE_INODE(h);
    if (new_size < ino->len) {
	dprintf("%ld: ino->len %ld > new_size %ld (truncating)\n",
		h->fh_dbfs.inode, ino->len, new_size);
	dreturn(-E_INVAL);
	for (uint64_t i = nblocks(new_size); i <= ino->len / BLOCKSIZE; i++) {
	    echeck(free_block(ino->dblocks[i]));
	}
    } else if (new_size > ino->len) {
	dprintf("%ld: ino->len %ld < new_size %ld (growing)\n",
		h->fh_dbfs.inode, ino->len, new_size);
	dprintf("nblocks(ino->len) = %ld, nblocks(new_size) = %ld\n",
		nblocks(ino->len), nblocks(new_size));
	for (uint64_t i = nblocks(ino->len); i < nblocks(new_size); i++) {
	    ino->dblocks[i] = echeck(alloc_block());
	    dprintf("set %ld to %ld\n", i, ino->dblocks[i]);
	}
    }
    ino->len = new_size;
    dbfs_write_block(h->fh_dbfs.inode, ino, 0, BLOCKSIZE);
    foo(h);
    dreturn(0);
}

static int64_t
dbfs_file_truncate(struct fs_handle *h, uint64_t new_size)
{
    return dbfs_truncate(h, new_size);
}

#define DO_BLOCKS(op,typ) \
    typ blkbuf = buf; \
    uint64_t startblock = off / BLOCKSIZE; \
    uint64_t endblock = (end + BLOCKSIZE - 1) / BLOCKSIZE; \
    dprintf("off %ld end %ld cnt %ld len %ld startblock %ld endblock %ld\n", \
            off, end, count, ino->len, startblock, endblock); \
    for (uint64_t i = startblock; i < endblock; i++) { \
        uint64_t blkoff = i == startblock ? off % BLOCKSIZE : 0; \
        uint64_t blklen = min(end - i * BLOCKSIZE, BLOCKSIZE) - blkoff; \
        dprintf("block %ld off %ld len %ld\n", ino->dblocks[i], blkoff, blklen); \
        echeck(op(ino->dblocks[i], blkbuf, blkoff, blklen)); \
        blkbuf += blklen; \
    }

static int64_t
dbfs_file_pread(struct fs_handle *h, void *buf, uint64_t count, uint64_t off)
{
    trace();
    READ_HANDLE_INODE(h);
    uint64_t end = min(ino->len, off + count);
    assert(count >= end - off);
    DO_BLOCKS(dbfs_read_block, void *);
    dreturn(end - off);
}

static int64_t
dbfs_file_pwrite(struct fs_handle *h,
		 const void *buf, uint64_t count, uint64_t off)
{
    trace();
    READ_HANDLE_INODE(h);
    uint64_t end = off + count;
    if (end > ino->len) {
	echeck(dbfs_truncate(h, end));
	READ_HANDLE_INODE_INTO(ino, h);
    }
    DO_BLOCKS(dbfs_write_block, const void *);
    dreturn(count);
}

static int
dbfs_file_create(struct fs_handle *dir, const char *fn, struct fs_handle *o)
{
    trace();

    // Update the containing directory with a new entry.
    READ_HANDLE_INODE(dir);
    struct dbfs_dent dent;
    strncpy(dent.name, fn, sizeof dent.name);
    dent.name[sizeof dent.name - 1] = '\0';
    dent.inode = alloc_block();
    echeck(dbfs_file_pwrite(dir, &dent, sizeof(struct dbfs_dent), ino->len));

    // Initialize the new inode.
    ALLOC_INODE(new_ino);
    new_ino->len = 0;
    echeck(dbfs_write_block(dent.inode, new_ino, 0, BLOCKSIZE));

    // Initialize the handle to dreturn to the user.
    o->fh_dev_id = 'd';
    o->fh_dbfs.inode = dent.inode;

    dreturn(0);
}

/** Scan the directory entries, reading in a block at a time. */
static int
dbfs_dir_lookup(struct fs_handle *h, const char *fn, struct fs_handle *o)
{
    READ_HANDLE_INODE(h);
    ALLOC_BLOCK(dbfs_dir_t, dir);
    FOR_DBLOCK(i, ino) {
	echeck(dbfs_read_block(ino->dblocks[i], dir, 0, BLOCKSIZE));
	FOR_DENT(i, j, ino) {
	    if (0 == strcmp(dir->ent[j].name, fn)) {
		o->fh_dev_id = 'd';
		o->fh_dbfs.inode = dir->ent[j].inode;
		return 0;
	    }
	}
    }

    return -E_NOT_FOUND;
}

/** Not implemented. */
static int
dbfs_dir_mk(struct fs_handle *h, const char *fn, struct fs_handle *o)
{
    o->fh_dev_id = 'd';
    return -E_INVAL;
}

/** TODO: implement */
static int
dbfs_pfork(struct fs_handle *root, struct sobj_ref *sh)
{
    // XXX need to fix interface, should pass ps object and at
    // *sh = SOBJ(root->fh_dbfs.seg.share, root->fh_dbfs.seg.share);
    return 0;
}

static int
dbfs_stat(struct fs_handle *h, struct fs_stat *stat)
{
    READ_HANDLE_INODE(h);
    stat->size = ino->len;
    stat->mode = 0;
    return 0;
}

static int
dbfs_fsync(struct fs_handle *h)
{
    return 0;
}

struct fs_dev dbfs_dev = {
    .file_pread = dbfs_file_pread,
    .file_pwrite = dbfs_file_pwrite,
    .file_create = dbfs_file_create,
    .file_truncate = dbfs_file_truncate,
    .dir_lookup = dbfs_dir_lookup,
    .dir_mk = dbfs_dir_mk,
    .fs_pfork = dbfs_pfork,
    .fs_stat = dbfs_stat,
    .fs_fsync = dbfs_fsync,
};
