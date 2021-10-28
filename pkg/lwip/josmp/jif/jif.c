/*
 * Copyright (c) 2001-2004 Swedish Institute of Computer Science.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 *
 */

#include <inc/syscall.h>
#include <inc/lib.h>
#include <inc/memlayout.h>
#include <inc/error.h>
#include <inc/thread.h>
#include <inc/jnic.h>

#include <string.h>

#include <jif/jif.h>

#include "lwip/opt.h"
#include "lwip/def.h"
#include "lwip/mem.h"
#include "lwip/pbuf.h"
#include "lwip/sys.h"
#include <lwip/stats.h>

#include <netif/etharp.h>

struct jif {
    struct jnic *jnic;
    struct eth_addr *ethaddr;
};

static void
low_level_init(struct netif *netif, struct jnic *jnic)
{
    int r;
    struct jif *jif = netif->state;

    netif->hwaddr_len = 6;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST;

    jif->jnic = jnic;

    r = jnic_init(jnic);
    if (r < 0)
	panic("unable to init jnic: %s", e2s(r));

    r = jnic_mac(jnic, &netif->hwaddr[0]);
    if (r < 0)
	panic("unable to get mac: %s", e2s(r));
}

/*
 * low_level_output():
 *
 * Should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 */
static err_t
low_level_output(struct netif *netif, struct pbuf *p)
{
    struct jif *jif = netif->state;
    
#if ETH_PAD_SIZE
    pbuf_header(p, -ETH_PAD_SIZE);			/* drop the padding word */
#endif

    struct netbuf_hdr *nb;
    int r = jnic_txbuf_next(jif->jnic, &nb);
    if (r < 0) {
	cprintf("jif: jnic_txbuf_next failed: %s\n", e2s(r));
	return ERR_MEM;
    }
    
    char *txbuf = (char *) (nb + 1);
    int txsize = 0;

    for (struct pbuf *q = p; q != NULL; q = q->next) {
	/* Send the data from the pbuf to the interface, one pbuf at a
	   time. The size of the data in each pbuf is kept in the ->len
	   variable. */

	if (txsize + q->len > 2000)
	    panic("oversized packet, fragment %d txsize %d\n", q->len, txsize);
	memcpy(&txbuf[txsize], q->payload, q->len);
	txsize += q->len;
    }

    nb->size = txsize;
    nb->actual_count = 0;

    r = jnic_txbuf_done(jif->jnic);
    if (r < 0) {
	cprintf("jif: jnic_txbuf failed: %s\n", e2s(r));
	return ERR_MEM;
    }
       
#if ETH_PAD_SIZE
    pbuf_header(p, ETH_PAD_SIZE);			/* reclaim the padding word */
#endif

#if LINK_STATS
    lwip_stats.link.xmit++;
#endif /* LINK_STATS */

    return ERR_OK;
}

/*
 * low_level_input():
 *
 * Should allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 *
 */
static struct pbuf *
low_level_input(struct netif *netif)
{
    struct jif *jif = netif->state;

    
    struct netbuf_hdr *nb;
    lwip_core_unlock();
    int r = jnic_rxbuf_next(jif->jnic, &nb);
    lwip_core_lock();
    if (r < 0) {
	cprintf("jif: rx packet error: %s\n", e2s(r));
	return 0;
    }

    uint16_t count = nb->actual_count;
    if ((count & NETHDR_COUNT_ERR)) {
	cprintf("jif: rx packet error\n");
	jnic_rxbuf_done(jif->jnic, nb);
	return 0;
    }

    s16_t len = count & NETHDR_COUNT_MASK;

#if ETH_PAD_SIZE
    /* allow room for Ethernet padding */
    len += ETH_PAD_SIZE;
#endif

    /* We allocate a pbuf chain of pbufs from the pool. */
    struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    if (p == 0) {
#if LINK_STATS
	lwip_stats.link.memerr++;
	lwip_stats.link.drop++;
#endif /* LINK_STATS */      
	return 0;
    }

#if ETH_PAD_SIZE
    /* drop the padding word */
    pbuf_header(p, -ETH_PAD_SIZE);
#endif

    /* We iterate over the pbuf chain until we have read the entire
     * packet into the pbuf. */
    void *rxbuf = (void *) (nb + 1);
    int copied = 0;
    for (struct pbuf *q = p; q != NULL; q = q->next) {
	/* Read enough bytes to fill this pbuf in the chain. The
	 * available data in the pbuf is given by the q->len
	 * variable. */
	int bytes = q->len;
	if (bytes > (len - copied))
	    bytes = len - copied;
	memcpy(q->payload, rxbuf + copied, bytes);
	copied += bytes;
    }

#if ETH_PAD_SIZE
    /* reclaim the padding word */
    pbuf_header(p, ETH_PAD_SIZE);
#endif

#if LINK_STATS
    lwip_stats.link.recv++;
#endif /* LINK_STATS */

#if 0
    uint8_t *packet = (uint8_t *)rxbuf;
    if (packet[12] == 0x08 && packet[13] == 0x00) {
	uint8_t *ip = &packet[14];
	if (ip[9] == 0x06) {
	    uint8_t *tcp = &ip[20];
	    uint32_t seq = tcp[4] << 24 | tcp[5] << 16 | tcp[6] << 8 | tcp[7];
	    cprintf("seq %u\n", seq);
	}
    }
#endif

    jnic_rxbuf_done(jif->jnic, nb);
    return p;
}
/*
 * jif_output():
 *
 * This function is called by the TCP/IP stack when an IP packet
 * should be sent. It calls the function called low_level_output() to
 * do the actual transmission of the packet.
 *
 */

static err_t
jif_output(struct netif *netif, struct pbuf *p,
      struct ip_addr *ipaddr)
{
    /* resolve hardware address, then send (or queue) packet */
    return etharp_output(netif, p, ipaddr);
}

/*
 * jif_input():
 *
 * This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface.
 *
 */

void
jif_input(struct netif *netif)
{
    struct jif *jif;
    struct eth_hdr *ethhdr;
    struct pbuf *p;

    jif = netif->state;
  
    /* move received packet into a new pbuf */
    p = low_level_input(netif);

    /* no packet could be read, silently ignore this */
    if (p == NULL) return;
    /* points to packet payload, which starts with an Ethernet header */
    ethhdr = p->payload;

#if LINK_STATS
    lwip_stats.link.recv++;
#endif /* LINK_STATS */

    ethhdr = p->payload;

    switch (htons(ethhdr->type)) {
    case ETHTYPE_IP:
	/* update ARP table */
	etharp_ip_input(netif, p);
	/* skip Ethernet header */
	pbuf_header(p, -(int)sizeof(struct eth_hdr));
	/* pass to network layer */
	netif->input(p, netif);
	break;
      
    case ETHTYPE_ARP:
	/* pass p to ARP module  */
	etharp_arp_input(netif, jif->ethaddr, p);
	break;

    default:
	pbuf_free(p);
    }
}

/*
 * jif_init():
 *
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 */

err_t
jif_init(struct netif *netif)
{
    struct jnic *jnic = netif->state;
    struct jif *jif;

    jif = mem_malloc(sizeof(struct jif));

    if (jif == NULL) {
	LWIP_DEBUGF(NETIF_DEBUG, ("jif_init: out of memory\n"));
	return ERR_MEM;
    }

    netif->state = jif;
    netif->output = jif_output;
    netif->linkoutput = low_level_output;
    memcpy(&netif->name[0], "en", 2);

    jif->ethaddr = (struct eth_addr *)&(netif->hwaddr[0]);

    low_level_init(netif, jnic);

    etharp_init();

    return ERR_OK;
}
