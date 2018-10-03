# SPDX-License-Identifier: GPL-2.0/BSD

.SILENT:

COMMON_DIR=src/common
CLI_DIR=src/cli
HAC_DIR=src/host_auto_connect
MON_DIR=src/monitor
DC_DIR=src/discovery_ctrl
SC_DIR=src/supervisory_ctrl
INCL_DIR=src/incl
BIN_DIR=.bin

DEM_CFLAGS = -DMG_ENABLE_THREADS -DMG_ENABLE_HTTP_WEBSOCKET=0

GCC = ${KWWRAP}gcc
CFLAGS = -W -Wall -Wno-unused-function
CFLAGS += -Imongoose -Ijansson/src -I${INCL_DIR}

if EXTRA_SECURITY_CFLAGS
LDFLAGS += -z noexecstack -z relro -z now -pie

CFLAGS += -fstack-protector-strong
CFLAGS += -fPIE -fPIC
CFLAGS += -Wformat

CFLAGS += -D_FORTIFY_SOURCE=2
CFLAGS += -Wformat-security
CFLAGS += -Wno-unused-result
GDB_OPTS = -O2
else
CFLAGS += -Werror
if DEBUG
GDB_OPTS = -g -O0 -DCONFIG_DEBUG
else
GDB_OPTS = -O2
endif

endif

# ALT_CFLAGS used for sparse since mongoose has too many errors
# a modified version of mongoose.h can be created and stored in /files
# for sparse testing but is not valid for executable
ALT_CFLAGS = -W -Wall -Werror -Wno-unused-function
ALT_CFLAGS += -Ifiles -Ijansson/src -I${INCL_DIR} -I${DC_DIR}

SPARSE_OPTS = ${DEM_CFLAGS} ${ALT_CFLAGS} -DCS_PLATFORM=0

VALGRIND_OPTS = --leak-check=full --show-leak-kinds=all -v --track-origins=yes
VALGRIND_OPTS += --suppressions=files/valgrind_suppress

DC_LIBS = -lpthread -lrdmacm -libverbs -lcurl jansson/libjansson.a
SC_LIBS = -lpthread -lrdmacm -libverbs jansson/libjansson.a
HAC_LIBS = -lpthread -lrdmacm -libverbs jansson/libjansson.a
MON_LIBS = -lpthread -lrdmacm -libverbs jansson/libjansson.a

CLI_LIBS = -lcurl jansson/libjansson.a

LINUX_INCL = ${INCL_DIR}/nvme.h ${INCL_DIR}/utils.h

CLI_SRC = ${CLI_DIR}/cli.c ${COMMON_DIR}/curl.c ${CLI_DIR}/show.c
CLI_INC = ${INCL_DIR}/dem.h ${INCL_DIR}/curl.h ${CLI_DIR}/show.h \
	  ${INCL_DIR}/tags.h

MG_DIR ?= mongoose

HAC_SRC = ${HAC_DIR}/daemon.c ${COMMON_DIR}/nvmeof.c ${COMMON_DIR}/rdma.c \
	  ${COMMON_DIR}/logpages.c ${COMMON_DIR}/parse.c
HAC_INC = ${INCL_DIR}/dem.h ${HAC_DIR}/common.h ${INCL_DIR}/ops.h ${LINUX_INCL}

MON_SRC = ${MON_DIR}/daemon.c ${COMMON_DIR}/nvmeof.c ${COMMON_DIR}/rdma.c \
	  ${COMMON_DIR}/logpages.c ${COMMON_DIR}/parse.c
MON_INC = ${INCL_DIR}/dem.h ${MON_DIR}/common.h ${INCL_DIR}/ops.h ${LINUX_INCL}

DC_SRC = ${DC_DIR}/daemon.c ${DC_DIR}/json.c ${DC_DIR}/restful.c \
	 ${DC_DIR}/interfaces.c ${DC_DIR}/pseudo_target.c ${DC_DIR}/config.c \
	 ${COMMON_DIR}/nvmeof.c ${COMMON_DIR}/curl.c ${COMMON_DIR}/rdma.c \
	 ${COMMON_DIR}/logpages.c ${COMMON_DIR}/parse.c ${MG_DIR}/mongoose.c
DC_INC = ${INCL_DIR}/dem.h ${DC_DIR}/json.h ${DC_DIR}/common.h \
	 ${INCL_DIR}/ops.h ${INCL_DIR}/curl.h ${INCL_DIR}/tags.h \
	 mongoose/mongoose.h ${LINUX_INCL}

