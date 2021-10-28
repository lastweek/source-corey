#ifndef JOS_KERN_LIB_H
#define JOS_KERN_LIB_H

#include <machine/types.h>
#include <machine/compiler.h>
#include <inc/spinlock.h>
#include <inc/locality.h>
#include <inc/array.h>
#include <inc/pagezero.h>
#include <stdarg.h>

void *memset(void *dest, int c, size_t len);
void *memcpy(void *dest, const void *src, size_t len);
void *memmove(void *dest, const void *src, size_t len);
int   memcmp(const void *s1, const void *s2, size_t n);
char *strchr (const char *p, int ch);
int strcmp (const char *s1, const char *s2);
int strncmp (const char *p, const char *q, size_t n);
size_t strlen (const char *);
char *strncpy(char *dest, const char *src, size_t size);
char *strstr(const char *haystack, const char *needle);

/* printfmt.c */
int vsnprintf(char *str, size_t size, const char *fmt, va_list)
	__attribute__((__format__ (__printf__, 3, 0)));
void printfmt (void (*putch) (int, void *), void *putdat,
	const char *fmt, ...)
	__attribute__((__format__ (__printf__, 3, 4)));
void vprintfmt (void (*putch) (int, void *), void *putdat,
	const char *fmt, va_list ap)
	__attribute__((__format__ (__printf__, 3, 0)));
int vcprintf (const char *fmt, va_list ap)
	__attribute__((__format__ (__printf__, 1, 0)));
int cprintf (const char *fmt, ...)
	__attribute__((__format__ (__printf__, 1, 2)));
int snprintf(char *str, size_t size, const char *fmt, ...)
	__attribute__((__format__ (__printf__, 3, 4)));
int sprintf(char *str, const char *fmt, ...)
	__attribute__((__format__ (__printf__, 2, 3)));

const char *e2s(int err);
const char *syscall2s(int sys);

void abort (void) __attribute__((__noreturn__));
void _panic (const char *file, int line, const char *fmt, ...)
	__attribute__((__format__ (__printf__, 3, 4)))
	__attribute__((__noreturn__));
#define panic(fmt, varargs...) _panic(__FILE__, __LINE__, fmt, ##varargs)

#define __stringify(s) #s
#define stringify(s) __stringify(s)
#define __FL__ __FILE__ ":" stringify (__LINE__)

#define assert(x)				\
    do {					\
	if (!(x))				\
	    panic("assertion failed:\n%s", #x);	\
    } while (0)

/* Generate a compile-time error if 'e' is false. */
#define static_assert(e) ((void)sizeof(char[1 - 2 * !(e)]))

/* Force a compilation error if condition is false, but also produce a
   result (of value 0 and type size_t), so the expression can be used
   e.g. in a structure initializer (or where-ever else comma expressions
   aren't permitted). */
#define static_assert_zero(e) (sizeof(char[1 - 2 * !(e)]) - 1)

/*
 * Page allocation
 */
extern uint64_t global_npages;
extern uint64_t largest_ram_start;

extern struct page_stats {
    jos_atomic64_t pages_used;
    jos_atomic64_t pages_avail;
    jos_atomic64_t allocations;
    jos_atomic64_t failures;
} page_stats;

void page_alloc_init(void);
int  page_alloc(void **p, proc_id_t pid)
    __attribute__ ((warn_unused_result));
void page_free(void *p);

void page_register_hw(void *v);
int  page_get_hw(physaddr_t pa, void **vp)
     __attribute__ ((warn_unused_result));

/*
 * Memory affinity
 */
void locality_fill(struct u_locality_matrix *ulm);

struct memory_node
{
    uint64_t baseaddr;
    uint64_t length;
    struct {
	uint8_t p : 1;
    } cpu[JOS_NCPU];
};

/*
 * Debug
 */
extern struct spinlock debug_lock;
#define debug(__exp, __fmt, __args...)		        \
    do {					        \
	if (__exp) {				        \
	    spin_lock(&debug_lock);		        \
	    cprintf("%d %s: " __fmt "\n", arch_cpu(),	\
		    __func__, ##__args);		\
	    spin_unlock(&debug_lock);		        \
	}					        \
    } while (0)

#endif /* !JOS_KERN_LIB_H */
