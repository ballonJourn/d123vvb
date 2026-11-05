# Host build of the board C port (dataserver_c) plus the upstream C++
# remoteClient, used together by tests/test_dataserver.py.
CC      ?= cc
CFLAGS  ?= -O2 -g
CFLAGS  += -std=gnu99 -Wall -Wextra -Werror -pthread -I..
UPSTREAM := ../../Project2

all: dataserver_c remoteClient

dataserver_c: ../dataserver.c ../dataserver.h ../dsstat.c ../dsstat.h ../netmon.h main_host.c
	$(CC) $(CFLAGS) -o $@ ../dataserver.c ../dsstat.c main_host.c

test_format: ../dataserver.c ../dsstat.c ../dsstat.h ../netmon.h test_format.c
	$(CC) $(CFLAGS) -o $@ ../dataserver.c ../dsstat.c test_format.c

remoteClient:
	$(MAKE) -C $(UPSTREAM) client
	cp $(UPSTREAM)/remoteClient $@

asan: CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
asan: clean-server dataserver_c

tsan: CFLAGS += -fsanitize=thread
tsan: clean-server dataserver_c

clean-server:
	rm -f dataserver_c test_format

clean: clean-server
	rm -f remoteClient
	$(MAKE) -C $(UPSTREAM) clean

.PHONY: all asan tsan clean clean-server
