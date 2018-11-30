# SPDX-License-Identifier: GPL-2.0/BSD

.SILENT:

DEM_EXE=dem
MON_EXE=dem-dm
CLI_EXE=dem-cli
AC_EXE=dem-ac
EM_EXE=dem-em

BIN_DIR=.bin
COMMON_DIR=src/common
INCL_DIR=src/incl

DEM_DIR=src/dem
MON_DIR=src/monitor
CLI_DIR=src/cli
EM_DIR=src/endpoint
AC_DIR=src/auto_connect

DEM_CFLAGS = -DMG_ENABLE_THREADS -DMG_ENABLE_HTTP_WEBSOCKET=0

if NVMEOF_TRACING
DEM_CFLAGS += -DDEBUG_COMMANDS
endif

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

if CI_SERVER
CFLAGS += -Wno-unused-result
endif

# ALT_CFLAGS used for sparse since mongoose has too many errors
# a modified version of mongoose.h can be created and stored in /files
# for sparse testing but is not valid for executable
ALT_CFLAGS = -W -Wall -Werror -Wno-unused-function
ALT_CFLAGS += -Ifiles -Ijansson/src -I${INCL_DIR} -I${DEM_DIR}

SPARSE_OPTS = ${DEM_CFLAGS} ${ALT_CFLAGS} -DCS_PLATFORM=0

VALGRIND_OPTS = --leak-check=full --show-leak-kinds=all -v --track-origins=yes
VALGRIND_OPTS += --suppressions=files/valgrind_suppress

DEM_LIBS = -lpthread -lrdmacm -libverbs -lcurl jansson/libjansson.a
EM_LIBS = -lpthread -lrdmacm -libverbs jansson/libjansson.a
AC_LIBS = -lpthread -lrdmacm -libverbs jansson/libjansson.a
MON_LIBS = -lpthread -lrdmacm -libverbs jansson/libjansson.a

CLI_LIBS = -lcurl jansson/libjansson.a

LINUX_INCL = ${INCL_DIR}/nvme.h ${INCL_DIR}/utils.h

CLI_SRC = ${CLI_DIR}/cli.c ${COMMON_DIR}/curl.c ${CLI_DIR}/show.c
CLI_INC = ${INCL_DIR}/dem.h ${INCL_DIR}/curl.h ${CLI_DIR}/show.h \
	  ${INCL_DIR}/tags.h

MG_DIR ?= mongoose

if SPDK
CFLAGS += -DCONFIG_SPDK
EM_LIBS += -lspdk -lspdk_env_dpdk -ldpdk -ldl -lnuma -lpciaccess
EM_SPDK_CFG = ${EM_DIR}/spdk_config.c
endif

if CONFIGFS
CFLAGS += -DCONFIG_CONFIGFS
EM_CFGFS_CFG = ${EM_DIR}/configfs.c
endif

AC_SRC = ${AC_DIR}/daemon.c ${COMMON_DIR}/nvmeof.c ${COMMON_DIR}/rdma.c \
	 ${COMMON_DIR}/logpages.c ${COMMON_DIR}/parse.c ${COMMON_DIR}/tcp.c
AC_INC = ${INCL_DIR}/dem.h ${AC_DIR}/common.h ${INCL_DIR}/ops.h ${LINUX_INCL}

MON_SRC = ${MON_DIR}/daemon.c ${COMMON_DIR}/nvmeof.c ${COMMON_DIR}/rdma.c \
	  ${COMMON_DIR}/logpages.c ${COMMON_DIR}/parse.c ${COMMON_DIR}/tcp.c
MON_INC = ${INCL_DIR}/dem.h ${MON_DIR}/common.h ${INCL_DIR}/ops.h ${LINUX_INCL}

