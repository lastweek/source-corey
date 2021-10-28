
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

/*
** This file implements the SQLite interface to JOSMP.
*/

#define DEBUG_MSG(fp, ...) do { \
	fprintf (fp, "%s:%d ", __FILE__, __LINE__); \
	fprintf (fp, __VA_ARGS__); \
	} while (0)

/*
** Maximum supported path-length.
*/
#define MAX_PATHNAME 512

/*
** The josFile structure is subclass of sqlite3_file specific for the jos
** protability layer.
*/
typedef struct josFile josFile;
struct josFile {
  sqlite3_io_methods const *pMethod;  /* Always the first entry */
  struct fs_handle fh;     /* The file descriptor */
};

/* note that a struct fs_handle looks like:
 * struct fs_handle {
 *		uint8_t fh_dev_id;
 *		union {
 *			struct sobj_ref     fh_ref;
 *			struct ramfs_handle fh_ramfs;
 *			struct vfs0_handle  fh_vfs0;
 *		};
 * }; and
 * struct ramfs_handle
 * {
 * 	struct sobj_ref seg;
 * };
 * and 
 * struct sobj_ref {
 *  uint64_t share;
 *  uint64_t object;
 * };
 */

#define FILE_DEV(fhp) ((fhp)->fh.fh_dev_id)

#define FILE_SHARE(fhp) \
	(((fhp)->fh.fh_dev_id == 'r') ? \
		(fhp)->fh.fh_ramfs.seg.share : \
	((fhp)->fh.fh_dev_id == '0') ? \
		(fhp)->fh.fh_vfs0.wrap.share : \
		(fhp)->fh.fh_ref.share)

#define FILE_OBJECT(fhp) \
	(((fhp)->fh.fh_dev_id == 'r') ? \
		(fhp)->fh.fh_ramfs.seg.object : \
	((fhp)->fh.fh_dev_id == '0') ? \
		(fhp)->fh.fh_vfs0.wrap.object : \
		(fhp)->fh.fh_ref.object)

static char * cwd = "/";
static char * getcwd1(char * trgt, size_t maxlen) {
	if (maxlen < sizeof(cwd)) {
		return (NULL);
	}
	memcpy(trgt, cwd, sizeof(cwd));
	return (trgt);
}

static int josRead(
  sqlite3_file *id, 
  void *pBuf, 
  int amt,
  sqlite3_int64 offset
){
  int got;

  josFile * pFile = (josFile*)id;
  assert( id );

  cprintf(stderr, "josRead file=(%c, %lu, %lu), amt=%d, offset=%lld\n", 
		FILE_DEV(pFile), FILE_SHARE(pFile), FILE_OBJECT(pFile),
			amt, offset);

  TIMER_START;
  got = (int) fs_pread(&pFile->fh, pBuf, (uint64_t)amt, (uint64_t)offset);
  TIMER_END;
  OSTRACE7("READ    file=(%c, %lu, %lu)) %5d %7ld %d\n", 
		FILE_DEV(pFile), FILE_SHARE(pFile), FILE_OBJECT(pFile), 
			got, offset, TIMER_ELAPSED); 

  if( got==amt ){
    return SQLITE_OK;
  }else if( got<0 ){
    return SQLITE_IOERR_READ;
  }else{
    memset(&((char*)pBuf)[got], 0, amt-got);
    return SQLITE_IOERR_SHORT_READ;
  }
}

/*
** Write data from a buffer into a file.  Return SQLITE_OK on success
** or some other error code on failure.
*/
static int josWrite(
  sqlite3_file *id, 
  const void *pBuf, 
  int amt,
  sqlite3_int64 offset 
){
  int wrote = 0, got = 0;
  josFile * pFile = (josFile*)id;

  assert( id );
  assert( amt>0 );

  cprintf(stderr, "josWrite file=(%c, %lu, %lu), amt=%d, offset=%lld\n",
		FILE_DEV(pFile), FILE_SHARE(pFile), FILE_OBJECT(pFile),
			amt, offset);

  TIMER_START;
  while ((amt > 0) && (wrote = (int) fs_pwrite(&pFile->fh, pBuf, (uint64_t)amt, (uint64_t)offset)) > 0) {
    amt -= wrote;
    offset += wrote;
		got += wrote;
    pBuf = &((char *)pBuf)[wrote];
  }
  TIMER_END;
  OSTRACE7("WRITE   file=(%c, %lu, %lu) %5d %7ld %d\n", 
		FILE_DEV(pFile), FILE_SHARE(pFile), FILE_OBJECT(pFile), 
			got, offset, TIMER_ELAPSED);

  if( amt>0 ){
    if( wrote<0 ){
      return SQLITE_IOERR_WRITE;
    }else{
      return SQLITE_FULL;
    }
  }
  return SQLITE_OK;
}

