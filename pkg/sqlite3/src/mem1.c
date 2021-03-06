/*
** 2007 August 14
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains the C functions that implement a memory
** allocation subsystem for use by SQLite.  
**
** $Id: mem1.c,v 1.16 2008/02/14 23:26:56 drh Exp $
*/
#include "sqliteInt.h"

/*
** This version of the memory allocator is the default.  It is
** used when no other memory allocator is specified using compile-time
** macros.
*/
#ifdef SQLITE_SYSTEM_MALLOC

#ifdef PRIVATE_HEAP
unsigned int using_private_heap = 0;

extern void* private_malloc(size_t bytes);
extern void private_free(void* mem);
extern void *private_realloc(void *ptr, size_t size);
#endif

/*
** All of the static variables used by this module are collected
** into a single structure named "mem".  This is to keep the
** static variables organized and to reduce namespace pollution
** when this module is combined with other in the amalgamation.
*/
static struct {
  /*
  ** The alarm callback and its arguments.  The mem.mutex lock will
  ** be held while the callback is running.  Recursive calls into
  ** the memory subsystem are allowed, but no new callbacks will be
  ** issued.  The alarmBusy variable is set to prevent recursive
  ** callbacks.
  */
  sqlite3_int64 alarmThreshold;
  void (*alarmCallback)(void*, sqlite3_int64,int);
  void *alarmArg;
  int alarmBusy;
  
  /*
  ** Mutex to control access to the memory allocation subsystem.
  */
  sqlite3_mutex *mutex;
  
  /*
  ** Current allocation and high-water mark.
  */
  sqlite3_int64 nowUsed;
  sqlite3_int64 mxUsed;
  
 
} mem;

/*
** Enter the mutex mem.mutex. Allocate it if it is not already allocated.
*/
static void enterMem(void){
  if( mem.mutex==0 ){
    mem.mutex = sqlite3_mutex_alloc(SQLITE_MUTEX_STATIC_MEM);
  }
  sqlite3_mutex_enter(mem.mutex);
}

/*
** Return the amount of memory currently checked out.
*/
sqlite3_int64 sqlite3_memory_used(void){
  sqlite3_int64 n;
  enterMem();
  n = mem.nowUsed;
  sqlite3_mutex_leave(mem.mutex);  
  return n;
}

/*
** Return the maximum amount of memory that has ever been
** checked out since either the beginning of this process
** or since the most recent reset.
*/
sqlite3_int64 sqlite3_memory_highwater(int resetFlag){
  sqlite3_int64 n;
  enterMem();
  n = mem.mxUsed;
  if( resetFlag ){
    mem.mxUsed = mem.nowUsed;
  }
  sqlite3_mutex_leave(mem.mutex);  
  return n;
}

/*
** Change the alarm callback
*/
int sqlite3_memory_alarm(
  void(*xCallback)(void *pArg, sqlite3_int64 used,int N),
  void *pArg,
  sqlite3_int64 iThreshold
){
  enterMem();
  mem.alarmCallback = xCallback;
  mem.alarmArg = pArg;
  mem.alarmThreshold = iThreshold;
  sqlite3_mutex_leave(mem.mutex);
  return SQLITE_OK;
}

/*
** Trigger the alarm 
*/
static void sqlite3MemsysAlarm(int nByte){
  void (*xCallback)(void*,sqlite3_int64,int);
  sqlite3_int64 nowUsed;
  void *pArg;
  if( mem.alarmCallback==0 || mem.alarmBusy  ) return;
  mem.alarmBusy = 1;
  xCallback = mem.alarmCallback;
  nowUsed = mem.nowUsed;
  pArg = mem.alarmArg;
  sqlite3_mutex_leave(mem.mutex);
  xCallback(pArg, nowUsed, nByte);
  sqlite3_mutex_enter(mem.mutex);
  mem.alarmBusy = 0;
}

/*
** Allocate nBytes of memory
*/
void *sqlite3_malloc(int nBytes){
  sqlite3_int64 *p = 0;
  if( nBytes>0 ){
    enterMem();
    if( mem.alarmCallback!=0 && mem.nowUsed+nBytes>=mem.alarmThreshold ){
      sqlite3MemsysAlarm(nBytes);
    }
#ifdef PRIVATE_HEAP
	if (using_private_heap)
		p = private_malloc(nBytes+8);
	else
#endif
		p = malloc(nBytes+8);

    if( p==0 ){
			sqlite3MemsysAlarm(nBytes);
#ifdef PRIVATE_HEAP
			if (using_private_heap)
				p = private_malloc(nBytes+8);
			else
#endif
				p = malloc(nBytes+8);
    }
    if( p ){
      p[0] = nBytes;
      p++;
      mem.nowUsed += nBytes;
      if( mem.nowUsed>mem.mxUsed ){
        mem.mxUsed = mem.nowUsed;
      }
    }
    sqlite3_mutex_leave(mem.mutex);
  }
  return (void*)p; 
}

/*
** Free memory.
*/
void sqlite3_free(void *pPrior){
  sqlite3_int64 *p;
  int nByte;
  if( pPrior==0 ){
    return;
  }
  assert( mem.mutex!=0 );
  p = pPrior;
  p--;
  nByte = (int)*p;
  sqlite3_mutex_enter(mem.mutex);
  mem.nowUsed -= nByte;
#ifdef PRIVATE_HEAP
  if (using_private_heap)
	  private_free(p);
	else
#endif
	  free(p);

  sqlite3_mutex_leave(mem.mutex);  
}

/*
** Return the number of bytes allocated at p.
*/
int sqlite3MallocSize(void *p){
  sqlite3_int64 *pInt;
  if( !p ) return 0;
  pInt = p;
  return pInt[-1];
}

/*
** Change the size of an existing memory allocation
*/
void *sqlite3_realloc(void *pPrior, int nBytes){
  int nOld;
  sqlite3_int64 *p;
  if( pPrior==0 ){
    return sqlite3_malloc(nBytes);
  }
  if( nBytes<=0 ){
    sqlite3_free(pPrior);
    return 0;
  }
  p = pPrior;
  p--;
  nOld = (int)p[0];
  assert( mem.mutex!=0 );
  sqlite3_mutex_enter(mem.mutex);
  if( mem.nowUsed+nBytes-nOld>=mem.alarmThreshold ){
    sqlite3MemsysAlarm(nBytes-nOld);
  }
#ifdef PRIVATE_HEAP
  if (using_private_heap)
	  p = private_realloc(p, nBytes+8);
	else
#endif
	  p = realloc(p, nBytes+8);

  if( p==0 ){
    sqlite3MemsysAlarm(nBytes);
    p = pPrior;
    p--;
#ifdef PRIVATE_HEAP
	if (using_private_heap)
		p = private_realloc(p, nBytes+8);
	else
#endif
		p = realloc(p, nBytes+8);
  }
  if( p ){
    p[0] = nBytes;
    p++;
    mem.nowUsed += nBytes-nOld;
    if( mem.nowUsed>mem.mxUsed ){
      mem.mxUsed = mem.nowUsed;
    }
  }
  sqlite3_mutex_leave(mem.mutex);
  return (void*)p;
}

#endif /* SQLITE_SYSTEM_MALLOC */
