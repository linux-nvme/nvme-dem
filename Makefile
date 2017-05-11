.SILENT:

DEM_CFLAGS = -DMG_ENABLE_THREADS -DMG_ENABLE_HTTP_WEBSOCKET=0

CFLAGS = -W -Wall -Werror -Wno-unused-function
CFLAGS += -Imongoose -Ijansson/src -Isrc -I.

# ALT_CFLAGS used for sparse since mongoose has too many errors
# a modified version of mongoose.h can be created and stored in /files
# for sparse testing but is not valid for executable
ALT_CFLAGS = -W -Wall -Werror -Wno-unused-function
ALT_CFLAGS += -Ifiles -Ijansson/src -Isrc -I.

SPARSE_OPTS = ${DEM_CFLAGS} ${ALT_CFLAGS} -DCS_PLATFORM=0

VALGRIND_OPTS = --leak-check=full --show-leak-kinds=all -v --track-origins=yes
VALGRIND_OPTS += --suppressions=files/valgrind_suppress

DEM_LIBS = -lpthread -lfabric -luuid jansson/libjansson.a

CLI_LIBS = -lcurl jansson/libjansson.a

GDB_OPTS = -g -O0

CLI_SRC = src/cli.c src/curl.c src/show.c
CLI_INC = src/curl.h src/show.h src/tags.h
DEM_SRC = src/daemon.c src/json.c src/restful.c mongoose/mongoose.c \
	  src/parse.c src/ofi.c src/logpages.c src/interfaces.c src/pseudo_target.c
DEM_INC = src/json.h src/common.h mongoose/mongoose.h src/tags.h

all: mongoose/ jansson/libjansson.a demd dem
	echo Done.

dem: ${CLI_SRC} ${CLI_INC} Makefile jansson/libjansson.a
	echo CC $@
	gcc ${CLI_SRC} -o $@ ${CFLAGS} ${GDB_OPTS} ${CLI_LIBS}

demd: ${DEM_SRC} ${DEM_INC} Makefile jansson/libjansson.a
	echo CC $@
	gcc ${DEM_SRC} -o $@ ${DEM_CFLAGS} ${CFLAGS} ${GDB_OPTS} ${DEM_LIBS}

clean:
	rm -f dem demd config.json
	echo Done.

mongoose/:
	echo cloning github.com/cesanta/mongoose.git
	git clone https://github.com/cesanta/mongoose.git
	cd mongoose ; patch -p 1 < ../files/mongoose.patch

jansson/Makefile.am:
	echo cloning github.com/akheron/jansson.git
	git clone https://github.com/akheron/jansson.git >/dev/null
jansson/configure: jansson/Makefile.am
	echo configuring jansson
	cd jansson ; autoreconf -i >/dev/null 2>&1
jansson/Makefile: jansson/configure
	cd jansson ; ./configure >/dev/null
jansson/src/.libs/libjansson.a: jansson/Makefile
	echo building libjansson
	cd jansson/src ; make libjansson.la >/dev/null
jansson/libjansson.a: jansson/src/.libs/libjansson.a
	cp jansson/src/.libs/libjansson.a jansson

# hard coded path to nvme-cli
get_logpages:
	sudo /home/cayton/src/nvme/nvme-cli/nvme discover /dev/nvme-fabrics \
		-t rdma -a 192.168.22.1 -s 4422 || echo nvme-cli error $$?

archive/make_config.sh: Makefile
	[ -d archive ] || mkdir archive
	echo ./dem set c ctrl1 rdma ipv4 192.168.22.1 4420 20 > $@
	echo ./dem set c ctrl2 rdma ipv4 192.168.22.2 4420 25 >> $@
	echo ./dem set s ctrl1 host01subsys1 1 >> $@
	echo ./dem set s ctrl1 host01subsys2 0 >> $@
	echo ./dem set s ctrl1 host01subsys3 1 >> $@
	echo ./dem set s ctrl2 host02subsys1 1 >> $@
	echo ./dem set s ctrl2 host02subsys2 0 >> $@
	echo ./dem set h host01 >> $@
	echo ./dem set h host02 >> $@
	echo ./dem set a host01 host01subsys2 1 >> $@
	echo ./dem set a host01 host02subsys2 2 >> $@
	echo ./dem set a host02 host01subsys2 3 >> $@
	echo ./dem set a host02 host02subsys2 3 >> $@

