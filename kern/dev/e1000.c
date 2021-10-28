#include <kern/nic.h>
#include <kern/intr.h>
#include <kern/arch.h>
#include <kern/lib.h>
#include <kern/timer.h>
#include <kern/segment.h>
#include <dev/pci.h>
#include <dev/e1000.h>
#include <dev/e1000reg.h>
#include <dev/ioapic.h>
#include <dev/vnic.h>
#include <inc/error.h>
#include <inc/spinlock.h>
#include <inc/mcs.h>

enum { print_mac = 1 };
enum { overrun_reset = 0 };

// Number virtual nics
#define E1000_VNIC	0

// Number of statically allocated hardware and software descriptor buffers
#define E1000_BUFFERS	8

#define E1000_RX_SLOTS	1024
#define E1000_TX_SLOTS	1024

// E1000_ITR = (256 * 10^-9 * interrupts per/sec)^-1
// "optimal performance setting for this register is very system and 
//  configuration specific.  A initial suggested range is 0x28B-15CC."
#define E1000_ITR      0

// What type of locks
#if 0
typedef struct spinlock e1000mu_t;
#define e1000mu_lock spin_lock
#define e1000mu_unlock spin_unlock
#else
typedef struct mcslock e1000mu_t;
#define e1000mu_lock mcs_lock
#define e1000mu_unlock mcs_unlock
#endif

// List of e1000 card, so we can correctly configure dual-ported MACs
static LIST_HEAD(e1000_list, e1000_card) 
     card_list = LIST_HEAD_INITIALIZER(card_list);

struct e1000_buffer_slot {
    struct netbuf_hdr *nb;
    uint64_t sg_id;
    uint64_t offset;
};

static struct e1000_buffer_slot e1000_buffer_slot_rx[E1000_BUFFERS][E1000_RX_SLOTS];
static struct e1000_buffer_slot e1000_buffer_slot_tx[E1000_BUFFERS][E1000_TX_SLOTS];

// Static allocation ensures contiguous memory.
struct e1000_tx_descs {
    struct wiseman_txdesc txd[E1000_TX_SLOTS] __attribute__((aligned (16)));
};

struct e1000_rx_descs {
    struct wiseman_rxdesc rxd[E1000_RX_SLOTS] __attribute__((aligned (16)));
};

static struct e1000_tx_descs e1000_tx_descs_static[E1000_BUFFERS];
static struct e1000_rx_descs e1000_rx_descs_static[E1000_BUFFERS];

struct e1000_card {
    struct nic nic;

    uint32_t membase;
    uint32_t iobase;
    uint16_t pci_dev_id;
    struct interrupt_handler ih;

    struct e1000_tx_descs *txds;
    struct e1000_rx_descs *rxds;

    uint32_t icr;

    struct e1000_buffer_slot *tx;
    struct e1000_buffer_slot *rx;

    int rx_head;	// card receiving into rx_head, -1 if none
    int rx_nextq;	// next slot for rx buffer

    int tx_head;	// card transmitting from tx_head, -1 if none
    int tx_nextq;	// next slot for tx buffer
    
    LIST_ENTRY(e1000_card) list_link;

    uint8_t filter[WM_RAL_TABSIZE][6];
    
    e1000mu_t tx_lock;
    e1000mu_t rx_lock;
};

static uint32_t
e1000_io_read(struct e1000_card *c, uint32_t reg)
{
    physaddr_t pa = c->membase + reg;
    volatile uint32_t *ptr = pa2kva(pa);
    return *ptr;
}

static void
e1000_io_write(struct e1000_card *c, uint32_t reg, uint32_t val)
{
    physaddr_t pa = c->membase + reg;
    volatile uint32_t *ptr = pa2kva(pa);
    *ptr = val;
}

static void
e1000_io_write_flush(struct e1000_card *c, uint32_t reg, uint32_t val)
{
    e1000_io_write(c, reg, val);
    e1000_io_read(c, WMREG_STATUS);
}

