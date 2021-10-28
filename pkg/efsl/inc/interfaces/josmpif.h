#ifndef __JOSMPIF_H__
#define __JOSMPIF_H__

/*****************************************************************************/
#include "../debug.h"
#include "../types.h"
#include "config.h"
#include <inc/share.h>
/*****************************************************************************/

/*************************************************************\
              hwInterface
               ----------
* FILE* 	imagefile		File emulation of hw interface.
* long		sectorCount		Number of sectors on the file.
\*************************************************************/
struct hwInterface
{
    // required by efsl
    eint32 sectorCount;

    struct sobj_ref disk_dev;
    struct sobj_ref buf_seg;
    void *	    buf_base;
};
typedef struct hwInterface hwInterface;

esint8 if_initInterface(hwInterface* file,eint8* fileName);
esint8 if_readBuf(hwInterface* file,euint32 address,euint8* buf);
esint8 if_writeBuf(hwInterface* file,euint32 address,euint8* buf);
esint8 if_setPos(hwInterface* file,euint32 address);

#endif
