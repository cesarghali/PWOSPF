#------------------------------------------------------------------------------
# File: Makefile
# 
# Note: This Makefile requires GNU make.
# 
# (c) 2001,2000 Stanford University
#
#------------------------------------------------------------------------------

all : sr

CC = g++

OSTYPE = $(shell uname)

ifeq ($(OSTYPE),Linux)
ARCH = -D_LINUX_
SOCK = -lnsl
endif

ifeq ($(OSTYPE),SunOS)
ARCH =  -D_SOLARIS_
SOCK = -lnsl -lsocket
endif

ifeq ($(OSTYPE),Darwin)
ARCH = -D_DARWIN_
SOCK =
endif

ifdef NO_DEBUG
  CFLAGS = -g -Wall -ansi $(ARCH)
else
  CFLAGS = -g -Wall -ansi -D_DEBUG_ $(ARCH)
endif

LIBS= $(SOCK) -lm -lresolv -lpthread
PFLAGS= -follow-child-processes=yes -cache-dir=/tmp/${USER} 
PURIFY= purify ${PFLAGS}

sr_SRCS = sr_router.c sr_main.c  \
          sr_if.c sr_rt.c sr_vns_comm.c   \
          sr_dumper.c sr_pwospf.c sha1.c cache.c queue.c \
          pwospf_neighbors.c pwospf_topology.c dijkstra_stack.c

sr_OBJS = $(patsubst %.c,%.o,$(sr_SRCS))
sr_DEPS = $(patsubst %.c,.%.d,$(sr_SRCS))

$(sr_OBJS) : %.o : %.c
	$(CC) -c $(CFLAGS) $< -o $@

$(sr_DEPS) : .%.d : %.c
	$(CC) -MM $(CFLAGS) $<  > $@

include $(sr_DEPS)	

sr : $(sr_OBJS)
	$(CC) $(CFLAGS) -o sr $(sr_OBJS) $(LIBS) 

sr.purify : $(sr_OBJS)
	$(PURIFY) $(CC) $(CFLAGS) -o sr.purify $(sr_OBJS) $(LIBS)

.PHONY : clean clean-deps dist    

clean:
	rm -f *.o *~ core sr *.dump *.tar tags

clean-deps:
	rm -f .*.d

dist-clean: clean clean-deps
	rm -f .*.swp pwospf_stub.tar.gz

dist: dist-clean 
	(cd ..; tar -X pwospf_stub/exclude -cvf pwospf_stub.tar pwospf_stub/; gzip pwospf_stub.tar); \
    mv ../pwospf_stub.tar.gz .

tags:
	ctags *.c