static void
e1000_eeprom_uwire_out(struct e1000_card *c, uint16_t data, uint16_t count)
{
    uint32_t mask = 1 << (count - 1);
    uint32_t eecd = e1000_io_read(c, WMREG_EECD) & ~(EECD_DO | EECD_SK);

    do {
	if (data & mask)
	    eecd |= EECD_DI;
	else
	    eecd &= ~(EECD_DI);

	e1000_io_write_flush(c, WMREG_EECD, eecd);
	timer_delay(50000);

	e1000_io_write_flush(c, WMREG_EECD, eecd | EECD_SK);
	timer_delay(50000);

	e1000_io_write_flush(c, WMREG_EECD, eecd);
	timer_delay(50000);

	mask = mask >> 1;
    } while (mask);

    e1000_io_write_flush(c, WMREG_EECD, eecd & ~(EECD_DI));
}

static uint16_t
e1000_eeprom_uwire_in(struct e1000_card *c, uint16_t count)
{
    uint32_t data = 0;
    uint32_t eecd = e1000_io_read(c, WMREG_EECD) & ~(EECD_DO | EECD_DI);

    for (uint16_t i = 0; i < count; i++) {
	data = data << 1;

	e1000_io_write_flush(c, WMREG_EECD, eecd | EECD_SK);
	timer_delay(50000);

	eecd = e1000_io_read(c, WMREG_EECD) & ~(EECD_DI);
	if (eecd & EECD_DO)
	    data |= 1;

	e1000_io_write_flush(c, WMREG_EECD, eecd & ~EECD_SK);
	timer_delay(50000);
    }

    return data;
}

static int32_t
e1000_eeprom_uwire_read(struct e1000_card *c, uint16_t off)
{
    /* Make sure this is microwire */
    uint32_t eecd = e1000_io_read(c, WMREG_EECD);
    if (eecd & EECD_EE_TYPE) {
	cprintf("e1000_eeprom_read: EERD timeout, SPI not supported\n");
	return -1;
    }

    uint32_t abits = (eecd & EECD_EE_SIZE) ? 8 : 6;

    /* Get access to the EEPROM */
    eecd |= EECD_EE_REQ;
    e1000_io_write_flush(c, WMREG_EECD, eecd);
    for (uint32_t t = 0; t < 100; t++) {
	timer_delay(50000);
	eecd = e1000_io_read(c, WMREG_EECD);
	if (eecd & EECD_EE_GNT)
	    break;
    }

    if (!(eecd & EECD_EE_GNT)) {
	cprintf("e1000_eeprom_read: cannot get EEPROM access\n");
	e1000_io_write_flush(c, WMREG_EECD, eecd & ~EECD_EE_REQ);
	return -1;
    }

    /* Turn on the EEPROM */
    eecd &= ~(EECD_DI | EECD_SK);
    e1000_io_write_flush(c, WMREG_EECD, eecd);

    eecd |= EECD_CS;
    e1000_io_write_flush(c, WMREG_EECD, eecd);

    /* Read the bits */
    e1000_eeprom_uwire_out(c, UWIRE_OPC_READ, 3);
    e1000_eeprom_uwire_out(c, off, abits);
    uint16_t v = e1000_eeprom_uwire_in(c, 16);

    /* Turn off the EEPROM */
    eecd &= ~(EECD_CS | EECD_DI | EECD_SK);
    e1000_io_write_flush(c, WMREG_EECD, eecd);

    e1000_io_write_flush(c, WMREG_EECD, eecd | EECD_SK);
    timer_delay(50000);

    e1000_io_write_flush(c, WMREG_EECD, eecd & ~EECD_EE_REQ);
    timer_delay(50000);

    return v;
}

static int32_t
e1000_eeprom_eerd_read(struct e1000_card *c, uint16_t off)
{
    e1000_io_write(c, WMREG_EERD, (off << EERD_ADDR_SHIFT) | EERD_START);

    uint32_t reg;
    for (int x = 0; x < 100; x++) {
	reg = e1000_io_read(c, WMREG_EERD);
	if (!(reg & EERD_DONE))
	    timer_delay(5000);
    }

    if (reg & EERD_DONE)
	return (reg & EERD_DATA_MASK) >> EERD_DATA_SHIFT;
    return -1;
}

static int
e1000_eeprom_read(struct e1000_card *c, uint16_t *buf, int off, int count)
{
    for (int i = 0; i < count; i++) {
	int32_t r = e1000_eeprom_eerd_read(c, off + i);
	if (r < 0)
	    r = e1000_eeprom_uwire_read(c, off + i);

	if (r < 0) {
	    cprintf("e1000_eeprom_read: cannot read\n");
	    return -1;
	}

	buf[i] = r;
    }

    return 0;
}

