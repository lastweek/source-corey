#include <machine/param.h>
#include <machine/x86.h>
#include <inc/syscall.h>
#include <inc/fs.h>
#include <inc/lib.h>
#include <efsl/efs.h>
#include <test.h>

#include <string.h>
#include <stdio.h>

static char file_str[] = "Hello world from esfl FS.";

void
efsl_test(void)
{
    EmbeddedFileSystem *efs;
    echeck(segment_alloc(core_env->sh, sizeof(EmbeddedFileSystem), 0,
			 (void **)&efs, 0, "FAT-fs", core_env->pid));

    EmbeddedFile file;	
    uint8_t buf[512];
    char fn[] = "foo";
    
    if (efs_init(efs, 0) != 0) {
	cprintf("Could not open filesystem.\n");
	return;
    }

    if (file_fopen(&file, &efs->myFs, fn, 'r') != 0) {
	cprintf(" Couldn't open file '%s', try to create..\n", fn);
	if(file_fopen(&file, &efs->myFs, fn,'w') != 0) {
	    cprintf(" Couldn't create file '%s'!\n", fn);
	    return;
	}
	
	uint32_t r = file_write(&file, strlen(file_str), (uint8_t *)file_str);
	assert(r == strlen(file_str));
	cprintf(" Created file '%s'!\n", fn);
    } else {
	cprintf(" File '%s' already exists..\n", fn);
    }
    
    assert(file_setpos(&file, 0) == 0);
    assert(file_read(&file, 512, buf) > 0);
    assert(memcmp(file_str, buf, strlen(file_str)) == 0);
    
    cprintf(" Verified file '%s' contents!\n", fn);

    file_fclose(&file);
    fs_umount(&efs->myFs);
}
