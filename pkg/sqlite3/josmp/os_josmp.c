
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <inc/lib.h>

#include "sqliteInt.h"
#include "os_common.h"

#include "userfs.h"

#if JOSMP_USER 

#define USERFS_MAX_NAMELEN 32
#define USERFS_FILE(x) ((struct userfs_file*)(((josmpFile*)(x))->fh))
#define USERFS_FILEPP(x) ((struct userfs_file**)(&(((josmpFile*)(x))->fh)))


static void enterMutex(){
	  sqlite3_mutex_enter(sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_MASTER));
}
static void leaveMutex(){
	  sqlite3_mutex_leave(sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_MASTER));
}

typedef struct josThread {
	thread_id_t tid;
	proc_id_t pid;
} jos_thread_t;

/*
 * ** An instance of the following structure serves as the key used
 * ** to locate a particular lockInfo structure given its inode.
 * **
 * ** If threads cannot override each others locks, then we set the
 * ** lockKey.tid field to the thread ID.  If threads can override
 * ** each others locks then tid is always set to zero.  tid is omitted
 * ** if we compile without threading support.
 * */
struct lockKey {
	userfs_dev_t dev;       /* Device number */
	userfs_ino_t ino;       /* Inode number */
#if SQLITE_THREADSAFE
	jos_thread_t jtid;   /* Thread ID or zero if threads can override each other */
#endif
};


/*
 * ** An instance of the following structure is allocated for each open
 * ** inode on each thread with a different process ID.  (Threads have
 * ** different process IDs on linux, but not on most other unixes.)
 * **
 * ** A single inode can have multiple file descriptors, so each unixFile
 * ** structure contains a pointer to an instance of this object and this
 * ** object keeps a count of the number of unixFile pointing to it.
 * */
struct lockInfo {
	struct lockKey key;  /* The lookup key */
	int cnt;             /* Number of SHARED locks held */
	int locktype;        /* One of SHARED_LOCK, RESERVED_LOCK etc. */
	int nRef;            /* Number of pointers to this structure */
};

/*
 * ** An instance of the following structure serves as the key used
 * ** to locate a particular openCnt structure given its inode.  This
 * ** is the same as the lockKey except that the thread ID is omitted.
 * */
struct openKey {
	userfs_dev_t dev;   /* Device number */
	userfs_ino_t ino;   /* Inode number */
};


/*
 * ** An instance of the following structure is allocated for each open
 * ** inode.  This structure keeps track of the number of locks on that
 * ** inode.  If a close is attempted against an inode that is holding
 * ** locks, the close is deferred until all locks clear by adding the
 * ** file descriptor to be closed to the pending list.
 * */
struct openCnt {
	struct openKey key;   /* The lookup key */
	int nRef;             /* Number of pointers to this structure */
	int nLock;            /* Number of outstanding locks */
	int nPending;         /* Number of pending close() operations */
	int *aPending;        /* Malloced space holding fd's awaiting a close() */
};

/*
 * ** These hash tables map inodes and file descriptors (really, lockKey and
 * ** openKey structures) into lockInfo and openCnt structures.  Access to
 * ** these hash tables must be protected by a mutex.
 * */
static Hash lockHash = {SQLITE_HASH_BINARY, 0, 0, 0, 0, 0};
static Hash openHash = {SQLITE_HASH_BINARY, 0, 0, 0, 0, 0};

static int threadsOverrideEachOthersLocks;

/*
** The josmpFile structure is subclass of sqlite3_file specific for the josmp
** protability layer.
*/
typedef struct josmpFile josmpFile;
struct josmpFile {
	sqlite3_io_methods const *pMethod;  /* Always the first entry */
	struct userfs_file * fh;     /* The file descriptor */
	struct openCnt *pOpen;    /* Info about all open fd's on this inode */
	struct lockInfo *pLock;   /* Info about locks on this inode */
	unsigned char locktype;		/* The type of lock held on this fd */
#if SQLITE_THREADSAFE
	jos_thread_t jtid;            /* The thread that "owns" this unixFile */
#endif
};

/*
 * ** Release a lockInfo structure previously allocated by findLockInfo().
 * */
static void releaseLockInfo(struct lockInfo *pLock){
	if (pLock == NULL)
		return;
	pLock->nRef--;
	if( pLock->nRef==0 ){
		sqlite3HashInsert(&lockHash, &pLock->key, sizeof(pLock->key), 0);
		sqlite3_free(pLock);
	}
}

