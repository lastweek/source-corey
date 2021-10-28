// So jhttpd can be built in Linux

extern "C" {
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
}

#include <iostream>
#include <cstdio>

using namespace std;

#define lwip_accept accept
#define lwip_bind bind
#define lwip_close close
#define lwip_connect connect
#define lwip_listen listen
#define lwip_socket socket
#define lwip_send send
#define lwip_recv recv

#define cprintf printf

#define panic(x) assert(0)
