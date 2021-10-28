#include <inc/device.h>
#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/stdio.h>
#include <lwip.h>

#include <lwipinc/lsocket.h>
#include <lwip/sockets.h>
#include <lwip/inet.h>
#include <arch/sys_arch.h>

#include <string.h>

enum { verbose = 0 };

int
lwip_client_test(void)
{
    static const char *request = ""
	"GET / HTTP/1.0\r\nUser-Agent: "
	"TestClient\r\nHost: pdos.csail.mit.edu:80\r\n"
	"\r\n";
    static const char *host = "18.26.4.9";
    static uint16_t host_port = 80;
    
    int r;
    uint16_t port = htons(host_port);
    uint32_t ip = inet_addr(host);
    
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = port;
    sin.sin_addr.s_addr = ip;

    cprintf("Trying to connect to %s:%u\n", host, host_port);
    
    int s = lsocket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
	cprintf("socket error %d\n", s);
	return -1;
    }

    r = lconnect(s, (struct sockaddr *)&sin, sizeof(sin));
    if (r < 0)
	panic("connect error %d", r);

    cprintf("Connected, requesting /\n");

    int sz = strlen(request);
    r = lwrite(s, (char *)request, sz);
    assert(r == sz);

    if (verbose) {
	for (;;) {
	    char buf[128];
	    r = lread(s, buf, sizeof(buf) - 1);
	    if (r == 0)
		break;
	    buf[r] = 0;
	    cprintf("%s", buf);
	}
	cprintf("\n\n");
    }

    cprintf("all done!\n");
    lclose(s);

    return 0;
}