/*
 * ** Release a openCnt structure previously allocated by findLockInfo().
 * */
static void releaseOpenCnt(struct openCnt *pOpen){
	if (pOpen == NULL)
		return;
	pOpen->nRef--;
	if( pOpen->nRef==0 ){
		sqlite3HashInsert(&openHash, &pOpen->key, sizeof(pOpen->key), 0);
		if (pOpen->aPending) free(pOpen->aPending);
		sqlite3_free(pOpen);
	}
}

/*
** Given a file descriptor, locate lockInfo and openCnt structures that
** describes that file descriptor.  Create new ones if necessary.  The
** return values might be uninitialized if an error occurs.
**
** Return the number of errors.
*/
static int findLockInfo(
  struct userfs_file *fh,     /* The file descriptor used in the key */
  struct lockInfo **ppLock,    /* Return the lockInfo structure here */
  struct openCnt **ppOpen      /* Return the openCnt structure here */
){
  int rc;
  struct lockKey key1;
  struct openKey key2;
  struct userfs_stat statbuf;
  struct lockInfo *pLock;
  struct openCnt *pOpen;
  rc = userfs_fstat(fh, &statbuf);
  if( rc!=0 ) return 1;

  memset(&key1, 0, sizeof(key1));
  key1.dev = statbuf.st_dev;
  key1.ino = statbuf.st_ino;
#if SQLITE_THREADSAFE
//  if( threadsOverrideEachOthersLocks<0 ){
//    testThreadLockingBehavior(fd);
//  }
  threadsOverrideEachOthersLocks = 0;
  key1.jtid.tid = threadsOverrideEachOthersLocks ? 0 : thread_id();
  key1.jtid.pid = threadsOverrideEachOthersLocks ? 0 : processor_current_procid();
#endif
  memset(&key2, 0, sizeof(key2));
  key2.dev = statbuf.st_dev;
  key2.ino = statbuf.st_ino;
  pLock = (struct lockInfo*)sqlite3HashFind(&lockHash, &key1, sizeof(key1));
  if( pLock==0 ){
    struct lockInfo *pOld;
    pLock = sqlite3_malloc( sizeof(*pLock) );
    if( pLock==0 ){
      rc = 1;
      goto exit_findlockinfo;
    }
    pLock->key = key1;
    pLock->nRef = 1;
    pLock->cnt = 0;
    pLock->locktype = 0;
    pOld = sqlite3HashInsert(&lockHash, &pLock->key, sizeof(key1), pLock);
    if( pOld!=0 ){
      assert( pOld==pLock );
      sqlite3_free(pLock);
      rc = 1;
      goto exit_findlockinfo;
    }
  }else{
    pLock->nRef++;
  }
  *ppLock = pLock;
  if( ppOpen!=0 ){
    pOpen = (struct openCnt*)sqlite3HashFind(&openHash, &key2, sizeof(key2));
    if( pOpen==0 ){
      struct openCnt *pOld;
      pOpen = sqlite3_malloc( sizeof(*pOpen) );
      if( pOpen==0 ){
        releaseLockInfo(pLock);
        rc = 1;
        goto exit_findlockinfo;
      }
      pOpen->key = key2;
      pOpen->nRef = 1;
      pOpen->nLock = 0;
      pOpen->nPending = 0;
      pOpen->aPending = 0;
      pOld = sqlite3HashInsert(&openHash, &pOpen->key, sizeof(key2), pOpen);
      if( pOld!=0 ){
        assert( pOld==pOpen );
        sqlite3_free(pOpen);
        releaseLockInfo(pLock);
        rc = 1;
        goto exit_findlockinfo;
      }
    }else{
      pOpen->nRef++;
    }
    *ppOpen = pOpen;
  }

exit_findlockinfo:
  return rc;
}

static int josmpRead (
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

static int josmpWrite(
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
		assert(!"os_josmp_userfs bad");
	}
	return SQLITE_OK;
}

// sync doesn't mean anything for userfs
static int full_fsync(int fd, int fullSync, int dataOnly){
	return SQLITE_OK;
}

static int josmpSync(sqlite3_file *id, int flags){
	return SQLITE_OK;
}