static void __attribute__((unused))
e1000_dump_stats(struct e1000_card *c)
{
    for (uint32_t i = 0; i <= 0x124; i += 4) {
	uint32_t v = e1000_io_read(c, 0x4000 + i);
	if (v != 0)
	    cprintf("%x:%x ", i, v);
    }
    cprintf("\n");
}

static void
e1000_reset(struct e1000_card *c)
{
    e1000_io_write(c, WMREG_RCTL, 0);
    e1000_io_write(c, WMREG_TCTL, 0);

    // Allocate the card's packet buffer memory equally between rx, tx
    uint32_t pba = e1000_io_read(c, WMREG_PBA);
    uint32_t rxtx = ((pba >> PBA_RX_SHIFT) & PBA_RX_MASK) +
		    ((pba >> PBA_TX_SHIFT) & PBA_TX_MASK);
    e1000_io_write(c, WMREG_PBA, rxtx / 2);

    // Reset PHY, card
    uint32_t ctrl = e1000_io_read(c, WMREG_CTRL);
    e1000_io_write(c, WMREG_CTRL, ctrl | CTRL_PHY_RESET);
    timer_delay(5 * 1000 * 1000);

    e1000_io_write(c, WMREG_CTRL, ctrl | CTRL_RST);
    timer_delay(10 * 1000 * 1000);

    for (int i = 0; i < 1000; i++) {
	if ((e1000_io_read(c, WMREG_CTRL) & CTRL_RST) == 0)
	    break;
	timer_delay(20000);
    }

    if (e1000_io_read(c, WMREG_CTRL) & CTRL_RST)
	cprintf("e1000_reset: card still resetting, odd..\n");

    e1000_io_write(c, WMREG_CTRL, ctrl | CTRL_SLU | CTRL_ASDE);

    // Make sure the management hardware is not hiding any packets
    if (c->pci_dev_id == 0x108c || c->pci_dev_id == 0x109a) {
	uint32_t manc = e1000_io_read(c, WMREG_MANC);
	manc &= ~MANC_ARP_REQ;
	manc |= MANC_MNG2HOST;

	e1000_io_write(c, WMREG_MANC2H, MANC2H_PORT_623 | MANC2H_PORT_664);
	e1000_io_write(c, WMREG_MANC, manc);
    }

    // Setup RX, TX rings
    uint64_t rptr = kva2pa(&c->rxds->rxd[0]);
    e1000_io_write(c, WMREG_RDBAH, rptr >> 32);
    e1000_io_write(c, WMREG_RDBAL, rptr & 0xffffffff);
    e1000_io_write(c, WMREG_RDLEN, sizeof(c->rxds->rxd));
    e1000_io_write(c, WMREG_RDH, 0);
    e1000_io_write(c, WMREG_RDT, 0);
    e1000_io_write(c, WMREG_RDTR, 0);
    e1000_io_write(c, WMREG_RADV, 0);

    uint64_t tptr = kva2pa(&c->txds->txd[0]);
    e1000_io_write(c, WMREG_TDBAH, tptr >> 32);
    e1000_io_write(c, WMREG_TDBAL, tptr & 0xffffffff);
    e1000_io_write(c, WMREG_TDLEN, sizeof(c->txds->txd));
    e1000_io_write(c, WMREG_TDH, 0);
    e1000_io_write(c, WMREG_TDT, 0);
    e1000_io_write(c, WMREG_TIDV, 1);
    e1000_io_write(c, WMREG_TADV, 1);

    // Disable VLAN
    e1000_io_write(c, WMREG_VET, 0);

    // Flow control junk?
    e1000_io_write(c, WMREG_FCAL, FCAL_CONST);
    e1000_io_write(c, WMREG_FCAH, FCAH_CONST);
    e1000_io_write(c, WMREG_FCT, 0x8808);
    e1000_io_write(c, WMREG_FCRTH, FCRTH_DFLT);
    e1000_io_write(c, WMREG_FCRTL, FCRTL_DFLT);
    e1000_io_write(c, WMREG_FCTTV, FCTTV_DFLT);

    // Interrupts
    e1000_io_write(c, WMREG_IMC, ~0);
    e1000_io_write(c, WMREG_IMS, ICR_TXDW | ICR_RXO | ICR_RXT0);
    c->icr |= ICR_TXDW | ICR_RXO | ICR_RXT0;
    // Interrupt throttle
    if (E1000_ITR)
	e1000_io_write(c, WMREG_ITR, (E1000_ITR & ITR_IVAL_MASK));    

    // MAC address filters
    for (int i = 0; i < WM_RAL_TABSIZE; i++) {
	e1000_io_write(c, WMREG_CORDOVA_RAL_BASE + (i * 8),
		       (c->filter[i][0]) |
		       (c->filter[i][1] << 8) |
		       (c->filter[i][2] << 16) |
		       (c->filter[i][3] << 24));
	e1000_io_write(c, WMREG_CORDOVA_RAL_BASE + 4 + (i * 8),
		       (c->filter[i][4]) |
		       (c->filter[i][5] << 8) | RAL_AV);
    }

    for (int i = 0; i < WM_MC_TABSIZE; i++)
	e1000_io_write(c, WMREG_CORDOVA_MTA + i * 4, 0);

    // Enable RX, TX
    e1000_io_write(c, WMREG_RCTL,
		   RCTL_EN | RCTL_RDMTS_1_2 | RCTL_DPF | RCTL_BAM | RCTL_2k);
    e1000_io_write(c, WMREG_TCTL,
		   TCTL_EN | TCTL_PSP | TCTL_CT(TX_COLLISION_THRESHOLD) |
		   TCTL_COLD(TX_COLLISION_DISTANCE_FDX));

    for (int i = 0; i < E1000_TX_SLOTS; i++) {
	if (c->tx[i].nb) {
	    c->tx[i].nb->actual_count |= NETHDR_COUNT_RESET;
	    pagetree_decref_hw(c->tx[i].nb);
	}
	c->tx[i].nb = 0;
    }

    for (int i = 0; i < E1000_RX_SLOTS; i++) {
	if (c->rx[i].nb) {
	    c->rx[i].nb->actual_count |= NETHDR_COUNT_RESET;
	    pagetree_decref_hw(c->rx[i].nb);
	}
	c->rx[i].nb = 0;
    }

    c->rx_head = -1;
    c->rx_nextq = 0;

    c->tx_head = -1;
    c->tx_nextq = 0;
}