// sync doesn't mean anything for a RAMFS
static int full_fsync(int fd, int fullSync, int dataOnly){
  return SQLITE_OK;
}

static int josSync(sqlite3_file *id, int flags){
  return SQLITE_OK;
}

static int josTruncate(sqlite3_file *id, i64 nByte){
	int rc;
    assert( id );	
	rc = fs_ftruncate(&((josFile*)id)->fh, (uint64_t)nByte);
	fprintf(stderr, "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
	if (rc) {
		return SQLITE_IOERR_TRUNCATE;
	} else {
		return SQLITE_OK;
	}
}

static int josFileSize(sqlite3_file *id, i64 *pSize){
  struct fs_stat stat;
  josFile *pFile = (josFile*)id;

  if (!fs_stat(&pFile->fh, &stat)) {
    *pSize = (i64)stat.size;
    return SQLITE_OK; 
  } else {
    return SQLITE_ERROR; 
  }
}

/*
 * no locking, yet
 */
static int josLock(sqlite3_file *id, int locktype){
  josFile *pFile = (josFile*)id;
	(void)pFile;

  OSTRACE5("LOCK file=(%c, %lu, %lu) %d\n", 
		FILE_DEV(pFile), FILE_SHARE(pFile), FILE_OBJECT(pFile), locktype);

  return (SQLITE_OK);
}

int josCheckReservedLock(sqlite3_file * id) {
	int r = 0;
  josFile *pFile = (josFile*)id;
	(void)pFile; (void)r;

	OSTRACE4("TEST WR-LOCK file=(%c, %lu, %lu)\n", 
		FILE_DEV(pFile), FILE_SHARE(pFile), FILE_OBJECT(pFile));

	return 0;
}

static int josUnlock(sqlite3_file *id, int locktype){
  josFile *pFile = (josFile*)id;
	(void)pFile;

  OSTRACE5("UNLOCK file=(%c, %lu, %lu) %d\n", 
		FILE_DEV(pFile), FILE_SHARE(pFile), FILE_OBJECT(pFile), locktype);

  return (SQLITE_OK);
}

static int josClose(sqlite3_file *id) {
  josFile *pFile = (josFile*)id;
  
  if( !pFile ) return SQLITE_OK;

	OSTRACE4("CLOSE file=(%c, %lu, %lu)\n", 
		FILE_DEV(pFile), FILE_SHARE(pFile), FILE_OBJECT(pFile));

  memset(pFile, 0, sizeof(josFile));
  return SQLITE_OK;
}

/*
** Information and control of an open file handle.
*/
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

/*
** Open a file descriptor to the directory containing file zFilename.
** If successful, *pFd is set to the opened file descriptor and
** SQLITE_OK is returned. If an error occurs, either SQLITE_NOMEM
** or SQLITE_CANTOPEN is returned and *pFd is set to an undefined
** value.
**
** If SQLITE_OK is returned, the caller is responsible for closing
** the file descriptor *pFd using close().
*/
static int openDirectory(const char *zFilename, int *pFd){
  int ii;
  int res, fd = -1;
  char zDirname[MAX_PATHNAME+1];

  sqlite3_snprintf(MAX_PATHNAME, zDirname, "%s", zFilename);
  for(ii=strlen(zDirname); ii>=0 && zDirname[ii]!='/'; ii--);
  if( ii>0 ){
    zDirname[ii] = '\0';
	res = fs_lookup_path(NULL, zDirname, &fd);
  }
  *pFd = fd;
  return (fd>=0 ? SQLITE_OK : SQLITE_CANTOPEN);
}

static int josOpen(
  sqlite3_vfs *pVfs,
  const char *zPath,
  sqlite3_file *pFile,
  int flags,
  int *pOutFlags
){
	size_t sz, split;
	char * dirpath, * fname;
	struct fs_handle dirh;
	josFile *pNew;
	char local_zPath [MAX_PATHNAME+1]; 
	int isCreate = (flags & SQLITE_OPEN_CREATE);

	local_zPath[0] = '\0';
	local_zPath[MAX_PATHNAME] = '\0';

	memset(pFile, 0, sizeof(josFile));
	pNew = (josFile *)pFile;

	if (isCreate) {
		// create the file
		if (fs_namei(zPath, &pNew->fh) != 0) {
			// does not exist

			// split into directory and filename, find the last '/'
			sz = strnlen(zPath, MAX_PATHNAME);
			strncpy(local_zPath, zPath, MAX_PATHNAME);
			for (split = sz-1; split>0; split--) {
				if (local_zPath[split] == '/') break;
			}
			local_zPath[split] = '\0';
			dirpath = &local_zPath[0];
			fname = (split == sz-1) ? NULL : &local_zPath[split+1];
			if (fname == NULL || fs_namei(dirpath, &dirh) != 0) {
				// no file name or couldn't open up parent handle
				return SQLITE_CANTOPEN;
			}
			if (fs_create(&dirh, fname, &pNew->fh) != 0) {	
				return SQLITE_CANTOPEN;	
			}
		} else {
			// the file already exists
//			fs_ftruncate(&pNew->fh, 0);
		}
	} else {
		// open, but do not create
		if (fs_namei(zPath, &pNew->fh) != 0) {
			// file does not exist
			return SQLITE_CANTOPEN;
		}
	}

	OSTRACE5("OPEN file=(%c, %lu, %lu) %s\n", 
		FILE_DEV(pNew), FILE_SHARE(pNew), FILE_OBJECT(pNew), zPath);

	pNew->pMethod = &sqlite3JosIoMethod;

	return SQLITE_OK;
}

static int josDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync){
	// currently not implement the real unlink operation
	josFile pFile;
	if (fs_namei(zPath, &pFile.fh) != 0) {
		fprintf(stderr, "Warning: Should not unlink a file which does not exist\n");
		return SQLITE_INTERNAL; 
	} else {
		fs_ftruncate(&pFile.fh, 0);
	}
	return SQLITE_OK;
}