static int josmpTruncate(sqlite3_file *id, i64 nByte) {
	int rc;

	rc = userfs_ftruncate(USERFS_FILE(id), (uint64_t)nByte);
	if (rc) {
		return SQLITE_OK;
	} else {
		assert(!"os_josmp_userfs bad");
	}
	return SQLITE_OK;
}

static int josmpFileSize(sqlite3_file *id, i64 *pSize) {
	struct userfs_stat st;

	if (!userfs_fstat(USERFS_FILE(id), &st)) {
		*pSize = (i64)st.st_size;
		return SQLITE_OK; 
	} else {
		assert(!"os_josmp_userfs bad");
	}
	return SQLITE_OK;
}

static inline int jos_thread_equal(jos_thread_t *t1, jos_thread_t *t2) {
	    return (t1->tid == t2->tid) && (t1->pid == t2->pid);
}

#ifdef SQLITE_DEBUG
/*
** Helper function for printing out trace information from debugging
** binaries. This returns the string represetation of the supplied
** integer lock-type.
*/
static const char *locktypeName(int locktype){
  switch( locktype ){
  case NO_LOCK: return "NONE";
  case SHARED_LOCK: return "SHARED";
  case RESERVED_LOCK: return "RESERVED";
  case PENDING_LOCK: return "PENDING";
  case EXCLUSIVE_LOCK: return "EXCLUSIVE";
  }
  return "ERROR";
}
#endif

/*
** If we are currently in a different thread than the thread that the
** unixFile argument belongs to, then transfer ownership of the unixFile
** over to the current thread.
**
** A unixFile is only owned by a thread on systems where one thread is
** unable to override locks created by a different thread.  RedHat9 is
** an example of such a system.
**
** Ownership transfer is only allowed if the unixFile is currently unlocked.
** If the unixFile is locked and an ownership is wrong, then return
** SQLITE_MISUSE.  SQLITE_OK is returned if everything works.
*/
#if SQLITE_THREADSAFE
static int transferOwnership(josmpFile *pFile){
  int rc;
  jos_thread_t hSelf;
  if( threadsOverrideEachOthersLocks ){
    /* Ownership transfers not needed on this system */
    return SQLITE_OK;
  }
  hSelf.tid = thread_id();
  hSelf.pid = processor_current_procid();
  if( jos_thread_equal(&pFile->jtid, &hSelf) ){
    /* We are still in the same thread */
    OSTRACE1("No-transfer, same thread\n");
    return SQLITE_OK;
  }
  if( pFile->locktype!=NO_LOCK ){
    /* We cannot change ownership while we are holding a lock! */
    return SQLITE_MISUSE;
  }
  OSTRACE6("Transfer ownership of %p from (%d,%d) to (%d,%d)\n",
            pFile->fh, pFile->jtid.pid, pFile->jtid.tid, hSelf.pid, hSelf.tid);
  pFile->jtid.pid = hSelf.pid;
  pFile->jtid.tid = hSelf.tid;
  if (pFile->pLock != NULL) {
    releaseLockInfo(pFile->pLock);
    rc = findLockInfo(pFile->fh, &pFile->pLock, 0);
    OSTRACE5("LOCK    %p is now %s(%s,%d)\n", pFile->fh,
           locktypeName(pFile->locktype),
           locktypeName(pFile->pLock->locktype), pFile->pLock->cnt);
    return rc;
  } else {
    return SQLITE_OK;
  }
}
#else
  /* On single-threaded builds, ownership transfer is a no-op */
# define transferOwnership(X) SQLITE_OK
#endif

