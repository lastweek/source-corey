
# build for Linux

ROOT=/home/stein/sqlite-3.5.7/

INCDIR=$(ROOT)/
LIBDIR=$(ROOT)/.libs/

BENCHES= db-select-shared db-select-diff 

benches: $(BENCHES)

db-select-shared: db-select.c
	gcc -std=c99 -DLINUX_SHARED -I$(INCDIR) -L$(LIBDIR) -o db-select-shared db-select.c -lsqlite3 -lpthread -ldl

db-select-diff: db-select.c
	gcc -std=c99 -DLINUX_DIFF -I$(INCDIR) -L$(LIBDIR) -o db-select-diff db-select.c -lsqlite3

clean:
	rm -f $(BENCHES) *.db

