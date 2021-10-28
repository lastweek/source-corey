#ifndef JOS_INC_STACKTRACE_H
#define JOS_INC_STACKTRACE_H

/*
 * You probably want to use print_backtrace(), see inc/lib.h.
 *
 * This does not necessarily print a backtrace.  Some ABIs (like AMD64)
 * do not require the frame pointer to be stored on the stack.  
 * print_stacktrace() attempts to locate all call frames using the 
 * frame pointer.  It sould only be used for very low-level debugging 
 * (like ummap.c).
 */

void print_stacktrace(void);

#endif
