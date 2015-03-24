CFLAGS	+= -Wall -O3 -std=c99 -D_POSIX_C_SOURCE=200809L -D_BSD_SOURCE -D_XOPEN_SOURCE=600
LDFLAGS	+=

OBJS	= main.o net.o request_handler.o http.o util.o log.o cgi.o index.o

.PHONY:	clean

sws: 	$(OBJS)
	if [ `uname` = SunOS ]; then export LIBS=-lsocket; fi;\
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) $$LIBS -o $@

clean:
	rm -f sws $(OBJS)