DEM_SRC = ${DEM_DIR}/daemon.c ${DEM_DIR}/config.c ${DEM_DIR}/restful.c \
	  ${DEM_DIR}/interfaces.c ${DEM_DIR}/pseudo_target.c \
	  ${COMMON_DIR}/nvmeof.c ${COMMON_DIR}/curl.c ${COMMON_DIR}/rdma.c \
	  ${COMMON_DIR}/logpages.c ${DEM_DIR}/logpages.c ${COMMON_DIR}/tcp.c \
	  ${DEM_DIR}/json.c ${COMMON_DIR}/parse.c ${MG_DIR}/mongoose.c
DEM_INC = ${INCL_DIR}/dem.h ${DEM_DIR}/json.h ${DEM_DIR}/common.h \
	  ${INCL_DIR}/ops.h ${INCL_DIR}/curl.h ${INCL_DIR}/tags.h \
	  mongoose/mongoose.h ${LINUX_INCL}

EM_SRC = ${EM_DIR}/daemon.c ${EM_DIR}/restful.c ${EM_DIR}/etc_config.c \
	 ${EM_DIR}/pseudo_target.c ${COMMON_DIR}/rdma.c ${COMMON_DIR}/tcp.c \
	 ${COMMON_DIR}/nvmeof.c ${COMMON_DIR}/parse.c ${MG_DIR}/mongoose.c \
	 ${EM_CFGFS_CFG} ${EM_SPDK_CFG}

EM_INC = ${INCL_DIR}/dem.h ${EM_DIR}/common.h ${INCL_DIR}/tags.h \
	 ${INCL_DIR}/ops.h mongoose/mongoose.h ${LINUX_INCL}

all: ${BIN_DIR} mongoose/mongoose.h jansson/libjansson.a \
     ${BIN_DIR}/${DEM_EXE} ${BIN_DIR}/${CLI_EXE} ${BIN_DIR}/${EM_EXE} \
     ${BIN_DIR}/${MON_EXE} ${BIN_DIR}/${AC_EXE}

	echo Done.

${BIN_DIR}:
	mkdir ${BIN_DIR}

${DEM_EXE}: ${BIN_DIR}/${DEM_EXE}
${CLI_EXE}: ${BIN_DIR}/${CLI_EXE}
${MON_EXE}: ${BIN_DIR}/${MON_EXE}
${AC_EXE}: ${BIN_DIR}/${AC_EXE}
${EM_EXE}: ${BIN_DIR}/${EM_EXE}

${BIN_DIR}/${CLI_EXE}: ${CLI_SRC} ${CLI_INC} Makefile jansson/libjansson.a
	echo CC ${CLI_EXE}
	${GCC} ${CLI_SRC} -o $@ ${CFLAGS} ${GDB_OPTS} ${CLI_LIBS} \
		-I${CLI_DIR} -DDEM_CLI

${BIN_DIR}/${AC_EXE}: ${AC_SRC} ${AC_INC} Makefile
	echo CC ${AC_EXE}
	${GCC} ${AC_SRC} -o $@ ${DEM_CFLAGS} ${CFLAGS} ${GDB_OPTS} \
		${AC_LIBS} -I${AC_DIR}

${BIN_DIR}/${MON_EXE}: ${MON_SRC} ${MON_INC} Makefile
	echo CC ${MON_EXE}
	${GCC} ${MON_SRC} -o $@ ${DEM_CFLAGS} ${CFLAGS} ${GDB_OPTS} \
		${MON_LIBS} -I${MON_DIR} -DDEBUG_LOG_PAGES

${BIN_DIR}/${DEM_EXE}: ${DEM_SRC} ${DEM_INC} Makefile jansson/libjansson.a
	echo CC ${DEM_EXE}
	${GCC} ${DEM_SRC} -o $@ ${DEM_CFLAGS} ${CFLAGS} ${GDB_OPTS} \
		${DEM_LIBS} -I${DEM_DIR}

