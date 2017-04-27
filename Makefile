.SILENT:

CFLAGS = -W -Wall -Werror -Wno-unused-function
CFLAGS += -Iincl -Imongoose -Ijansson/src

DEM_CFLAGS = -DMG_ENABLE_THREADS -DMG_ENABLE_HTTP_WEBSOCKET=0
DEM_CFLAGS += -lpthread -lfabric -luuid libjansson.a

CLI_CFLAGS = -lcurl libjansson.a

GDB_OPTS = -g -O0

CLI_SRC = cli.c curl.c show.c
CLI_INC = curl.h show.h tags.h
DEM_SRC = daemon.c json.c restful.c mongoose/mongoose.c \
	  parse.c ofi.c logpages.c interfaces.c pseudo_target.c
DEM_INC = json.h common.h mongoose/mongoose.h tags.h

all: mongoose/ libjansson.a demd dem
	echo Done.

dem: ${CLI_SRC} ${CLI_INC} Makefile libjansson.a
	echo CC $@
	gcc ${CLI_SRC} -o $@ ${CLI_CFLAGS} ${CFLAGS} ${GDB_OPTS}

demd: ${DEM_SRC} ${DEM_INC} Makefile libjansson.a
	echo CC $@
	gcc ${DEM_SRC} -o $@ ${DEM_CFLAGS} ${CFLAGS} ${GDB_OPTS}

clean:
	rm -f dem demd config.json
	echo Done.

mongoose/:
	echo cloning github.com/cesanta/mongoose.git
	git clone https://github.com/cesanta/mongoose.git
	cd mongoose ; patch -p 1 < ../mongoose.patch

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
libjansson.a: jansson/src/.libs/libjansson.a
	cp jansson/src/.libs/libjansson.a .

#must be run as root. hard coded path to nvme-cli
get_logpages:
	/home/cayton/src/nvme/nvme-cli/nvme discover /dev/nvme-fabrics -t rdma -a 192.168.22.1 -s 4422

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
	echo "sudo make get_logpages" >> $@
	echo "make show_ctrls" >> $@
	echo "make show_hosts" >> $@
	echo "make del_ctrls" >> $@
	echo "make del_hosts" >> $@
	echo "./dem apply" >> $@
	echo "sudo make get_logpages" >> $@
	echo "fmt=-j make show_ctrls" >> $@
	echo "make show_hosts" >> $@
	echo "fmt=-j sh archive/make_config.sh" >> $@
	echo "./dem apply" >> $@

config.json: archive/make_config.sh
	sh archive/make_config.sh

run_test: archive/run_test.sh
	sh archive/run_test.sh

archive: clean
	[ -d archive ] || mkdir archive
	tar cz -f archive/`date +"%y%m%d_%H%M"`.tgz Makefile *.c *.h *.patch

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
	for i in `./dem lis h |grep -v ^http: | grep -v "^No .* config"`; do ./dem $$fmt sho h $$i ; done

show_ctrls:
	for i in `./dem lis c |grep -v ^http: | grep -v "^No .* config"`; do ./dem $$fmt sho c $$i ; done

del_hosts:
	for i in `./dem lis h |grep -v ^http: | grep -v "^No .* config"`; do ./dem del h $$i ; done

del_ctrls:
	for i in `./dem lis c |grep -v ^http: | grep -v "^No .* config"`; do ./dem del c $$i ; done

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
	./dem del c ctrl1 || echo -n
	./dem del c ctrl2 || echo -n
	./dem del h host01 || echo -n
	./dem del h host02 || echo -n
	sh archive/make_config.sh
	./dem apply
	./dem list c
	./dem list h

memcheck: demd
	reset
	valgrind --leak-check=full --show-leak-kinds=all -v --track-origins=yes --log-file=demd.vglog ./demd

post_reboot:
	sudo ssh root@host02 "cd ~cayton/nvme_scripts; ./rdma_target"
	cd ~cayton/nvme_scripts; ./rdma_target
	modprobe nvme_rdma
