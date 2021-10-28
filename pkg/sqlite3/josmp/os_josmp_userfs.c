
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#include "sqliteInt.h"
#include "os_common.h"

#if JOSMP_USER 

#define USERFS_MAX_NAMELEN 32
#define USERFS_FILE(x) ((struct userfs_file*)(((josFile*)(x))->fh))
#define USERFS_FILEPP(x) ((struct userfs_file**)(&(((josFile*)(x))->fh)))

/*
** The josFile structure is subclass of sqlite3_file specific for the jos
** protability layer.
*/
typedef struct josFile josFile;
struct josFile {
	sqlite3_io_methods const *pMethod;  /* Always the first entry */
	struct userfs_file * fh;     /* The file descriptor */
};

static int josRead (
	sqlite3_file *id, 
	void *pBuf, 
	int amt,
	sqlite3_int64 offset
){
	int got;

	TIMER_START;
	got = (int) userfs_pread(USERFS_FILE(id), pBuf, (uint64_t)amt, 
		(uint64_t)offset);
	TIMER_END;

	if (got == amt) {
		return SQLITE_OK;
	} else if (got < 0) {
		return SQLITE_IOERR_READ;
	} else {
		memset(&((char*)pBuf)[got], 0, amt-got);
		return SQLITE_IOERR_SHORT_READ;
	} 
}

static int josWrite(
	sqlite3_file *id, 
	const void *pBuf, 
	int amt,
	sqlite3_int64 offset 
){
	int wrote = 0;

	TIMER_START;
	wrote = (int) userfs_pwrite(USERFS_FILE(id), pBuf, (uint64_t)amt, 
		(uint64_t)offset);
	TIMER_END;

	if (wrote == amt){
		return SQLITE_OK;
	} else {
		assert(!"os_jos_userfs bad");
	}
}

// sync doesn't mean anything for userfs
static int full_fsync(int fd, int fullSync, int dataOnly){
	return SQLITE_OK;
}

static int josSync(sqlite3_file *id, int flags){
	return SQLITE_OK;
}

static int josTruncate(sqlite3_file *id, i64 nByte){
	int rc;

	rc = userfs_ftruncate(USERFS_FILE(id), (uint64_t)nByte);
	if (rc) {
		return SQLITE_OK;
	} else {
		assert(!"os_jos_userfs bad");
	}
}

static int josFileSize(sqlite3_file *id, i64 *pSize){
	struct fs_stat stat;

	if (!userfs_fstat(USERFS_FILE(id), &stat)) {
		*pSize = (i64)stat.size;
		return SQLITE_OK; 
	} else {
		assert(!"os_jos_userfs bad");
	}
}

static int josLock(sqlite3_file *id, int locktype){
	return (SQLITE_OK);
}

int josCheckReservedLock(sqlite3_file * id) {
	return 0;
}

static int josUnlock(sqlite3_file *id, int locktype){
	return (SQLITE_OK);
}

static int josClose(sqlite3_file *id) {
	return SQLITE_OK;
}

static int josFileControl(sqlite3_file *id, int op, void *pArg){
	return SQLITE_OK;
}

static int josSectorSize(sqlite3_file *id){
	return SQLITE_DEFAULT_SECTOR_SIZE;
}

static int josDeviceCharacteristics(sqlite3_file *id){
	return 0;
}

/*
** This vector defines all the methods that can operate on an sqlite3_file
** for jos.
*/
static const sqlite3_io_methods sqlite3JosIoMethod = {
  1,                        /* iVersion */
  josClose,
  josRead,
  josWrite,
  josTruncate,
  josSync,
  josFileSize,
  josLock,
  josUnlock,
  josCheckReservedLock,
  josFileControl,
  josSectorSize,
  josDeviceCharacteristics
};