${BIN_DIR}/${EM_EXE}: ${EM_SRC} ${EM_INC} Makefile jansson/libjansson.a
	echo CC ${EM_EXE}
	${GCC} ${EM_SRC} -o $@ ${DEM_CFLAGS} ${CFLAGS} ${GDB_OPTS} \
		${EM_LIBS} -I${EM_DIR}

clean:
	rm -rf ${BIN_DIR}/ config.json *.vglog .k* kw.cfg
	echo Done.

must_be_root:
	[ `whoami` == "root" ]

install: must_be_root
	[ -L /bin/dem ] || cp ${BIN_DIR}/* /bin
	[ -e /etc/nvme/nvmeof-dem ] || mkdir -p /etc/nvme/nvmeof-dem
	cp usr/nvmeof.conf /etc/nvme
	[ -d /usr/libexec ] && cp usr/nvmeof /usr/libexec/
	cp usr/nvmeof.service /usr/lib/systemd/system/nvmeof.service
	cp usr/bash_completion.d/* /etc/bash_completion.d/
	cp usr/man/*.1 /usr/share/man/man1/
	cp usr/man/*.8 /usr/share/man/man8/
	echo Done.

uninstall: must_be_root
	[ `whoami` == "root" ]
	rm -f /etc/bash_completion.d/${CLI_EXE}
	rm -f /bin/${CLI_EXE} /bin/${AC_EXE} /bin/${MON_EXE}
	rm -f /bin/${DEM_EXE} /bin/${EM_EXE}
	rm -f /etc/nvme/nvmeof.conf
	rm -rf /etc/nvme/nvmeof-dem
	rm -f /usr/libexec/nvmeof
	rm -f /usr/lib/systemd/system/nvmeof.service
	rm -f /usr/share/man/man1/${CLI_EXE}.1 /usr/share/man/man8/${MON_EXE}.1
	rm -f /usr/share/man/man1/${EM_EXE}.8 /usr/share/man/man8/${AC_EXE}.8
	rm -f /usr/share/man/man1/${DEM_EXE}.8
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

test_cli: ${CLI_EXE}
	${CLI_EXE} config
	${CLI_EXE} list ctrl
	${CLI_EXE} set ctrl ctrl1
	${CLI_EXE} set portid 1 rdma ipv4 1.1.1.2 2332
	${CLI_EXE} show ctrl ctrl1
	${CLI_EXE} rename ctrl ctrl1 ctrl2
	${CLI_EXE} add ss ctrl2 ss21 ss22
	${CLI_EXE} delete ss ctrl2 ss21
	${CLI_EXE} delete ctrl ctrl2
	${CLI_EXE} list host
	${CLI_EXE} set host host01
	${CLI_EXE} rename host host01 host02
	${CLI_EXE} add acl host01 ss11 ss21
	${CLI_EXE} show host host01
	${CLI_EXE} delete acl host02 ss21
	${CLI_EXE} delete host host02
	${CLI_EXE} shutdown

# show format for raw: fmt=-r make show_hosts
# show format for pretty json: fmt=-j make show_hosts
show_hosts:
	for i in `${CLI_EXE} lis h|grep -v ^http:|grep -v "^No .* defined"`; \
		do ${CLI_EXE} $$fmt get h $$i ; done

show_targets:
	for i in `${CLI_EXE} lis t|grep -v ^http:|grep -v "^No .* defined"`; \
		do ${CLI_EXE} $$fmt get t $$i ; done

del_hosts:
	for i in `${CLI_EXE} lis h|grep -v ^http:|grep -v "^No .* defined"`; \
		do ${CLI_EXE} -f del h $$i ; done

del_targets:
	for i in `${CLI_EXE} lis t|grep -v ^http:|grep -v "^No .* defined"`; \
		do ${CLI_EXE} -f del t $$i ; done

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

memcheck: ${DEM_EXE}
	reset
	valgrind ${VALGRIND_OPTS} --log-file=dem.vglog ${DEM_EXE}
	echo "valgrind output in 'dem.vglog'"

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
