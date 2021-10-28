/* See COPYRIGHT for copyright information. */

#ifndef JOS_INC_ERROR_H
#define JOS_INC_ERROR_H

// Kernel error codes -- keep in sync with list in lib/sysstring.c.
enum {
    E_UNSPEC = 1,	// Unspecified or unknown problem
    E_INVAL,		// Invalid parameter
    E_NO_MEM,		// Request failed due to memory shortage
    E_RESTART,		// Restart system call
    E_NOT_FOUND,	// Object not found
    E_PERM,		// permission check error
    E_BUSY,		// device busy
    E_NO_SPACE,		// not enough space in buffer
    E_AGAIN,		// try again
    E_IO,		// disk IO error
    E_BAD_TYPE,		// bad object type
    
    // User-level error codes
    E_EXISTS,		// object exists
    E_RANGE,		// value of of range

    E_MAXERROR
};

#endif // _ERROR_H_
