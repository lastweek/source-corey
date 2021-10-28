#ifndef __MR_COMMON_H_
#define __MR_COMMON_H_
#include <windows.h>
#include <tchar.h>
#include <stdio.h>

#define SET_AFFINITY

/* 
 *	some Linux-specic functions are ported to windows here
 */ 
/*
 *  getopt function in windows
 */ 
extern char *optarg;
extern DWORD gFileSize;
int getopt(int argc, char *const argv[], const char *optstring);

/*
 * map a file into memory
 */ 
int getFileMap(TCHAR * filename, char **data);
void closeFileMap();

#define dbg_int_msg(msg,p)		\
    printf(msg);	\
    printf(" : %d\n", p);
#endif