/*
** Lock the file with the lock specified by parameter locktype - one
** of the following:
**
**     (1) SHARED_LOCK
**     (2) RESERVED_LOCK
**     (3) PENDING_LOCK
**     (4) EXCLUSIVE_LOCK
**
** Sometimes when requesting one lock state, additional lock states
** are inserted in between.  The locking might fail on one of the later
** transitions leaving the lock state different from what it started but
** still short of its goal.  The following chart shows the allowed
** transitions and the inserted intermediate states:
**
**    UNLOCKED -> SHARED
**    SHARED -> RESERVED
**    SHARED -> (PENDING) -> EXCLUSIVE
**    RESERVED -> (PENDING) -> EXCLUSIVE
**    PENDING -> EXCLUSIVE
**
** This routine will only increase a lock.  Use the sqlite3OsUnlock()
** routine to lower a locking level.
*/
static int josmpLock(sqlite3_file *id, int locktype) {
  /* The following describes the implementation of the various locks and
  ** lock transitions in terms of the POSIX advisory shared and exclusive
  ** lock primitives (called read-locks and write-locks below, to avoid
  ** confusion with SQLite lock names). The algorithms are complicated
  ** slightly in order to be compatible with windows systems simultaneously
  ** accessing the same database file, in case that is ever required.
  **
  ** Symbols defined in os.h indentify the 'pending byte' and the 'reserved
  ** byte', each single bytes at well known offsets, and the 'shared byte
  ** range', a range of 510 bytes at a well known offset.
  **
  ** To obtain a SHARED lock, a read-lock is obtained on the 'pending
  ** byte'.  If this is successful, a random byte from the 'shared byte
  ** range' is read-locked and the lock on the 'pending byte' released.
  **
  ** A process may only obtain a RESERVED lock after it has a SHARED lock.
  ** A RESERVED lock is implemented by grabbing a write-lock on the
  ** 'reserved byte'. 
  **
  ** A process may only obtain a PENDING lock after it has obtained a
  ** SHARED lock. A PENDING lock is implemented by obtaining a write-lock
  ** on the 'pending byte'. This ensures that no new SHARED locks can be
  ** obtained, but existing SHARED locks are allowed to persist. A process
  ** does not have to obtain a RESERVED lock on the way to a PENDING lock.
  ** This property is used by the algorithm for rolling back a journal file
  ** after a crash.
  **
  ** An EXCLUSIVE lock, obtained after a PENDING lock is held, is
  ** implemented by obtaining a write-lock on the entire 'shared byte
  ** range'. Since all other locks require a read-lock on one of the bytes
  ** within this range, this ensures that no other locks are held on the
  ** database. 
  **
  ** The reason a single byte cannot be used instead of the 'shared byte
  ** range' is that some versions of windows do not support read-locks. By
  ** locking a random byte from a range, concurrent SHARED locks may exist
  ** even if the locking primitive used is always a write-lock.
  */
	return SQLITE_OK;
  int rc = SQLITE_OK;
  josmpFile *pFile = (josmpFile*)id;
  struct lockInfo *pLock = pFile->pLock;
  struct userfs_flock lock;
  int s;

  assert( pFile );
  OSTRACE6("LOCK    %p %s was %s(%s,%d)\n", pFile->fh,
      locktypeName(locktype), locktypeName(pFile->locktype),
      locktypeName(pLock->locktype), pLock->cnt);

  /* If there is already a lock of this type or more restrictive on the
  ** unixFile, do nothing. Don't use the end_lock: exit path, as
  ** enterMutex() hasn't been called yet.
  */
  if( pFile->locktype>=locktype ){
    OSTRACE3("LOCK    %p %s ok (already held)\n", pFile->fh,
            locktypeName(locktype));
    return SQLITE_OK;
  }

  /* Make sure the locking sequence is correct
  */
  assert( pFile->locktype!=NO_LOCK || locktype==SHARED_LOCK );
  assert( locktype!=PENDING_LOCK );
  assert( locktype!=RESERVED_LOCK || pFile->locktype==SHARED_LOCK );

  /* This mutex is needed because pFile->pLock is shared across threads
  */
  enterMutex();

  /* Make sure the current thread owns the pFile.
  */
  rc = transferOwnership(pFile);
  if( rc!=SQLITE_OK ){
    leaveMutex();
    return rc;
  }
  pLock = pFile->pLock;

  /* If some thread using this PID has a lock via a different unixFile*
  ** handle that precludes the requested lock, return BUSY.
  */
  if( (pFile->locktype!=pLock->locktype && 
          (pLock->locktype>=PENDING_LOCK || locktype>SHARED_LOCK))
  ){
    rc = SQLITE_BUSY;
    goto end_lock;
  }

  /* If a SHARED lock is requested, and some thread using this PID already
  ** has a SHARED or RESERVED lock, then increment reference counts and
  ** return SQLITE_OK.
  */
  if( locktype==SHARED_LOCK && 
      (pLock->locktype==SHARED_LOCK || pLock->locktype==RESERVED_LOCK) ){
    assert( locktype==SHARED_LOCK );
    assert( pFile->locktype==0 );
    assert( pLock->cnt>0 );
    pFile->locktype = SHARED_LOCK;
    pLock->cnt++;
    pFile->pOpen->nLock++;
    goto end_lock;
  }

  lock.l_len = 1L;

  lock.l_whence = U_SEEK_SET;

  /* A PENDING lock is needed before acquiring a SHARED lock and before
  ** acquiring an EXCLUSIVE lock.  For the SHARED lock, the PENDING will
  ** be released.
  */
  if( locktype==SHARED_LOCK 
      || (locktype==EXCLUSIVE_LOCK && pFile->locktype<PENDING_LOCK)
  ){
    lock.l_type = (locktype==SHARED_LOCK?UF_RDLCK_:UF_WRLCK_);
    lock.l_start = PENDING_BYTE;
    s = userfs_fcntl(pFile->fh, UF_SETLK, &lock);
    if( s==(-1) ){
      rc = (errno==EINVAL) ? SQLITE_NOLFS : SQLITE_BUSY;
      goto end_lock;
    }
  }


  /* If control gets to this point, then actually go ahead and make
  ** operating system calls for the specified lock.
  */
  if( locktype==SHARED_LOCK ){
    assert( pLock->cnt==0 );
    assert( pLock->locktype==0 );

    /* Now get the read-lock */
    lock.l_start = SHARED_FIRST;
    lock.l_len = SHARED_SIZE;
    s = userfs_fcntl(pFile->fh, UF_SETLK, &lock);

    /* Drop the temporary PENDING lock */
    lock.l_start = PENDING_BYTE;
    lock.l_len = 1L;
    lock.l_type = UF_UNLCK_;
    if( userfs_fcntl(pFile->fh, UF_SETLK, &lock)!=0 ){
      rc = SQLITE_IOERR_UNLOCK;  /* This should never happen */
      goto end_lock;
    }
    if( s==(-1) ){
      rc = (errno==EINVAL) ? SQLITE_NOLFS : SQLITE_BUSY;
    }else{
      pFile->locktype = SHARED_LOCK;
      pFile->pOpen->nLock++;
      pLock->cnt = 1;
    }
  }else if( locktype==EXCLUSIVE_LOCK && pLock->cnt>1 ){
    /* We are trying for an exclusive lock but another thread in this
    ** same process is still holding a shared lock. */
    rc = SQLITE_BUSY;
  }else{
    /* The request was for a RESERVED or EXCLUSIVE lock.  It is
    ** assumed that there is a SHARED or greater lock on the file
    ** already.
    */
    assert( 0!=pFile->locktype );
    lock.l_type = UF_WRLCK_;
    switch( locktype ){
      case RESERVED_LOCK:
        lock.l_start = RESERVED_BYTE;
        break;
      case EXCLUSIVE_LOCK:
        lock.l_start = SHARED_FIRST;
        lock.l_len = SHARED_SIZE;
        break;
      default:
        assert(0);
    }
    s = userfs_fcntl(pFile->fh, UF_SETLK, &lock);
    if( s==(-1) ){
      rc = (errno==EINVAL) ? SQLITE_NOLFS : SQLITE_BUSY;
    }
  }
  
  if( rc==SQLITE_OK ){
    pFile->locktype = locktype;
    pLock->locktype = locktype;
  }else if( locktype==EXCLUSIVE_LOCK ){
    pFile->locktype = PENDING_LOCK;
    pLock->locktype = PENDING_LOCK;
  }

end_lock:
  leaveMutex();
  OSTRACE4("LOCK    %p %s %s\n", pFile->fh, locktypeName(locktype), 
      rc==SQLITE_OK ? "ok" : "failed");
  return rc;
}

