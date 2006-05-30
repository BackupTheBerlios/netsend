# $Id$

DESTDIR=
BINDIR=/usr/bin

TARGET = netsend
OBJECTS = netsend.o
CFLAGS += -g -Os
WARNINGS = -Wall -W -Wwrite-strings -Wsign-compare       \
           -Wpointer-arith -Wcast-qual -Wcast-align      \
           -Wstrict-prototypes -Wmissing-prototypes      \
           -Wnested-externs -Winline -Wshadow -Wformat=2

XFLAGS = -DDEBUG


all: config.h $(TARGET)

config.h: 
	@bash configure $(KERNEL_INCLUDE)

$(TARGET): $(OBJECTS)
	$(CC) $(WARNINGS) -o $(TARGET) $(OBJECTS)

%.o: %.c
	$(CC) $(XFLAGS) $(WARNINGS) $(CFLAGS) -c  $< -o $@

install: all
	install -m 0644 $(TARGET) $(DESTDIR)$(BINDIR)

uninstall:
	rm $(DESTDIR)$(BINDIR)/$(TARGET)

clean :
	@rm -rf $(TARGET) $(OBJECTS) core *~

distclean: clean
	@rm -f config.h