static int josAccess(sqlite3_vfs *pVfs, const char *zPath, int flags){
  return SQLITE_OK;
}

static int josGetTempname(sqlite3_vfs *pVfs, int nBuf, char *zBuf){
	static const char *azDirs[] = {
		0,
		"/var/tmp",
		"/usr/tmp",
		"/tmp",
		".",
	};
	static const unsigned char zChars[] =
		"abcdefghijklmnopqrstuvwxyz"
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"0123456789";
	int i, j;
	const char *zDir = ".";
	struct fs_handle fh;

	azDirs[0] = sqlite3_temp_directory;

	// find a directory that exists
	for(i=0; i<sizeof(azDirs)/sizeof(azDirs[0]); i++){
		if( azDirs[i]==0 ) continue;
		if (fs_namei(azDirs[i], &fh) != 0) 
			continue;
    zDir = azDirs[i];
    break;
  }

	if( strlen(zDir) - sizeof(SQLITE_TEMP_FILE_PREFIX) - 17 <= 0 ){
		return SQLITE_ERROR;
	}

	// create a random file name
	do{
		assert( pVfs->mxPathname==MAX_PATHNAME );
		sqlite3_snprintf(nBuf-17, zBuf, "%s/"SQLITE_TEMP_FILE_PREFIX, zDir);
		j = strlen(zBuf);
		sqlite3Randomness(15, &zBuf[j]);
		for(i=0; i<15; i++, j++){
			zBuf[j] = (char)zChars[ ((unsigned char)zBuf[j])%(sizeof(zChars)-1) ];
		}
		zBuf[j] = 0;
	} while (fs_namei(zBuf, &fh) == 0);

  return SQLITE_OK;
}

static int josFullPathname(
  sqlite3_vfs *pVfs,            /* Pointer to vfs object */
  const char *zPath,            /* Possibly relative input path */
  int nOut,                     /* Size of output buffer in bytes */
  char *zOut                    /* Output buffer */
){
	assert(pVfs->mxPathname==MAX_PATHNAME);

  zOut[nOut-1] = '\0';
	if (zPath[0]=='/') {
		// already an absolute pathname
		sqlite3_snprintf(nOut, zOut, "%s", zPath);
	} else {
		// a relative pathname; transform it to absolute
		int nCwd;
		if (getcwd1(zOut, nOut-1)==0) {
			return SQLITE_CANTOPEN;
		}
		nCwd = strlen(zOut);
	  sqlite3_snprintf(nOut-nCwd, &zOut[nCwd], "%s", zPath);
	}
  return SQLITE_OK;
}

#define josDlOpen  0
#define josDlError 0
#define josDlSym   0
#define josDlClose 0

/*
 * write nBuf bytes of "random" data to the supplied buffer zBuf
 */
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

static int josSleep(sqlite3_vfs *pVfs, int microseconds){
  // usleep(microseconds); XXX eventually
  return microseconds;
}

static int josCurrentTime(sqlite3_vfs *pVfs, double *prNow){
  return 0;
}

sqlite3_vfs *sqlite3OsDefaultVfs(void){
  static sqlite3_vfs josVfs = {
    1,                  /* iVersion */
    sizeof(josFile),   /* szOsFile */
    MAX_PATHNAME,       /* mxPathname */
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