static int josOpen(
	sqlite3_vfs *pVfs,
	const char *zPath,
	sqlite3_file *pFile,
	int flags,
	int *pOutFlags
){

	int isExclusive  = (flags & SQLITE_OPEN_EXCLUSIVE);
	int isDelete     = (flags & SQLITE_OPEN_DELETEONCLOSE);
	int isCreate     = (flags & SQLITE_OPEN_CREATE);
	int isReadonly   = (flags & SQLITE_OPEN_READONLY);
	int isReadWrite  = (flags & SQLITE_OPEN_READWRITE);

	assert((isReadonly == 0 || isReadWrite == 0) && (isReadWrite || isReadonly));
	assert(isCreate == 0 || isReadWrite);
	assert(isExclusive == 0 || isCreate);
	assert(isDelete == 0 || isCreate);

	//fprintf(stderr, "josOpen(zPath=%s, pFile=0x%x, flags=0x%x)in\n", 
	//	zPath, (unsigned int)pFile, (unsigned int)flags);

	if (flags & SQLITE_OPEN_CREATE) {
		if (userfs_lookup(zPath, USERFS_FILEPP(pFile)) == -ENOENT) {
			if (userfs_create(zPath, USERFS_FILEPP(pFile)) < 0) {
				return SQLITE_CANTOPEN;
			}
		}
	} else {
		// open if it exists, but do not create
		if (userfs_lookup(zPath, USERFS_FILEPP(pFile)) == -ENOENT) {
			return SQLITE_CANTOPEN;
		}
	}

	//userfs_print_file_info(USERFS_FILE(pFile));

	((josFile*)pFile)->pMethod = &sqlite3JosIoMethod;

	return SQLITE_OK;
}

static int josDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync) {
	return SQLITE_OK;
}

static int josAccess(sqlite3_vfs *pVfs, const char *zPath, int flags) {
	return SQLITE_OK;
}

static int josRandomness(sqlite3_vfs *pVfs, int nBuf, char *zBuf){
	static char c = 'x';
	int i;

	memset (zBuf, 0, nBuf);
	for (i = 0; i < nBuf; i ++) {
		c = (char)(c * 31 + 1);
		zBuf[i] = c;
	}
	return SQLITE_OK;
}

static char * alphanum = "abcdefghijklmnopqrstuvwxyz0123456789";
static unsigned int rand_state = 1001;
static char rand_char ()
{
	static unsigned int A = 1664525;
	static unsigned int B = 1013904223;
	rand_state = rand_state * A + B;
	return (alphanum [rand_state % strlen(alphanum)]);
}

static int josGetTempname(sqlite3_vfs *pVfs, int nBuf, char *zBuf) {
	zBuf[0] = '/';
	do { 
		for (int i = 1; i < nBuf-1; i ++) {
			zBuf[i] = rand_char();
		}
		zBuf[nBuf-1] = '\0';
	} while (!userfs_lookup(zBuf, NULL));
	return SQLITE_OK;
}

static int josFullPathname(
	sqlite3_vfs *pVfs,            /* Pointer to vfs object */
	const char *zPath,            /* Possibly relative input path */
	int nOut,                     /* Size of output buffer in bytes */
	char *zOut                    /* Output buffer */
){

	zOut[nOut-1] = '\0';
	if (zPath[0]=='/') {
		sqlite3_snprintf(nOut, zOut, "%s", zPath);
	} else {
		sqlite3_snprintf(nOut-1, zOut, "/%s", zPath);
	}
	return SQLITE_OK;
}

#define josDlOpen  0
#define josDlError 0
#define josDlSym   0
#define josDlClose 0

static int josSleep(sqlite3_vfs *pVfs, int microseconds){
	return microseconds;
}

static int josCurrentTime(sqlite3_vfs *pVfs, double *prNow){
	return 0;
}

sqlite3_vfs *sqlite3OsDefaultVfs(void){
  static sqlite3_vfs josVfs = {
    1,                  /* iVersion */
    sizeof(josFile),   /* szOsFile */
    USERFS_MAX_NAMELEN-1,       /* mxPathname */
    0,                  /* pNext */
    "jos",             /* zName */
    0,                  /* pAppData */
  
    josOpen,           /* xOpen */
    josDelete,         /* xDelete */
    josAccess,         /* xAccess */
    josGetTempname,    /* xGetTempName */
    josFullPathname,   /* xFullPathname */
    josDlOpen,         /* xDlOpen */
    josDlError,        /* xDlError */
    josDlSym,          /* xDlSym */
    josDlClose,        /* xDlClose */
    josRandomness,     /* xRandomness */
    josSleep,          /* xSleep */
    josCurrentTime     /* xCurrentTime */
  };
  
  return &josVfs;
}
 
#endif /* JOSMP_USER */

