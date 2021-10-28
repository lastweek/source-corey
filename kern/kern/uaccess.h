#ifndef JOS_KERN_UACCESS_H
#define JOS_KERN_UACCESS_H

int  uaccess_start(void);
void uaccess_stop(void);
void uaccess_error(void) __attribute__((noreturn));
int  uaccess_enabled(void);

#endif