static void
e1000_buffer_reset(void *a)
{
    e1000_reset(a);
}

static void
e1000_intr_rx(struct e1000_card *c)
{
    e1000mu_lock(&c->rx_lock);
    for (;;) {
	int i = c->rx_head;
	if (i == -1 || !(c->rxds->rxd[i].wrx_status & WRX_ST_DD))
	    break;

	if (c->rxds->rxd[i].wrx_errors)
	    c->rx[i].nb->actual_count |= NETHDR_COUNT_ERR;

	uint16_t count = c->rxds->rxd[i].wrx_len;
	c->rx[i].nb->actual_count = count;
	c->rx[i].nb->actual_count |= NETHDR_COUNT_DONE;

	if (c->nic.nc_intr_rx_cb)
	    c->nic.nc_intr_rx_cb(c->nic.nc_cb_arg, c->rx[i].sg_id, c->rx[i].offset,
				 c->rx[i].nb, count);
	
	pagetree_decref_hw(c->rx[i].nb);
	c->rx[i].nb = 0;
	
	c->rx_head = (i + 1) % E1000_RX_SLOTS;
	if (c->rx_head == c->rx_nextq)
	    c->rx_head = -1;
    }
    e1000mu_unlock(&c->rx_lock);
}

static void
e1000_intr_tx(struct e1000_card *c)
{
    e1000mu_lock(&c->tx_lock);
    for (;;) {
	int i = c->tx_head;
	if (i == -1 || !(c->txds->txd[i].wtx_fields.wtxu_status & WTX_ST_DD))
	    break;

	c->tx[i].nb->actual_count |= NETHDR_COUNT_DONE;
	pagetree_decref_hw(c->tx[i].nb);
	c->tx[i].nb = 0;
	
	c->tx_head = (i + 1) % E1000_TX_SLOTS;
	if (c->tx_head == c->tx_nextq)
	    c->tx_head = -1;
    }
    e1000mu_unlock(&c->tx_lock);
}

