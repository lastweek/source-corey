#!/bin/sh
qemu-system-x86_64 \
	-serial stdio \
	-smp 4 \
	-hda ./obj/boot/bochs.img \
	-hdb ./obj/fs/fs.fat \
	-m 256 \
	-redir tcp:8000::8000
