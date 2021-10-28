
# This makefile system follows the structuring conventions
# recommended by Peter Miller in his excellent paper:
#
#	Recursive Make Considered Harmful
#	http://aegis.sourceforge.net/auug97.pdf
#

# Target kernel architecture/type

K_ARCH	 := amd64
OBJDIR	 := obj
# use 'yes' to include symbol names in backtraces, requires rebuild
STABS_BT := no
# use 'yes' to use streamflow
USE_SF   := no
# use 'yes' to use sim allocator
USE_SIMALLOC := no
# use 'yes' to omit frame pointer, which may improve performance
REMOVE_FP := yes

-include conf/gcc.$(K_ARCH).mk
include conf/Makefrag.$(K_ARCH)

GCC_LIB := $(shell $(CC) -print-libgcc-file-name)
TOP	:= $(shell echo $${PWD-`pwd`})
GCC_EH_LIB := $(shell $(CC) -dumpspecs | grep -q .lgcc_eh && $(CC) -print-file-name=libgcc_eh.a)

# Native commands
NCC	:= gcc -pipe
NCXX	:= g++ -pipe
PERL	:= perl

# Compiler flags.
COMWARNS := -Wformat=2 -Wextra -Wmissing-noreturn \
	    -Wwrite-strings -Wno-unused-parameter -Wmissing-format-attribute \
	    -Wswitch-default -fno-builtin
CWARNS	 := $(COMWARNS) -Wmissing-prototypes -Wmissing-declarations -Wshadow
CXXWARNS := $(COMWARNS) -Wno-non-template-friend

ifeq ($(REMOVE_FP), yes)
OPTFLAG	 := -O3 -fomit-frame-pointer
else
OPTFLAG	 := -O3 -fno-omit-frame-pointer
endif

ifeq ($(ARCH),amd64)
OPTFLAG  += -march=opteron
endif

BASECFLAGS  := -nostdinc -isystem $(shell $(CC) -print-file-name=include)
COMFLAGS    := $(BASECFLAGS) -g $(OPTFLAG) -fno-strict-aliasing \
	       -fno-stack-protector -Wall -MD -DJOS_ARCH_$(ARCH)
CSTD	    := -std=c99 -fms-extensions
INCLUDES    := -I$(TOP) -I$(TOP)/kern -I$(TOP)/$(OBJDIR)

# Linker flags for user programs
CRT1	:= $(OBJDIR)/lib/crt1.o
CRTI	:= $(OBJDIR)/lib/crti.o
CRTN	:= $(OBJDIR)/lib/crtn.o

LDEPS	:= $(CRT1) $(CRTI) $(CRTN)	\
	   $(OBJDIR)/lib/libjos.a	\
	   $(OBJDIR)/lib/libc.a		\
	   $(OBJDIR)/lib/libstdc++.a	\
	   $(OBJDIR)/lib/libefsl.a	\
	   $(OBJDIR)/lib/liblwip.a	\
	   $(OBJDIR)/lib/libfs.a	\
	   $(OBJDIR)/lib/libsqlite3.a	\
	   $(OBJDIR)/lib/gcc.specs	\
	   user/user.ld

ifeq ($(USE_SF), yes)
LDEPS	    += $(OBJDIR)/lib/libstreamflow.a
LSTREAMFLOW := -lstreamflow
endif

ifeq ($(USE_SIMALLOC), yes)
LDEPS	    += $(OBJDIR)/lib/libsimalloc.a
LSIMALLOC   := -lsimalloc
endif

LDFLAGS := -B$(TOP)/$(OBJDIR)/lib -L$(TOP)/$(OBJDIR)/lib \
	   -specs=$(TOP)/$(OBJDIR)/lib/gcc.specs -static

# Lists that the */Makefrag makefile fragments will add to
OBJDIRS :=

# Make sure that 'all' is the first target
all:

# Eliminate default suffix rules
.SUFFIXES:

# Delete target files if there is an error (or make is interrupted)
.DELETE_ON_ERROR:

# make it so that no intermediate .o files are ever deleted
.PRECIOUS: %.o $(OBJDIR)/boot/%.o $(OBJDIR)/kern/%.o $(OBJDIR)/lib/%.o \
	$(OBJDIR)/user/%.o $(OBJDIR)/user/%.debuginfo $(OBJDIR)/user/% \
	$(OBJDIR)/kern/kernel.base $(OBJDIR)/kern/kernel.init

KERN_CFLAGS := $(COMFLAGS) $(INCLUDES) -DJOS_KERNEL $(CWARNS) -Werror
USER_INC    := $(INCLUDES)
USER_COMFLAGS = $(COMFLAGS) $(USER_INC) -DJOS_USER
LDFLAGS	      += -T user/user.ld

ifeq ($(USE_SF), yes)
USER_COMFLAGS += -DUMMAP_PAGEALIGN
endif

ifeq ($(STABS_BT), yes)
USER_COMFLAGS += -gstabs -DSTABS_BT
endif

USER_CFLAGS   = $(USER_COMFLAGS) $(CWARNS)
USER_CXXFLAGS = $(USER_COMFLAGS) $(CXXWARNS) -D__STDC_FORMAT_MACROS

# try to infer the correct GCCPREFIX
conf/gcc.$(K_ARCH).mk:
	@if $(TARGET)-objdump -i 2>&1 | grep '^$(OBJTYPE)$$' >/dev/null 2>&1; \
	then echo 'GCCPREFIX=$(TARGET)-' >conf/gcc.$(K_ARCH).mk; \
	elif objdump -i 2>&1 | grep '^$(OBJTYPE)$$' >/dev/null 2>&1; \
	then echo 'GCCPREFIX=' >conf/gcc.$(K_ARCH).mk; \
	else echo "***" 1>&2; \
	echo "*** Error: Couldn't find GCC/binutils for $(OBJTYPE)." 1>&2; \
	echo "*** Is the directory with $(TARGET)-gcc in your PATH?" 1>&2; \
	echo "*** If $(OBJTYPE) toolchain is installed with a command" 1>&2; \
	echo "*** prefix other than '$(TARGET)-', set your GCCPREFIX" 1>&2; \
	echo "*** environment variable to that prefix re-run 'gmake'." 1>&2; \
	echo "*** To turn off this error:" 1>&2; \
	echo "***     echo GCCPREFIX= >conf/gcc.$(K_ARCH).mk" 1>&2; \
	echo "***" 1>&2; exit 1; fi

# Include Makefrags for subdirectories
include boot/Makefrag
include kern/Makefrag
include lib/Makefrag
include fs/Makefrag
include pkg/Makefrag
include user/Makefrag
include test/Makefrag

bochs: $(OBJDIR)/kern/bochs.img $(OBJDIR)/fs/fs.img
	bochs-nogui

tags:
	@:

dist:
	make distclean
	tar -C.. -czvf josmp.tar.gz \
	    --exclude=\.svn --exclude=TODO --exclude=obj \
	    --exclude=notes --exclude=josmp.tar.gz \
	    $(shell basename `pwd`)

# For deleting the build
clean:
	rm -rf $(OBJDIR)/.deps $(OBJDIR)/*
	rm -f bochs.log

distclean: clean
	rm -f conf/gcc.$(K_ARCH).mk
	find . -type f \( -name '*~' -o -name '.*~' \) -exec rm -f \{\} \;

# This magic automatically generates makefile dependencies
# for header files included from C source files we compile,
# and keeps those dependencies up-to-date every time we recompile.
# See 'mergedep.pl' for more information.
$(OBJDIR)/.deps: $(foreach dir, $(OBJDIRS), $(wildcard $(OBJDIR)/$(dir)/*.d))
	@mkdir -p $(@D)
	$(PERL) mergedep.pl $@ $^

-include $(OBJDIR)/.deps

GNUmakefile: $(OBJDIR)/machine $(OBJDIR)/efsl
$(OBJDIR)/machine:
	@mkdir -p $(@D)
	ln -s $(TOP)/kern/arch/$(K_ARCH) $@

always:
	@:

.PHONY: all always clean distclean tags
