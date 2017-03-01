.SILENT:
CFLAGS = -g -W -Wall -Werror -Wno-unused-function
CFLAGS += -DMG_ENABLE_THREADS -DMG_ENABLE_HTTP_WEBSOCKET=0
CFLAGS += -lpthread

GDB_OPTS = -g -O0

all: demcli dem config.json
	echo Done.

demcli: cli.c curl.c Makefile
	echo CC $@
	gcc cli.c curl.c -o $@ -lcurl ${GDB_OPTS}

dem: dem.c json.c json.h mongoose.c mongoose.h Makefile
	echo CC $@
	gcc dem.c json.c mongoose.c -o $@ -ljson-c $(CFLAGS)

clean:
	rm -f dem demcli unittest config.json
	echo Done.

config.json:
	cp archive/config.json .

archive: clean
	[ -d archive ] || mkdir archive
	tar cz -f archive/`date +"%y%m%d_%H%M"`.tgz Makefile *.c *.h

test_cli: demcli
	./demcli list ctrl
	./demcli add ctrl ctrl1
	./demcli set ctrl ctrl1 rdma ipv4 1.1.1.2 2332 25
	./demcli show ctrl ctrl1
	./demcli rename ctrl ctrl1 ctrl2
	./demcli add ss ctrl2 ss21 ss22
	./demcli delete ss ctrl2 ss21
	./demcli delete ctrl ctrl2
	./demcli list host
	./demcli add host host01
	./demcli rename host host01 host02
	./demcli add acl host01 ss11 ss21
	./demcli show host host01
	./demcli delete acl host02 ss21
	./demcli delete host host02
	./demcli shutdown
	./demcli config

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