SC_SRC = ${SC_DIR}/daemon.c ${SC_DIR}/restful.c ${SC_DIR}/configfs.c \
	 ${SC_DIR}/pseudo_target.c ${COMMON_DIR}/rdma.c \
	 ${COMMON_DIR}/nvmeof.c ${COMMON_DIR}/parse.c ${MG_DIR}/mongoose.c
SC_INC = ${INCL_DIR}/dem.h ${SC_DIR}/common.h ${INCL_DIR}/tags.h \
	 ${INCL_DIR}/ops.h mongoose/mongoose.h ${LINUX_INCL}

all: ${BIN_DIR} mongoose/mongoose.h jansson/libjansson.a \
     ${BIN_DIR}/dem ${BIN_DIR}/dem-hac ${BIN_DIR}/dem-dc ${BIN_DIR}/dem-sc \
     ${BIN_DIR}/dem-mon
	echo Done.

${BIN_DIR}:
	mkdir ${BIN_DIR}

dem: ${BIN_DIR}/dem
dem-hac: ${BIN_DIR}/dem-hac
dem-mon: ${BIN_DIR}/dem-mon
dem-dc: ${BIN_DIR}/dem-dc
dem-sc: ${BIN_DIR}/dem-sc

${BIN_DIR}/dem: ${CLI_SRC} ${CLI_INC} Makefile jansson/libjansson.a
	echo CC dem
	${GCC} ${CLI_SRC} -o $@ ${CFLAGS} ${GDB_OPTS} ${CLI_LIBS} \
		-I${CLI_DIR} -DDEM_CLI

${BIN_DIR}/dem-hac: ${HAC_SRC} ${HAC_INC} Makefile
	echo CC dem-hac
	${GCC} ${HAC_SRC} -o $@ ${DEM_CFLAGS} ${CFLAGS} ${GDB_OPTS} \
		${HAC_LIBS} -I${HAC_DIR}

${BIN_DIR}/dem-mon: ${MON_SRC} ${MON_INC} Makefile
	echo CC dem-mon
	${GCC} ${MON_SRC} -o $@ ${DEM_CFLAGS} ${CFLAGS} ${GDB_OPTS} \
		${MON_LIBS} -I${MON_DIR} -DDEBUG_LOG_PAGES

${BIN_DIR}/dem-dc: ${DC_SRC} ${DC_INC} Makefile jansson/libjansson.a
	echo CC dem-dc
	${GCC} ${DC_SRC} -o $@ ${DEM_CFLAGS} ${CFLAGS} ${GDB_OPTS} \
		${DC_LIBS} -I${DC_DIR}

${BIN_DIR}/dem-sc: ${SC_SRC} ${SC_INC} Makefile jansson/libjansson.a
	echo CC dem-sc
	${GCC} ${SC_SRC} -o $@ ${DEM_CFLAGS} ${CFLAGS} ${GDB_OPTS} \
		${SC_LIBS} -I${SC_DIR}

clean:
	rm -rf ${BIN_DIR}/ config.json *.vglog .k* kw.cfg
	echo Done.

must_be_root:
	[ `whoami` == "root" ]

