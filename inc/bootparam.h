#ifndef JOS_INC_BOOTPARAM_H
#define JOS_INC_BOOTPARAM_H

#include <machine/types.h>

// For details, see Linux Documentation/i386/boot.txt

#define SETUP_HEADER_OFFSET 0x1F1
#define SETUP_LOAD_ADDR     0x2000

struct setup_header
{
    uint8_t  setup_sects;
    uint16_t root_flags;
    uint32_t syssize;
    uint16_t ram_size;
    uint16_t vid_mode;
    uint16_t root_dev;
    uint16_t boot_flag;
    uint16_t jump;
    uint32_t header;
    uint16_t version;
    uint32_t realmode_swtch;
    uint16_t start_sys;
    uint16_t kernel_version;
    uint8_t  type_of_loader;
    uint8_t  loadflags;
    uint16_t setup_move_size;
    uint32_t code32_start;
    uint32_t ramdisk_image;
    uint32_t ramdisk_size;
    uint32_t bootsect_kludge;
    uint16_t heap_end_ptr;
    uint16_t _pad1;
    uint32_t cmd_line_ptr;
    uint32_t initrd_addr_max;
    uint32_t kernel_alignment;
    uint8_t  relocatable_kernel;
} __attribute__((packed));

#endif