static void
e1000_intr(struct Processor *ps, void *arg)
{
    struct e1000_card *c = arg;
    uint32_t icr = e1000_io_read(c, WMREG_ICR);

    // NetBSD implements this loop.
    while (icr & c->icr) {
	if (icr & ICR_TXDW)
	    e1000_intr_tx(c);
	
	if (icr & ICR_RXT0)
	    e1000_intr_rx(c);

	if ((icr & ICR_RXO) && overrun_reset) {
	    cprintf("e1000_intr: receiver overrun\n");
	    e1000_reset(c);
	}

	icr = e1000_io_read(c, WMREG_ICR);
    }
}

static void
e1000_poll(void *a)
{
    struct e1000_card *c = a;
    // Avoid polling the MMIO registers because it congests the PCI bus.
    e1000_intr_tx(c);
    e1000_intr_rx(c);
}

static int 
e1000_add_txbuf(void *a, uint64_t sg_id, uint64_t offset,
		struct netbuf_hdr *nb, uint16_t size)
{
    struct e1000_card *c = a;
    e1000mu_lock(&c->tx_lock);
    int slot = c->tx_nextq;

    if (slot == c->tx_head) {
	e1000mu_unlock(&c->tx_lock);
	return -E_NO_SPACE;
    }

    if (size > 1522) {
	cprintf("e1000_add_txbuf: oversize buffer, %d bytes\n", size);
	e1000mu_unlock(&c->tx_lock);
	return -E_INVAL;
    }

    c->tx[slot].nb = nb;
    c->tx[slot].sg_id = sg_id;
    c->tx[slot].offset = offset;
    pagetree_incref_hw(nb);

    c->txds->txd[slot].wtx_addr = kva2pa(c->tx[slot].nb + 1);
    c->txds->txd[slot].wtx_cmdlen = size | WTX_CMD_RS | WTX_CMD_EOP | WTX_CMD_IFCS;
    memset(&c->txds->txd[slot].wtx_fields, 0, sizeof(&c->txds->txd[slot].wtx_fields));

    c->tx_nextq = (slot + 1) % E1000_TX_SLOTS;
    if (c->tx_head == -1)
	c->tx_head = slot;

    e1000_io_write(c, WMREG_TDT, c->tx_nextq);
    e1000mu_unlock(&c->tx_lock);
    return 0;
}

static void __attribute__((unused))
e1000_tx_test(struct e1000_card *c)
{
    uint32_t packet_sizes[] = { 60, 182, 60, 60 };
    //uint32_t packet_sizes[] = { 74, 54, 54, 54 };
    
    uint64_t tx_size = 0;
    for (int i = 0; i < E1000_TX_SLOTS - 1; i++) {
	uint32_t x = i % array_size(packet_sizes);
	tx_size += packet_sizes[x];
    }

    struct Segment *sg;
    assert(segment_alloc(&sg, 0) == 0);
    assert(segment_set_nbytes(sg, E1000_TX_SLOTS * PGSIZE) == 0);

    for (int i = 0; i < E1000_TX_SLOTS; i++) {
	void *pg;
	assert(segment_get_page(sg, i, &pg, page_shared_cow) == 0);
	uint8_t *buf = pg;
	uint8_t dst_addr[6] = { 0x00, 0x30, 0x48, 0x5F, 0x28, 0xAE };
	uint8_t src_addr[6] = { 0xDE, 0xAD, 0xBE, 0xEE, 0xEE, 0xFF };
	memcpy(buf, dst_addr, 6);
	memcpy(&buf[6], src_addr, 6);
    }
    
    for (int k = 0; ; k++) {
	//cprintf("e1000_tx_test: waiting to TX ring buffer...\n");
	uint64_t s = read_tsc();
	for (int i = 0; i < E1000_TX_SLOTS; i++) {
	    uint32_t x = i % array_size(packet_sizes);
	    uint32_t packet_size = packet_sizes[x];

	    void *pg;
	    assert(segment_get_page(sg, i, &pg, page_shared_cow) == 0);
	    c->txds->txd[i].wtx_addr = kva2pa(pg);
	    c->txds->txd[i].wtx_cmdlen = packet_size | WTX_CMD_RS | WTX_CMD_EOP | WTX_CMD_IFCS;
	    memset(&c->txds->txd[i].wtx_fields, 0, sizeof(&c->txds->txd[i].wtx_fields));
	    if (i != E1000_TX_SLOTS - 1)
		e1000_io_write(c, WMREG_TDT, i + 1);
	}
	
	//e1000_io_write(c, WMREG_TDT, E1000_TX_SLOTS - 1);
	
	volatile uint8_t *status = &c->txds->txd[E1000_TX_SLOTS - 2].wtx_fields.wtxu_status;
	while (!(*status & WTX_ST_DD))
	    ;
	uint64_t e = read_tsc();

	//uint64_t per = (tx_size * 1000000000UL) / ((e - s) / 2);
	uint64_t pack_per = ((E1000_TX_SLOTS - 1) * 1000000000UL) / ((e - s) / 2);

	//cprintf("all done:\n");
	//cprintf(" txed %lu bytes, %ld cycles, %ld bytes/sec, %ld Mbits/sec\n", 
	//tx_size, e - s, per, (per * 8)/(1000000));
	cprintf(" txed %u packets, %ld packets/sec\n", E1000_TX_SLOTS - 1, pack_per);

	e1000_io_write(c, WMREG_TDT, 0);
	status = &c->txds->txd[E1000_TX_SLOTS - 1].wtx_fields.wtxu_status;
	while (!(*status & WTX_ST_DD))
	    ;
    }
}

