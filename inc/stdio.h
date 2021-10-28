#ifndef JOS_INC_STDIO_H
#define JOS_INC_STDIO_H

#include <inc/types.h>
#include <stdarg.h>

/* lib/printfmt.c */
void	printfmt(void (*putch)(int, void*), void *putdat,
	    const char *fmt, ...)
	    __attribute__((__format__ (__printf__, 3, 4)));
void	vprintfmt(void (*putch)(int, void*), void *putdat,
	    const char *fmt, va_list)
	    __attribute__((__format__ (__printf__, 3, 0)));

const char *e2s(int err);
const char *syscall2s(int sys);

/* lib/printf.c */
int	cprintf(const char *fmt, ...)
	    __attribute__((__format__ (__printf__, 1, 2)));
void	cflush(void);
int	vcprintf(const char *fmt, va_list)
	    __attribute__((__format__ (__printf__, 1, 0)));

#endif
