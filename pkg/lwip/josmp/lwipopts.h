#ifndef JOS_LWIP_LWIPOPTS_H
#define JOS_LWIP_LWIPOPTS_H

#define LWIP_STATS		0
#define LWIP_STATS_DISPLAY	0
#define LWIP_DHCP		1
#define LWIP_COMPAT_SOCKETS	0
#define SYS_LIGHTWEIGHT_PROT	1

// Various tuning knobs, see:
// http://lists.gnu.org/archive/html/lwip-users/2006-11/msg00007.html

#if defined(JOS_ARCH_sparc)
#define MEM_ALIGNMENT		8
#define ETH_PAD_SIZE		2
#else
#define MEM_ALIGNMENT		4
#endif

#define MEMP_NUM_PBUF		64
#define MEMP_NUM_UDP_PCB	8
#define MEMP_NUM_TCP_PCB	32
#define MEMP_NUM_TCP_PCB_LISTEN	16
#define MEMP_NUM_TCP_SEG	TCP_SND_QUEUELEN// at least as big as TCP_SND_QUEUELEN
#define MEMP_NUM_NETBUF		128
#define MEMP_NUM_NETCONN	32
#define MEMP_NUM_SYS_TIMEOUT    6

#define PER_TCP_PCB_BUFFER	(16 * 4096)
#define MEM_SIZE		(PER_TCP_PCB_BUFFER*MEMP_NUM_TCP_SEG + 4096*MEMP_NUM_TCP_SEG)

#define PBUF_POOL_SIZE		512
#define PBUF_POOL_BUFSIZE	2000

#define TCP_MSS			1460
#define TCP_WND			24000
#define TCP_SND_BUF		(16 * TCP_MSS)
// lwip prints a warning if TCP_SND_QUEUELEN < (2 * TCP_SND_BUF/TCP_MSS), 
// but 16 is faster.. 
#define TCP_SND_QUEUELEN	(2 * TCP_SND_BUF/TCP_MSS)
//#define TCP_SND_QUEUELEN	16

// Print error messages when we run out of memory
#define LWIP_DEBUG	1
//#define TCP_DEBUG	LWIP_DBG_ON
//#define MEMP_DEBUG	LWIP_DBG_ON
//#define SOCKETS_DEBUG	LWIP_DBG_ON
#define DBG_TYPES_ON	LWIP_DBG_ON
#define DBG_MIN_LEVEL	DBG_LEVEL_SERIOUS
//#define LWIP_DBG_MIN_LEVEL	0
#define MEMP_SANITY_CHECK	0

#define ERRNO

#endif
