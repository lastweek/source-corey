#include <sys/socket.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>

#include <linux/if_packet.h>
#include <linux/if.h>
#include <linux/filter.h>
#include <sys/ioctl.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

static unsigned char src_addr[6];
//static unsigned char dst_addr[6] = { 0x00, 0x30, 0x48, 0x5F, 0x28, 0xAE };
static unsigned char dst_addr[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static int packet_sizes[] = { 74, 54, 54, 54 };

static void __attribute__((noreturn))
pexit(const char *s)
{
    perror(s);
    exit(EXIT_FAILURE);
}

static void __attribute__((noreturn))
pack_gen(int s)
{
    unsigned char *buf = malloc(4096);
    memset(buf, 0, 4096);

    memcpy(buf, dst_addr, 6);
    memcpy(&buf[6], src_addr, 6);
    
    int mod = sizeof(packet_sizes) / sizeof(packet_sizes[0]);

    for (int i = 0; ; i++) {
	int r = write(s, buf, packet_sizes[i % mod]);
	if (r < 0)
	    perror("write");
    }
}

int
main(int ac, char **av)
{
    if (ac < 2) {
	printf("usage: %s iface\n", av[0]);
	return -1;
    }

    const char *iface = av[1];

    int s = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (s < 0)
	pexit("socket");

    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ);
    if (ioctl(s, SIOCGIFINDEX, &ifr) < 0)
	pexit("ioctl");

    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = ifr.ifr_ifindex;
    sll.sll_protocol = htons(ETH_P_ALL);
    if (bind(s, (struct sockaddr *) &sll, sizeof(sll)) < 0)
	pexit("bind");
	
    strncpy(ifr.ifr_name, iface, IFNAMSIZ);
    if (ioctl(s, SIOCGIFHWADDR, &ifr) < 0)
	pexit("ioctl");

    memcpy(src_addr, ifr.ifr_ifru.ifru_hwaddr.sa_data, 6);
    
    printf("using src_addr: %02X:%02X:%02X:%02X:%02X:%02X\n",
	   src_addr[0], src_addr[1], src_addr[2], 
	   src_addr[3], src_addr[4], src_addr[5]);
    printf("using dst_addr: %02X:%02X:%02X:%02X:%02X:%02X\n",
	   dst_addr[0], dst_addr[1], dst_addr[2], 
	   dst_addr[3], dst_addr[4], dst_addr[5]);

    pack_gen(s);
    return 0;
}