/*
** This routine checks if there is a RESERVED lock held on the specified
** file by this or any other process. If such a lock is held, return
** non-zero.  If the file is unlocked or holds only SHARED locks, then
** return zero.
*/
int josmpCheckReservedLock(sqlite3_file * id) {
	return SQLITE_OK;
  int r = 0;
  josmpFile *pFile = (josmpFile*)id;

  assert( pFile );
  enterMutex(); /* Because pFile->pLock is shared across threads */

  /* Check if a thread in this process holds such a lock */
  if( pFile->pLock->locktype>SHARED_LOCK ){
    r = 1;
  }

  /* Otherwise see if some other process holds it.
  */
  if( !r ){
    struct userfs_flock lock;
    lock.l_whence = U_SEEK_SET;
    lock.l_start = RESERVED_BYTE;
    lock.l_len = 1;
    lock.l_type = UF_WRLCK_;
    userfs_fcntl(pFile->fh, UF_GETLK, &lock);
    if( lock.l_type!=UF_UNLCK_ ){
      r = 1;
    }
  }
  
  leaveMutex();
  OSTRACE3("TEST WR-LOCK %p %d\n", pFile->fh, r);

  return r;
}

/*
** Lower the locking level on file descriptor pFile to locktype.  locktype
** must be either NO_LOCK or SHARED_LOCK.
**
** If the locking level of the file descriptor is already at or below
** the requested locking level, this routine is a no-op.
*/
static int josmpUnlock(sqlite3_file *id, int locktype) {
	return SQLITE_OK;
  struct lockInfo *pLock;
  struct userfs_flock lock;
  int rc = SQLITE_OK;
  josmpFile *pFile = (josmpFile*)id;
  struct userfs_file *h;

  assert( pFile );
  OSTRACE6("UNLOCK  %p %d was %d(%d,%d)\n", pFile->fh, locktype,
      pFile->locktype, pFile->pLock->locktype, pFile->pLock->cnt);

  assert( locktype<=SHARED_LOCK );
  if( pFile->locktype<=locktype ){
    return SQLITE_OK;
  }
  jos_thread_t hSelf;
  hSelf.pid = processor_current_procid();
  hSelf.tid = thread_id();
  if ((threadsOverrideEachOthersLocks == 0) && !jos_thread_equal(&pFile->jtid, &hSelf))
  {
    return SQLITE_MISUSE;
  }
  enterMutex();
  h = pFile->fh;
  pLock = pFile->pLock;
  assert( pLock->cnt!=0 );
  if( pFile->locktype>SHARED_LOCK ){
    assert( pLock->locktype==pFile->locktype );
    //SimulateIOErrorBenign(1);
    //SimulateIOError( h=(-1) )
    //SimulateIOErrorBenign(0);
    if( locktype==SHARED_LOCK ){
      lock.l_type = UF_RDLCK_;
      lock.l_whence = U_SEEK_SET;
      lock.l_start = SHARED_FIRST;
      lock.l_len = SHARED_SIZE;
      if( userfs_fcntl(h, UF_SETLK, &lock)==(-1) ){
        rc = SQLITE_IOERR_RDLOCK;
      }
    }
    lock.l_type = UF_UNLCK_;
    lock.l_whence = U_SEEK_SET;
    lock.l_start = PENDING_BYTE;
    lock.l_len = 2L;  assert( PENDING_BYTE+1==RESERVED_BYTE );
    if( userfs_fcntl(h, UF_SETLK, &lock)!=(-1) ){
      pLock->locktype = SHARED_LOCK;
    }else{
      rc = SQLITE_IOERR_UNLOCK;
    }
  }
  if( locktype==NO_LOCK ){
    struct openCnt *pOpen;

    /* Decrement the shared lock counter.  Release the lock using an
    ** OS call only when all threads in this same process have released
    ** the lock.
    */
    pLock->cnt--;
    if( pLock->cnt==0 ){
      lock.l_type = UF_UNLCK_;
      lock.l_whence = U_SEEK_SET;
      lock.l_start = lock.l_len = 0L;
//      SimulateIOErrorBenign(1);
//      SimulateIOError( h=(-1) )
//      SimulateIOErrorBenign(0);
      if( userfs_fcntl(h, UF_SETLK, &lock)!=(-1) ){
        pLock->locktype = NO_LOCK;
      }else{
        rc = SQLITE_IOERR_UNLOCK;
        pLock->cnt = 1;
      }
    }

    /* Decrement the count of locks against this same file.  When the
    ** count reaches zero, close any other file descriptors whose close
    ** was deferred because of outstanding locks.
    */
    if( rc==SQLITE_OK ){
      pOpen = pFile->pOpen;
      pOpen->nLock--;
      assert( pOpen->nLock>=0 );
      if( pOpen->nLock==0 && pOpen->nPending>0 ){
        int i;
        for(i=0; i<pOpen->nPending; i++){
//          close(pOpen->aPending[i]);
        }
//        free(pOpen->aPending);
        pOpen->nPending = 0;
        pOpen->aPending = 0;
      }
    }
  }
  leaveMutex();
  if( rc==SQLITE_OK ) pFile->locktype = locktype;
  return rc;
}

