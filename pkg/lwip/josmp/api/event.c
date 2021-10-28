#include <inc/lib.h>
#include <inc/syscall.h>
#include <inc/error.h>
#include <lwip/sockets.h>
#include <lwip/opt.h>
#include <api/ext.h>

#include <string.h>
#include <malloc.h>

/* This is for jos64 fast selects across address spaces..
 * may or may not be useful...
 */

extern struct lwip_socket *sockets;

int
lwipext_sync_waiting(int s, char w)
{
    if (s < 0 || s > NUM_SOCKETS)
	return -E_INVAL;

    if (w)
	sockets[s].send_wakeup = 1;
    else
	sockets[s].recv_wakeup = 1;

    return 0;
}

void
lwipext_sync_notify(int s, enum netconn_evt evt)
{
#if 0
    if (evt == NETCONN_EVT_RCVPLUS) {
	if (sockets[s].rcvevent && sockets[s].recv_wakeup)
	    sys_sync_wakeup(&sockets[s].rcvevent);
    } else if (evt == NETCONN_EVT_SENDPLUS) {
	if (sockets[s].sendevent && sockets[s].send_wakeup)
	    sys_sync_wakeup(&sockets[s].sendevent);
    }
#endif
}

void
lwipext_init(char public_sockets)
{
    uint64_t bytes = NUM_SOCKETS * sizeof(struct lwip_socket);
    
    void *va = malloc(bytes);
    if (!va)
	panic("unable to alloc sockets");

    sockets = va;
}
