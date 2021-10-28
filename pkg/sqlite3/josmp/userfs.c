
// there are no directories in userfs

/*
#include <inc/lib.h>
#include <inc/syscall.h>
#include <inc/error.h>
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string.h>
#include <inc/assert.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>

#include <inc/lib.h>

#include "userfs.h"

#define PENDING_BYTE      0x40000000
#define RESERVED_BYTE     (PENDING_BYTE+1)
#define SHARED_FIRST      (PENDING_BYTE+2)
#define SHARED_SIZE       510

#define USERFS_MAX_NAMELEN 32
#define USERFS_MAX_NUMFILES 128

static struct userfs_file * userfs_filesystem = NULL;
uint32_t userfs_initialized = 0;

#define USERFS_FILE_PTR(x) ((struct userfs_file *)((x)->fh_userfs))
#define USERFS_FILE_SIZE(x) ((x)->size)

// 0 if no operlap, 1 if some
int overlap(struct userfs_flock * lk1, struct userfs_flock * lk2) {
	if ((lk2->l_start == 0) && (lk2->l_len == 0)) {
		return (1);
	}
	if (lk1->l_start < lk2->l_start) {
		if (lk1->l_start + lk1->l_len <= lk2->l_start) {
			return (0);
		} else {
			return (1);
		}
	} else {
		if (lk2->l_start + lk2->l_len <= lk1->l_start) {
			return (0);
		} else {
			return (1);
		}
	}
}

// 0 if not a complete covering overlap, 1 if
int complete_overlap(struct userfs_flock * lk1, struct userfs_flock * lk2) {
	return (lk1->l_start == lk2->l_start && lk1->l_len == lk2->l_len);
}

struct internal_userfs_flock *
find_flock(struct userfs_file * fp, struct userfs_flock * lp) {
	struct internal_userfs_flock * lkp;
	lkp = TAILQ_FIRST(&fp->lock_head);
	while (lkp != NULL) {
		if (overlap((struct userfs_flock *)lkp, lp)) {
			break;
		}
		lkp = TAILQ_NEXT(lkp, lock_list);
	}
	return lkp;
}

int
userfs_fcntl(struct userfs_file * fp, int cmd, struct userfs_flock * lp) {
	struct internal_userfs_flock * lkp, * nlk;
	uint32_t found = 0;

	proc_id_t pid = processor_current_procid();

	assert(lp->l_whence == U_SEEK_SET);
	assert(fp != NULL);

	lp->l_pid = pid;
	
	thread_mutex_lock(&fp->flocklist_mutex);
	lkp = TAILQ_FIRST(&fp->lock_head);
	while (lkp != NULL) {
		if (cmd == UF_SETLK) {
			if (overlap((struct userfs_flock *)lkp, lp)) {
				found = 1;
				if (lp->l_type == UF_WRLCK_) {
					if ((lp->l_start == SHARED_FIRST) && (lp->l_len == SHARED_SIZE)) {
						rw_read_unlock(&lkp->frw_lock);
					}
					rw_write_lock(&lkp->frw_lock);
					lkp->l_type = lp->l_type;
					lkp->l_pid = lp->l_pid;
				} else if (lp->l_type == UF_RDLCK_) {
					if ((lkp->l_type == UF_WRLCK_) && (lkp->l_pid == lp->l_pid)) {
						rw_write_unlock(&lkp->frw_lock);
					}
					rw_read_lock(&lkp->frw_lock);
					lkp->l_type = lp->l_type;
					lkp->l_pid = lp->l_pid;
				} else if (lp->l_type == UF_UNLCK_) {
					if (lkp->l_type == UF_WRLCK_) {
						rw_write_unlock(&lkp->frw_lock);
						lkp->l_type = lp->l_type;
						lkp->l_pid = 0;// maybe wrong
					} else if (lkp->l_type == UF_RDLCK_) {
						rw_read_unlock(&lkp->frw_lock);
						if (rw_write_can_lock(&lkp->frw_lock)) {
							lkp->l_type = UF_UNLCK_;
						}
					}
				} else {
					panic("Impossible");
				}
			} 
		} else if (cmd == UF_GETLK) {
			if (overlap((struct userfs_flock *)lkp, lp)) {
				memcpy (lp, lkp, sizeof (struct userfs_flock));
				goto out;
			}
		} else {
			panic("Unknown fcntl command");
		}
		lkp = TAILQ_NEXT(lkp, lock_list);
	}

	if (!found) {
		if (cmd == UF_SETLK) {
			lkp = (struct internal_userfs_flock *) malloc (sizeof (struct internal_userfs_flock));
			memcpy (lkp, lp, sizeof (struct userfs_flock));
			rw_init(&lkp->frw_lock);	
			TAILQ_INSERT_HEAD(&fp->lock_head, lkp, lock_list);
			if (lp->l_type == UF_WRLCK_) {
				rw_write_lock(&lkp->frw_lock);
			} else if (lp->l_type == UF_RDLCK_) {
				rw_read_lock(&lkp->frw_lock);
			} else if (lp->l_type == UF_UNLCK_) {

			} else {
				panic("Impossible");
			}
			lkp->l_type = lp->l_type;
		}
	}

out:
	thread_mutex_unlock(&fp->flocklist_mutex);
	return 0;
}

void
userfs_print_file_info(struct userfs_file * fp) {
	int i;
	assert(fp->used == 1);
	fprintf(stderr, "%s (0x%lx) [0, %lu] (%lu) %s\n", 
		fp->name, (unsigned long)fp->buf, fp->size, fp->used,
			((TAILQ_FIRST(&fp->lock_head)) ? "locks" : "no locks"));
	if (fp->buf) {
		fprintf(stderr, "contents=");
		for (i = 0; i < 15; i ++) {
			fprintf(stderr, "%c", (char)(fp->buf[i]));
		}
		fprintf(stderr, "\n");
	} else {
		fprintf(stderr, "contents=(none)\n");
	}
	return;
}

int64_t
userfs_pread(struct userfs_file *fp, void *buf, uint64_t amt, uint64_t off) {
	uint64_t rd;

	if (fp == NULL)
		return ((int64_t)-EBADF);

	if (off >= fp->size)
		return (0);

 	if ((off + amt) > fp->size) {
		rd = fp->size - off;
	} else {
		rd = amt;
	}

	memcpy(buf, fp->buf+off, (size_t)rd);

	//userfs_print_file_info(fp);

	return ((int64_t)rd);
}

int64_t
userfs_pwrite(struct userfs_file *fp, const void *buf, 
	uint64_t amt, uint64_t off)
{
	uint64_t append;
	char * new_buf = NULL;
	uint64_t old_size = USERFS_FILE_SIZE(fp);

	if (fp == NULL)
		return (-EBADF);

	append = 0;
	if ((off + amt) > fp->size) {
		append = (off + amt) - fp->size;
	}

	if (append > 0) {
		new_buf = (char*) malloc (old_size + append);
		// copy the old data into the right place
		if (old_size > 0) {
			memcpy(new_buf, fp->buf, old_size);
		}
		memset(new_buf + fp->size, 0, append);
		if (fp->buf)
			free(fp->buf);
		fp->buf = new_buf;
	}

	// write the data
	fp->size = fp->size + append;

	memcpy (fp->buf+off, buf, amt);
	//userfs_print_file_info(fp);
	return (amt);
}

int64_t
userfs_ftruncate(struct userfs_file *fp, uint64_t new_size) {

	fprintf(stderr, "[%d] userfs_ftruncate\n", processor_current_procid());

	if (new_size != 0) {
		return (-1); // error
	}

	if (fp == NULL) {
		return (-EBADF);
	}

	if (fp->name) {
		free(fp->name);
		fp->name = NULL;
	}

	if (fp->buf) {
		free(fp->buf);
		fp->buf = NULL;
	}
	fp->size = 0;

	//userfs_print_file_info(fp);

	return (0);
}

int
userfs_fstat(struct userfs_file *fp, struct userfs_stat *pstat) {

	if (pstat == NULL)
		return (-EFAULT);

	if (fp == NULL)
		return (-EBADF);

	pstat->st_size = USERFS_FILE_SIZE(fp);
	pstat->st_dev = fp->dev;
	pstat->st_ino = fp->ino;

	return (0);
}

int
userfs_lookup(const char * name, struct userfs_file ** fpp) {
	int i;

	for (i = 0; i < USERFS_MAX_NUMFILES; i ++) {
		if ((userfs_filesystem[i].used == 1) 
				&& (strcmp(name, userfs_filesystem[i].name) == 0)) {
			break;
		}
	}

	if (i == USERFS_MAX_NUMFILES)
		return (-ENOENT);

	if (fpp != NULL)
		*fpp = &userfs_filesystem[i];

	return (0);
}

int
userfs_create(const char *name, struct userfs_file **fpp) {
	int slot, i;
	struct userfs_file * fp;

	if (strlen(name) > USERFS_MAX_NAMELEN-1) {
		// name too long
		return (-ENAMETOOLONG);
	}

	// find a slot
	slot = -1;
	for (i = 0; i < USERFS_MAX_NUMFILES; i ++) {
		if ((userfs_filesystem[i].used == 1)
				&& (strcmp(name, userfs_filesystem[i].name) == 0)) {
			// name already exists
			return (-EEXIST);
		}
		if (slot == -1 && !userfs_filesystem[i].used) {
			slot = i;
		}
	}

	if (slot == -1) {
		// no space in table
		return (-ENOSPC);
	}

	fp = &userfs_filesystem[slot];
	fp->used = 1;
	fp->name = malloc(strlen(name) + 1);
	thread_mutex_init(&fp->flocklist_mutex);
	TAILQ_INIT(&fp->lock_head);
	strcpy(fp->name, name);
	fp->size = 0;
	fp->dev = 0;
	fp->ino = slot;

	*fpp = fp;

	//userfs_print_file_info(fp);
	return (0);
}

int userfs_init(void) {
	if (!userfs_initialized) {
		userfs_filesystem = malloc (sizeof(struct userfs_file) * USERFS_MAX_NUMFILES);
		memset(userfs_filesystem, 0, 
				sizeof(struct userfs_file) * USERFS_MAX_NUMFILES);
		for (uint32_t i = 0; i < USERFS_MAX_NUMFILES; i ++) {
			TAILQ_INIT(&(userfs_filesystem[i].lock_head));
		}
		userfs_initialized = 1;
	}
	return(0);
}