static void __attribute__((unused))
e1000_rx_test(struct e1000_card *c)
{
    struct Segment *sg;
    assert(segment_alloc(&sg, 0) == 0);
    assert(segment_set_nbytes(sg, E1000_RX_SLOTS * PGSIZE) == 0);
    
    for (int k = 0; ; k++) {
	//cprintf("e1000_rx_test: waiting to RX ring buffer...\n");
	uint64_t s = read_tsc();
	for (int i = 0; i < E1000_RX_SLOTS; i++) {
	    void *pg;
	    assert(segment_get_page(sg, i, &pg, page_shared_cow) == 0);

	    memset(&c->rxds->rxd[i], 0, sizeof(c->rxds->rxd[i]));
	    c->rxds->rxd[i].wrx_addr = kva2pa(pg);

	    if (i != E1000_RX_SLOTS - 1)
		e1000_io_write(c, WMREG_RDT, i + 1);
	}
	
	volatile uint8_t *status = &c->rxds->rxd[E1000_RX_SLOTS - 2].wrx_status;
	//volatile uint8_t *status = &c->rxds->rxd[0].wrx_status;
	while (!(*status & WRX_ST_DD))
	    ;
	uint64_t e = read_tsc();

	uint64_t pack_per = ((E1000_RX_SLOTS - 1) * 1000000000UL) / ((e - s) / 2);

	//cprintf("all done:\n");
	cprintf(" rxed %u packets, %ld packets/sec\n", E1000_RX_SLOTS - 1, pack_per);

	e1000_io_write(c, WMREG_RDT, 0);
	status = &c->rxds->rxd[E1000_RX_SLOTS - 1].wrx_status;
	while (!(*status & WRX_ST_DD))
	    ;
    }
}