archive/run_test.sh: Makefile
	[ -d archive ] || mkdir archive
	echo "make del_hosts" > $@
	echo "make del_ctrls" >> $@
	echo "./dem apply" >> $@
	echo "sh archive/make_config.sh" >> $@
	echo "./dem apply" >> $@
	echo "make get_logpages" >> $@
	echo "fmt=-j make show_ctrls" >> $@
	echo "fmt=-j make show_hosts" >> $@
	echo "make del_ctrls" >> $@
	echo "make del_hosts" >> $@
	echo "./dem apply" >> $@
	echo "make get_logpages" >> $@
	echo "make show_ctrls" >> $@
	echo "make show_hosts" >> $@
	echo "sh archive/make_config.sh" >> $@
	echo "./dem apply" >> $@

config.json: archive/make_config.sh
	sh archive/make_config.sh

run_test: archive/run_test.sh
	sh archive/run_test.sh

archive: clean
	[ -d archive ] || mkdir archive
	tar cz -f archive/`date +"%y%m%d_%H%M"`.tgz Makefile src/ files/ incl/

test_cli: dem
	./dem list ctrl
	./dem set ctrl ctrl1 rdma ipv4 1.1.1.2 2332 25
	./dem show ctrl ctrl1
	./dem rename ctrl ctrl1 ctrl2
	./dem add ss ctrl2 ss21 ss22
	./dem delete ss ctrl2 ss21
	./dem delete ctrl ctrl2
	./dem list host
	./dem set host host01
	./dem rename host host01 host02
	./dem add acl host01 ss11 ss21
	./dem show host host01
	./dem delete acl host02 ss21
	./dem delete host host02
	./dem shutdown
	./dem config

# show format for raw: fmt=-r make show_hosts
# show format for pretty json: fmt=-j make show_hosts
show_hosts:
	for i in `./dem lis h |grep -v ^http: | grep -v "^No .* config"`; \
		do ./dem $$fmt sho h $$i ; done

show_ctrls:
	for i in `./dem lis c |grep -v ^http: | grep -v "^No .* config"`; \
		do ./dem $$fmt sho c $$i ; done

del_hosts:
	for i in `./dem lis h |grep -v ^http: | grep -v "^No .* config"`; \
		do ./dem -f del h $$i ; done

del_ctrls:
	for i in `./dem lis c |grep -v ^http: | grep -v "^No .* config"`; \
		do ./dem -f del c $$i ; done

put:
	echo PUT Commands
	curl -X PUT -d '' http://127.0.0.1:22345/host/host01
	echo
	curl -X PUT -d '' http://127.0.0.1:22345/host/host02
	echo
	curl -X PUT -d '' http://127.0.0.1:22345/controller/ctrl1
	echo
	curl -X PUT -d '' http://127.0.0.1:22345/controller/ctrl2
	echo
	curl -X PUT -d '' http://127.0.0.1:22345/controller/ctrl1/subsys1
	echo
	echo
get:
	echo GET Commands
	curl http://127.0.0.1:22345/controller
	echo
	curl http://127.0.0.1:22345/controller/ctrl1
	echo
	curl http://127.0.0.1:22345/host
	echo
	curl http://127.0.0.1:22345/host/host01
	echo
	echo

del: delhost01 delhost02 delctrl1 delctrl2
	echo

delctrl2:
	echo DELETE Commands
	curl -X DELETE  http://127.0.0.1:22345/controller/ctrl2
	echo
delctrl1:
	echo DELETE Commands
	curl -X DELETE  http://127.0.0.1:22345/controller/ctrl1
	echo
delhost01:
	curl -X DELETE  http://127.0.0.1:22345/host/host01
	echo
delhost02:
	curl -X DELETE  http://127.0.0.1:22345/host/host02
	echo
post:
	echo POST Commands
	curl -d 'host02' http://127.0.0.1:22345/host/host01
	echo
	echo

get16:
	for i in `seq 1 16` ; do make get ; done

test: put get16 post get del

run: dem
	./dem -f del c ctrl1 || echo -n
	./dem -f del c ctrl2 || echo -n
	./dem -f del h host01 || echo -n
	./dem -f del h host02 || echo -n
	sh archive/make_config.sh
	./dem apply
	./dem list c
	./dem list h

memcheck: demd
	reset
	valgrind ${VALGRIND_OPTS} --log-file=demd.vglog ./demd
	echo "valgrind output in 'demd.vglog'"

sparse:
	echo running sparse of each .c file with options
	echo "${SPARSE_OPTS}"
	for i in src/*.c ; do sparse $$i ${SPARSE_OPTS} ; done
	echo Done.

simple_test:
	date
	rm -f config.json
	make config.json
	./dem apply
	sleep 1
	make get_logpages
	sleep 1
	echo
	make del_ctrls
	make del_hosts
	./dem apply
	make get_logpages

post_reboot:
	sudo ssh root@host02 "cd ~cayton/nvme_scripts; ./rdma_target"
	cd ~cayton/nvme_scripts; ./rdma_target
	sudo modprobe nvme_rdma
