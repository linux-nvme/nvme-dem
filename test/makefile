.SILENT:

.PHONY: all
all: ut

ut: rdma.c test.c ops.h makefile
	echo CC rdma.c test.c
	gcc -O0 -g rdma.c test.c -o $@ -lrdmacm -libverbs

.PHONY: clean
clean:
	-rm -f ut

.PHONY: archive
archive: clean
	tar cz -f archive/`date +%y%m%d_%H%M`.tgz M* *.[ch]