static void __attribute__((unused))
e1000_rx_tx_test(struct e1000_card *c)
{
    uint32_t packet_sizes[] = { 60, 182, 60, 60 };
    //uint32_t packet_sizes[] = { 74, 54, 54, 54 };
    
    uint64_t tx_size = 0;
    for (int i = 0; i < E1000_TX_SLOTS - 1; i++) {
	uint32_t x = i % array_size(packet_sizes);
	tx_size += packet_sizes[x];
    }

    struct Segment *tx_sg;
    assert(segment_alloc(&tx_sg, 0) == 0);
    assert(segment_set_nbytes(tx_sg, E1000_TX_SLOTS * PGSIZE) == 0);

    struct Segment *rx_sg;
    assert(segment_alloc(&rx_sg, 0) == 0);
    assert(segment_set_nbytes(rx_sg, E1000_TX_SLOTS * PGSIZE) == 0);

    for (int i = 0; i < E1000_TX_SLOTS; i++) {
	void *pg;
	assert(segment_get_page(tx_sg, i, &pg, page_shared_cow) == 0);
	uint8_t *buf = pg;
	//uint8_t dst_addr[6] = { 0x00, 0x30, 0x48, 0x5F, 0x28, 0xAE };
	uint8_t dst_addr[6] = { 0x00, 0x00, 0x48, 0x5F, 0x28, 0xAE };
	uint8_t src_addr[6] = { 0xDE, 0xAD, 0xBE, 0xEE, 0xEE, 0xFF };
	memcpy(buf, dst_addr, 6);
	memcpy(&buf[6], src_addr, 6);
    }
    
    for (int k = 0; ; k++) {
	uint64_t s = read_tsc();
	for (int i = 0; i < E1000_TX_SLOTS; i++) {
	    uint32_t x = i % array_size(packet_sizes);
	    uint32_t packet_size = packet_sizes[x];

	    // setup TX descriptor
	    void *pg;
	    assert(segment_get_page(tx_sg, i, &pg, page_shared_cow) == 0);
	    c->txds->txd[i].wtx_addr = kva2pa(pg);
	    c->txds->txd[i].wtx_cmdlen = packet_size | WTX_CMD_RS | WTX_CMD_EOP | WTX_CMD_IFCS;
	    memset(&c->txds->txd[i].wtx_fields, 0, sizeof(&c->txds->txd[i].wtx_fields));
	    if (i != E1000_TX_SLOTS - 1)
		e1000_io_write(c, WMREG_TDT, i + 1);

	    // setup RX descriptor
	    assert(segment_get_page(rx_sg, i, &pg, page_shared_cow) == 0);
	    memset(&c->rxds->rxd[i], 0, sizeof(c->rxds->rxd[i]));
	    c->rxds->rxd[i].wrx_addr = kva2pa(pg);

	    if (i != E1000_RX_SLOTS - 1)
		e1000_io_write(c, WMREG_RDT, i + 1);
	}
		
	// wait for all RX to complete
	volatile uint8_t *status = &c->rxds->rxd[E1000_RX_SLOTS - 2].wrx_status;
	while (!(*status & WRX_ST_DD))
	    ;

	// wait for all TX to complete
	status = &c->txds->txd[E1000_TX_SLOTS - 2].wtx_fields.wtxu_status;
	while (!(*status & WTX_ST_DD))
	    ;
	uint64_t e = read_tsc();
	uint64_t pack_per = ((E1000_TX_SLOTS - 1) * 1000000000UL) / ((e - s) / 2);
	cprintf(" txed %u packets, %ld packets/sec\n", E1000_TX_SLOTS - 1, pack_per);

	// wait for final RX
	e1000_io_write(c, WMREG_RDT, 0);
	status = &c->rxds->rxd[E1000_RX_SLOTS - 1].wrx_status;
	while (!(*status & WRX_ST_DD))
	    ;

	// wait for final TX
	e1000_io_write(c, WMREG_TDT, 0);
	status = &c->txds->txd[E1000_TX_SLOTS - 1].wtx_fields.wtxu_status;
	while (!(*status & WTX_ST_DD))
	    ;
    }
}

static int 
e1000_add_rxbuf(void *a, uint64_t sg_id, uint64_t offset,
		struct netbuf_hdr *nb, uint16_t size)
{
    struct e1000_card *c = a;
    e1000mu_lock(&c->rx_lock);
    int slot = c->rx_nextq;

    if (slot == c->rx_head) {
	e1000mu_unlock(&c->rx_lock);
	return -E_NO_SPACE;
    }

    // The receive buffer size is hard-coded in the RCTL register as 2K.
    // However, we configure it to reject packets over 1522 bytes long.
    if (size < 1522) {
	cprintf("e1000_add_rxbuf: buffer too small, %d bytes\n", size);
	e1000mu_unlock(&c->rx_lock);
	return -E_INVAL;
    }

    c->rx[slot].nb = nb;
    c->rx[slot].sg_id = sg_id;
    c->rx[slot].offset = offset;
    pagetree_incref_hw(nb);

    memset(&c->rxds->rxd[slot], 0, sizeof(c->rxds->rxd[slot]));
    c->rxds->rxd[slot].wrx_addr = kva2pa(c->rx[slot].nb + 1);
    e1000_io_write(c, WMREG_RDT, slot);

    c->rx_nextq = (slot + 1) % E1000_RX_SLOTS;
    if (c->rx_head == -1)
	c->rx_head = slot;

    e1000mu_unlock(&c->rx_lock);
    return 0;
}

static int
e1000_mac_index(void *arg, uint8_t *mac_addr)
{
    struct e1000_card *c = arg;
    uint8_t r = mac_addr[4] - c->filter[0][4];
    if (!memcmp(mac_addr, c->filter[r], 6))
	return r;
    return -E_INVAL;
}

