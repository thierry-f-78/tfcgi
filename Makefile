ifeq ($(DEBUG),1)
PRE_CFLAGS = -g -DDEBUG
else
PRE_CFLAGS = -O2
endif

ifeq ($(NO_THREADS),1)
else
PRE_CFLAGS += -DUSE_THREADS
PRE_LDFLAGS += -lpthread
endif

CFLAGS = $(PRE_CFLAGS) -Wall $(INCLUDES)

LDFLAGS = $(PRE_LDFLAGS) -lfcgi

LIBNAME = libtfcgi.so.0.0

OBJS = tfcgi.o

all: $(LIBNAME)

$(LIBNAME): libtfcgi-static.a
	-@rm -f $(LIBNAME)
	$(CC) -shared $(LDFLAGS) -Wl,-soname -Wl,$(LIBNAME) -o $(LIBNAME) $(OBJS)

libtfcgi-static.a: $(OBJS)
	$(AR) rcv libtfcgi-static.a $(OBJS)

clean:
	-rm -f $(OBJS) libtfcgi-static.a $(LIBNAME)