static int josmpClose(sqlite3_file *id) {
	return SQLITE_OK;
}

static int josmpFileControl(sqlite3_file *id, int op, void *pArg) {
	return SQLITE_OK;
}

static int josmpSectorSize(sqlite3_file *id){
	return SQLITE_DEFAULT_SECTOR_SIZE;
}

static int josmpDeviceCharacteristics(sqlite3_file *id){
	return 0;
}

/*
** This vector defines all the methods that can operate on an sqlite3_file
** for josmp.
*/
static const sqlite3_io_methods sqlite3JosIoMethod = {
  1,                        /* iVersion */
  josmpClose,
  josmpRead,
  josmpWrite,
  josmpTruncate,
  josmpSync,
  josmpFileSize,
  josmpLock,
  josmpUnlock,
  josmpCheckReservedLock,
  josmpFileControl,
  josmpSectorSize,
  josmpDeviceCharacteristics
};

static int josmpOpen(
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

//	fprintf(stderr, "josmpOpen(zPath=%s, pFile=0x%x, flags=0x%x)in\n", 
//		zPath, (unsigned int)pFile, (unsigned int)flags);

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

	josmpFile *pNew = (josmpFile *)pFile;
	pNew->pMethod = &sqlite3JosIoMethod;
/*	
	enterMutex();
	int rc = findLockInfo(pNew->fh, &pNew->pLock, &pNew->pOpen);
	leaveMutex();
	if (rc) {
		fprintf(stderr, "WARNING: Failed to find lock info for file\n");
		return SQLITE_NOMEM;
	}
*/	
#if SQLITE_THREADSAFE
	((josmpFile*)pFile)->jtid.tid = thread_id();
	((josmpFile*)pFile)->jtid.pid = processor_current_procid();
#endif	
	return SQLITE_OK;
}

