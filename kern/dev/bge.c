#include <kern/arch.h>
#include <kern/nic.h>
#include <kern/lib.h>
#include <kern/timer.h>
#include <dev/bge.h>
#include <dev/bgereg.h>
#include <inc/error.h>

struct bge_card {
    struct nic nic;

    struct pci_func pcif;

    uint32_t membase;
    uint16_t pci_dev_id;
};

static uint32_t
bge_readmem_ind(struct bge_card *c, uint32_t off)
{
    uint32_t val;
    pci_conf_write(&c->pcif, BGE_PCI_MEMWIN_BASEADDR, off);
    val = pci_conf_read(&c->pcif, BGE_PCI_MEMWIN_DATA);
    pci_conf_write(&c->pcif, BGE_PCI_MEMWIN_BASEADDR, 0);
    return val;
}

static void
bge_writemem_ind(struct bge_card *c, uint32_t off, uint32_t val)
{
    pci_conf_write(&c->pcif, BGE_PCI_MEMWIN_BASEADDR, off);
    pci_conf_write(&c->pcif, BGE_PCI_MEMWIN_DATA, val);
    pci_conf_write(&c->pcif, BGE_PCI_MEMWIN_BASEADDR, 0);
}

static void __attribute__((unused))
bge_writereg_ind(struct bge_card *c, uint32_t off, uint32_t val)
{
    pci_conf_write(&c->pcif, BGE_PCI_REG_BASEADDR, off);
    pci_conf_write(&c->pcif, BGE_PCI_REG_DATA, val);
}

static uint32_t
bge_io_read(struct bge_card *c, uint32_t addr)
{
    physaddr_t pa = c->membase + addr;
    volatile uint32_t *ptr = pa2kva(pa);
    return *ptr;
}

static void
bge_io_write(struct bge_card *c, uint32_t addr, uint32_t val)
{
    physaddr_t pa = c->membase + addr;
    volatile uint32_t *ptr = pa2kva(pa);
    *ptr = val;
}

/*
 * Read a byte of data stored in the EEPROM at address 'addr.' The
 * BCM570x supports both the traditional bitbang interface and an
 * auto access interface for reading the EEPROM. We use the auto
 * access method.
 */
static uint8_t
bge_eeprom_getbyte(struct bge_card *c, uint32_t addr, uint8_t *dest)
{
    int i;
    uint32_t byte = 0;
    
    /*
     * Enable use of auto EEPROM access so we can avoid
     * having to use the bitbang method.
     */
    uint32_t ctl = bge_io_read(c, BGE_MISC_LOCAL_CTL);
    bge_io_write(c, BGE_MISC_LOCAL_CTL, ctl | BGE_MLC_AUTO_EEPROM);
    
    /* Reset the EEPROM, load the clock period. */
    bge_io_write(c, BGE_EE_ADDR, 
		 BGE_EEADDR_RESET|BGE_EEHALFCLK(BGE_HALFCLK_384SCL));
    timer_delay(20 * 1000);
    
    /* Issue the read EEPROM command. */
    bge_io_write(c, BGE_EE_ADDR, BGE_EE_READCMD | addr);
    
    /* Wait for completion */
    for(i = 0; i < BGE_TIMEOUT * 10; i++) {
	timer_delay(10 * 1000);
	if (bge_io_read(c, BGE_EE_ADDR) & BGE_EEADDR_DONE)
	    break;
    }
    
    if (i == BGE_TIMEOUT) {
	cprintf("bge_eeprom_getbyte: read timed out\n");
	return -E_IO;
    }
    
    /* Get result. */
    byte = bge_io_read(c, BGE_EE_DATA);
    *dest = (byte >> ((addr % 4) * 8)) & 0xFF;
    
    return 0;
}

/*
 * Read a sequence of bytes from the EEPROM.
 */
static int
bge_read_eeprom(struct bge_card *c, uint8_t *dest, uint32_t off, size_t len)
{
    size_t i;
    int r;
    uint8_t byte;
    
    for (byte = 0, i = 0; i < len; i++) {
	r = bge_eeprom_getbyte(c, off + i, &byte);
	if (r < 0)
	    return r;
	*(dest + i) = byte;
    }
    
    return 0;
}

