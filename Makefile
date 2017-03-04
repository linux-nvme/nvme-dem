.SILENT:
CFLAGS = -g -W -Wall -Werror -Wno-unused-function
CFLAGS += -DMG_ENABLE_THREADS -DMG_ENABLE_HTTP_WEBSOCKET=0
CFLAGS += -lpthread

GDB_OPTS = -g -O0

CLI_SRC = cli.c curl.c show.c
DAEMON_SRC = daemon.c json.c restful.c mongoose.c
DAEMON_INC = json.h common.h mongoose.h

all: demd dem config.json
	echo Done.

dem: ${CLI_SRC} Makefile
	echo CC $@
	gcc ${CLI_SRC} -o $@ -lcurl -ljson-c ${GDB_OPTS}

demd: ${DAEMON_SRC} ${DAEMON_INC} Makefile
	echo CC $@
	gcc ${DAEMON_SRC} -o $@ -ljson-c ${CFLAGS}

clean:
	rm -f dem demd unittest config.json
	echo Done.

config.json:
	cp archive/config.json .

archive: clean
	[ -d archive ] || mkdir archive
	tar cz -f archive/`date +"%y%m%d_%H%M"`.tgz Makefile *.c *.h

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

put:
	curl -X PUT -d bar -d foo http://127.0.0.1:12345/host/host01 --verbose
get:
	curl http://127.0.0.1:12345/host/host01 --verbose
del:
	curl -X DELETE -d bar http://127.0.0.1:12345/host/host01 --verbose
post:
	curl -d bar -d foo http://127.0.0.1:12345/host/host01 --verbose
opt:
	curl -X OPTION -d bar http://127.0.0.1:12345/dem --verbose
test: get put del post opt url