static void
e1000_index_mac(void *arg, int index, uint8_t *mac_addr)
{
    assert(index < E1000_VNIC);
    struct e1000_card *c = arg;
    memcpy(mac_addr, c->filter[index], 6);
}

static void
e1000_shutdown(void *arg)
{
    struct e1000_card *c = arg;
    e1000_io_write(c, WMREG_RCTL, 0);
    e1000_io_write(c, WMREG_TCTL, 0);
}

int
e1000_attach(struct pci_func *pcif)
{
    static int e1000_count;

    if (e1000_count >= E1000_BUFFERS)
	panic("not enough E1000_BUFFERS");

    if (e1000_count) {
	cprintf("e1000_attach: ignoring e1000\n");
	return 0;
    }

    struct e1000_card *c;
    int r = page_alloc((void **) &c, 0);
    if (r < 0)
	return r;

    memset(c, 0, sizeof(*c));
    static_assert(PGSIZE >= sizeof(*c));
    static_assert(E1000_VNIC <= WM_RAL_TABSIZE);

    c->txds = &e1000_tx_descs_static[e1000_count];
    c->rxds = &e1000_rx_descs_static[e1000_count];
    
    c->rx = e1000_buffer_slot_rx[e1000_count];
    c->tx = e1000_buffer_slot_tx[e1000_count];
    
    pci_func_enable(pcif);
    c->nic.nc_irq_line = pcif->irq_line;
    c->membase = pcif->reg_base[0];
    c->iobase = pcif->reg_base[2];
    c->pci_dev_id = pcif->dev_id;

    // Get the MAC address
    uint16_t myaddr[3];
    r = e1000_eeprom_read(c, &myaddr[0], EEPROM_OFF_MACADDR, 3);
    if (r < 0) {
	cprintf("e1000_attach: cannot read EEPROM MAC addr: %s\n", e2s(r));
	return 0;
    }

    // See if we are dual-ported
    struct e1000_card *other;
    LIST_FOREACH(other, &card_list, list_link) {
	if (memcmp(other->nic.nc_hwaddr, myaddr, 6))
	    continue;

	// We are, so invert the least significant big-endian bit 
	myaddr[2] ^= 0x0100;
	break;
    }

    for (int i = 0; i < 3; i++) {
	c->nic.nc_hwaddr[2*i + 0] = myaddr[i] & 0xff;
	c->nic.nc_hwaddr[2*i + 1] = myaddr[i] >> 8;
    }
    memcpy(c->filter[0], c->nic.nc_hwaddr, 6); 

    // Register card with kernel
    c->ih.ih_func = &e1000_intr;
    c->ih.ih_arg = c;

    c->nic.nc_arg = c;
    c->nic.nc_add_txbuf = &e1000_add_txbuf;
    c->nic.nc_add_rxbuf = &e1000_add_rxbuf;
    c->nic.nc_reset = &e1000_buffer_reset;
    c->nic.nc_poll = &e1000_poll;
    c->nic.nc_dh.dh_shutdown = &e1000_shutdown;
    
    if (E1000_VNIC) {
	for (int i = 1; i < E1000_VNIC; i++) {
	    memcpy(c->filter[i], c->filter[0], 6);
	    c->filter[i][4] += i;
	}
	
	r = vnic_attach(&c->nic, c->nic.nc_irq_line, &c->ih, 
			E1000_VNIC, e1000_mac_index, e1000_index_mac);
	if (r < 0)
	    return r;
    } else {
	irq_register(c->nic.nc_irq_line, &c->ih);
	nic_register(&c->nic, device_nic);

	e1000_reset(c);
	
	// All done
	if (print_mac) 
	    cprintf("e1000: irq %d io 0x%x mac %02x:%02x:%02x:%02x:%02x:%02x\n",
		    c->nic.nc_irq_line, c->iobase,
		    c->nic.nc_hwaddr[0], c->nic.nc_hwaddr[1],
		    c->nic.nc_hwaddr[2], c->nic.nc_hwaddr[3],
		    c->nic.nc_hwaddr[4], c->nic.nc_hwaddr[5]);
    }

    LIST_INSERT_HEAD(&card_list, c, list_link);
    e1000_count++;

    //e1000_rx_tx_test(c);
    //e1000_tx_test(c);
    //e1000_rx_test(c);

    return 1;
}