static void
bge_reset(struct bge_card *c)
{
    pci_conf_write(&c->pcif, BGE_PCI_MISC_CTL,
		   BGE_PCIMISCCTL_INDIRECT_ACCESS|BGE_PCIMISCCTL_MASK_PCI_INTR|
		   BGE_HIF_SWAP_OPTIONS|BGE_PCIMISCCTL_PCISTATE_RW);

    /* Save some important PCI state. */
    uint32_t cachesize = pci_conf_read(&c->pcif, BGE_PCI_CACHESZ);
    uint32_t command = pci_conf_read(&c->pcif, BGE_PCI_CMD);
    uint32_t pcistate = pci_conf_read(&c->pcif, BGE_PCI_PCISTATE);
    

    /*
     * Write the magic number to SRAM at offset 0xB50.
     * When firmware finishes its initialization it will
     * write ~BGE_MAGIC_NUMBER to the same location.
     */
    bge_writemem_ind(c, BGE_SOFTWARE_GENCOMM, BGE_MAGIC_NUMBER);
    
    uint32_t reset = BGE_MISCCFG_RESET_CORE_CLOCKS | (65 << 1) | (1 << 29);

    /* Issue global reset */
    bge_writemem_ind(c, BGE_MISC_CFG, reset);

    timer_delay(1000 * 1000);

    pci_conf_write(&c->pcif, BGE_PCI_MISC_CTL,
		   BGE_PCIMISCCTL_INDIRECT_ACCESS|BGE_PCIMISCCTL_MASK_PCI_INTR|
		   BGE_HIF_SWAP_OPTIONS|BGE_PCIMISCCTL_PCISTATE_RW);

    pci_conf_write(&c->pcif, BGE_PCI_CACHESZ, cachesize);
    pci_conf_write(&c->pcif, BGE_PCI_CMD, command);
    bge_writemem_ind(c, BGE_MISC_CFG, (65 << 1));    

    /* Enable memory arbiter. */
    bge_io_write(c, BGE_MARB_MODE, BGE_MARBMODE_ENABLE);
    
    /*
     * Poll until we see the 1's complement of the magic number.
     * This indicates that the firmware initialization
     * is complete.
     */
    int32_t i, val;
    for (i = 0; i < BGE_TIMEOUT; i++) {
	val = bge_readmem_ind(c, BGE_SOFTWARE_GENCOMM);
	if (val == ~BGE_MAGIC_NUMBER)
	    break;
	timer_delay(10 * 1000);
    }

    if (i == BGE_TIMEOUT) {
	cprintf("bge_reset: firmware handshake timed out,found 0x%08x\n", val);
	return;
    }

    /*
     * XXX Wait for the value of the PCISTATE register to
     * return to its original pre-reset state. This is a
     * fairly good indicator of reset completion. If we don't
     * wait for the reset to fully complete, trying to read
     * from the device's non-PCI registers may yield garbage
     * results.
     */
    for (i = 0; i < BGE_TIMEOUT; i++) {
	if (pci_conf_read(&c->pcif, BGE_PCI_PCISTATE) == pcistate)
	    break;
	timer_delay(10 * 1000);
    }
}

int
bge_attach(struct pci_func *pcif)
{
    struct bge_card *c;
    int r = page_alloc((void **) &c, 0);
    if (r < 0)
	return r;
    
    memset(c, 0, sizeof(*c));
    static_assert(PGSIZE >= sizeof(*c));

    pci_func_enable(pcif);
    memcpy(&c->pcif, pcif, sizeof(c->pcif));

    c->nic.nc_irq_line = pcif->irq_line;
    c->membase = pcif->reg_base[0];
    c->pci_dev_id = pcif->dev_id;
    
    uint32_t val = bge_readmem_ind(c, 0x0c14);
    if ((val >> 16) == 0x484b) {
	c->nic.nc_hwaddr[0] = (uint8_t)(val >> 8);
	c->nic.nc_hwaddr[1] = (uint8_t)val;
	val = bge_readmem_ind(c, 0x0c18);
	c->nic.nc_hwaddr[2] = (uint8_t)(val >> 24);
	c->nic.nc_hwaddr[3] = (uint8_t)(val >> 16);
	c->nic.nc_hwaddr[4] = (uint8_t)(val >> 8);
	c->nic.nc_hwaddr[5] = (uint8_t)val;
    } else if ((r = bge_read_eeprom(c, c->nic.nc_hwaddr,
				    BGE_EE_MAC_OFFSET + 2, 6)) < 0)
    {
	cprintf("bge_attach: unable to read MAC address: %s\n", e2s(r));
	return r;
    }

    bge_reset(c);

    cprintf("bge: irq %d mac %02x:%02x:%02x:%02x:%02x:%02x\n",
	    c->nic.nc_irq_line,
	    c->nic.nc_hwaddr[0], c->nic.nc_hwaddr[1],
	    c->nic.nc_hwaddr[2], c->nic.nc_hwaddr[3],
	    c->nic.nc_hwaddr[4], c->nic.nc_hwaddr[5]);

    return 0;
}