install: must_be_root
	[ -L /bin/dem ] || cp ${BIN_DIR}/* /bin
	[ -e /etc/nvme/nvmeof-dem ] || mkdir -p /etc/nvme/nvmeof-dem
	cp usr/nvmeof.conf /etc/nvme
	cp usr/nvmeof /usr/libexec/
	cp usr/nvmeof.service /usr/lib/systemd/system/nvmeof.service
	cp usr/bash_completion.d/* /etc/bash_completion.d/
	cp usr/man/*.1 /usr/share/man/man1/
	cp usr/man/*.8 /usr/share/man/man8/
	echo Done.

uninstall: must_be_root
	[ `whoami` == "root" ]
	rm -f /etc/bash_completion.d/dem
	rm -f /bin/dem /bin/dem-dc /bin/dem-sc /bin/dem-hac /bin/dem-mon
	rm -f /etc/nvme/nvmeof.conf
	rm -rf /etc/nvme/nvmeof-dem
	rm -f /usr/libexec/nvmeof
	rm -f /usr/lib/systemd/system/nvmeof.service
	rm -f /usr/share/man/man1/dem.1 /usr/share/man/man8/dem-dc.8
	rm -f /usr/share/man/man1/dem-sc.8 /usr/share/man/man8/dem-hac.8
	echo Done.

mongoose/.git:
	echo cloning github.com/cesanta/mongoose.git
	echo NOTE: if no access to github, use scp to copy mongoose/.git
	git clone https://github.com/cesanta/mongoose.git >/dev/null
	touch $@
mongoose/mongoose.h: mongoose/.git
	cd mongoose ; git reset -q --hard HEAD
	touch $@

jansson/.git:
	echo cloning github.com/akheron/jansson.git
	echo NOTE: if no access to github, use scp to copy jansson/.git
	git clone https://github.com/akheron/jansson.git >/dev/null
	touch $@
jansson/Makefile.am: jansson/.git
	cd jansson ; git reset -q --hard HEAD
	touch $@
jansson/configure: jansson/Makefile.am
	echo configuring jansson
	cd jansson ; autoreconf -i >/dev/null 2>&1
	touch $@
jansson/Makefile: jansson/configure
	cd jansson ; ./configure >/dev/null
	touch $@
jansson/src/.libs/libjansson.a: jansson/Makefile
	echo building libjansson
	cd jansson/src ; make libjansson.la >/dev/null
jansson/libjansson.a: jansson/src/.libs/libjansson.a
	cp jansson/src/.libs/libjansson.a jansson

archive: clean
	[ -d archive ] || mkdir archive
	tar cz -f archive/`date +"%y%m%d_%H%M"`.tgz .gitignore .git/config \
		files/ html/ Makefile src/ usr/

test_cli: dem
	./dem config
	./dem list ctrl
	./dem set ctrl ctrl1
	./dem set portid 1 rdma ipv4 1.1.1.2 2332
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

# show format for raw: fmt=-r make show_hosts
# show format for pretty json: fmt=-j make show_hosts
show_hosts:
	for i in `./dem lis h |grep -v ^http: | grep -v "^No .* defined"`; \
		do ./dem $$fmt get h $$i ; done

show_targets:
	for i in `./dem lis t |grep -v ^http: | grep -v "^No .* defined"`; \
		do ./dem $$fmt get t $$i ; done

del_hosts:
	for i in `./dem lis h |grep -v ^http: | grep -v "^No .* defined"`; \
		do ./dem -f del h $$i ; done

del_targets:
	for i in `./dem lis t |grep -v ^http: | grep -v "^No .* defined"`; \
		do ./dem -f del t $$i ; done

put:
	echo PUT Commands
	curl -X PUT -d '' http://127.0.0.1:22345/host/host01
	echo
	curl -X PUT -d '' http://127.0.0.1:22345/host/host02
	echo
	curl -X PUT -d '' http://127.0.0.1:22345/target/ctrl1
	echo
	curl -X PUT -d '' http://127.0.0.1:22345/target/ctrl2
	echo
	curl -X PUT -d '' http://127.0.0.1:22345/target/ctrl1/subsys/subsys1
	echo
	echo
get:
	echo GET Commands
	curl http://127.0.0.1:22345/target
	echo
	curl http://127.0.0.1:22345/target/ctrl1
	echo
	curl http://127.0.0.1:22345/host
	echo
	curl http://127.0.0.1:22345/host/host01
	echo
	echo

del: delhost01 delhost02 delctrl1 delctrl2
	echo

delctrl2:
	curl -X DELETE  http://127.0.0.1:22345/target/ctrl2
	echo
delctrl1:
	curl -X DELETE  http://127.0.0.1:22345/target/ctrl1
	echo
delhost01:
	echo DELETE Commands
	curl -X DELETE  http://127.0.0.1:22345/host/host01
	echo
delhost02:
	curl -X DELETE  http://127.0.0.1:22345/host/host02
	echo
post:
	echo POST Commands
	curl -d '{"HOSTNQN":"host02"}' http://127.0.0.1:22345/host/host01
	echo
	echo

get16:
	for i in `seq 1 16` ; do make get ; done

test: put get16 post get del

memcheck: dem-dc
	reset
	valgrind ${VALGRIND_OPTS} --log-file=dem-dc.vglog dem-dc
	echo "valgrind output in 'dem-dc.vglog'"

sparse:
	echo running sparse of each .c file with options
	echo "${SPARSE_OPTS}"
	for i in src/*/*.c ; do sparse $$i ${SPARSE_OPTS} ; done
	echo Done.

kw.cfg:
	rm -rf .kw*
	echo creating klocwork project
	~/klocwork/kw-client/bin/kwcheck create
	echo rebuilding for klocwork
	MG_DIR=files \
	  KWWRAP="~/klocwork/kw-client/bin/kwwrap -o /tmp/klocwork.out " make
	~/klocwork/kw-client/bin/kwinject -t /tmp/klocwork.out -o kw.cfg
	rm /tmp//klocwork.out

klocwork: kw.cfg
	echo generating klocwork report
	~/klocwork/kw-client/bin/kwcheck run -b kw.cfg > klocwork.out
	echo klocwork output in klocwork.out
