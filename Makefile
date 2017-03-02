# Change these as appropriate depending on how you have libui installed.
LIBUI_HOME = /usr/local/libui
LIBUI_LIBDIR = $(LIBUI_HOME)/build/out

SRCS = coremon.c
OBJS = $(SRCS:%.c=%.o)

CC = gcc

INC = -I$(LIBUI_HOME)
CFLAGS = -std=gnu99 -g $(OPT) -Wall -D_REENTRANT $(INC) $(shell pkg-config --cflags gtk+-3.0)

LDFLAGS = -L$(LIBUI_LIBDIR) -lui $(shell pkg-config --libs gtk+-3.0) -lm -lpthread -Wl,-rpath=$(LIBUI_LIBDIR)

%.o : %.c
	$(CC) $(CFLAGS) -c $< -o $*.o

coremon : $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

clean :
	rm -f *.o coremon depend.mak

depend.mak :
	touch $@

depend :
	$(CC) $(CFLAGS) -M $(SRCS) > depend.mak

include depend.mak