static int josmpDelete(sqlite3_vfs *pVfs, const char *zPath, int dirSync) {
	return SQLITE_OK;
}

static int josmpAccess(sqlite3_vfs *pVfs, const char *zPath, int flags) {
	return SQLITE_OK;
}

static int josmpRandomness(sqlite3_vfs *pVfs, int nBuf, char *zBuf){
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

static int josmpGetTempname(sqlite3_vfs *pVfs, int nBuf, char *zBuf) {
	zBuf[0] = '/';
	do { 
		for (int i = 1; i < nBuf-1; i ++) {
			zBuf[i] = rand_char();
		}
		zBuf[nBuf-1] = '\0';
	} while (!userfs_lookup(zBuf, NULL));
	return SQLITE_OK;
}

static int josmpFullPathname(
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

#define josmpDlOpen  0
#define josmpDlError 0
#define josmpDlSym   0
#define josmpDlClose 0

static int josmpSleep(sqlite3_vfs *pVfs, int microseconds){
	return microseconds;
}

static int josmpCurrentTime(sqlite3_vfs *pVfs, double *prNow){
	return 0;
}

sqlite3_vfs *sqlite3OsDefaultVfs(void){
  static sqlite3_vfs josmpVfs = {
    1,                  /* iVersion */
    sizeof(josmpFile),   /* szOsFile */
    USERFS_MAX_NAMELEN-1,       /* mxPathname */
    0,                  /* pNext */
    "josmp",             /* zName */
    0,                  /* pAppData */
  
    josmpOpen,           /* xOpen */
    josmpDelete,         /* xDelete */
    josmpAccess,         /* xAccess */
    josmpGetTempname,    /* xGetTempName */
    josmpFullPathname,   /* xFullPathname */
    josmpDlOpen,         /* xDlOpen */
    josmpDlError,        /* xDlError */
    josmpDlSym,          /* xDlSym */
    josmpDlClose,        /* xDlClose */
    josmpRandomness,     /* xRandomness */
    josmpSleep,          /* xSleep */
    josmpCurrentTime     /* xCurrentTime */
  };
  
  return &josmpVfs;
}
 
#endif /* JOSMP_USER */

