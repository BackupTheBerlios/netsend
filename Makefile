# $Id$

ifeq ($(shell test \! -f Make.Rules || echo yes),yes)
		include Make.Rules
endif

TARGET = netsend
OBJECTS = analyze.o error.o file.o \
	getopt.o main.o net.o \
	proto_tipc.o proto_udp.o \
	receive.o trans_common.o \
	ns_hdr.o xfuncs.o proto_tcp.o \
	proto_udp_trans.o \
	proto_dccp_trans.o \
	proto_tipc_trans.o

POD = netsend.pod
MAN = netsend.1

LIBS = -lm

# use 64 bit off_t etc. even on 32 bit systems
CFLAGS += -D_FILE_OFFSET_BITS=64 

# Inline workaround:
# max-inline-insns-single specified the maximum size
# of a function (counted in internal gcc instructions).
# Default: 300
CFLAGS += --param max-inline-insns-single=400

# XXX: add path configure
DESTDIR=/usr
BINDIR=/bin

all: config.h $(TARGET)

config.h: Make.Rules

Make.Rules: configure
	@if [ ! -f Make.Rules ] ; then                   \
	echo "No Make.Rules file present" ;              \
	echo "Hint: call ./configure script" ;           \
	echo "./configure --help for more information" ; \
  exit 1 ; fi

config.h: 
	@bash configure

$(TARGET): $(OBJECTS)
	$(CC) $(LIBS) $(CFLAGS) -o $(TARGET) $(OBJECTS)

%.o: %.c analyze.h error.h global.h xfuncs.h Makefile
	$(CC) $(CFLAGS) -c  $< -o $@

install: all
	install $(TARGET) $(DESTDIR)$(BINDIR)

uninstall:
	rm $(DESTDIR)$(BINDIR)/$(TARGET)

clean :
	@rm -rf $(TARGET) $(OBJECTS) core *~

distclean: clean
	@rm -f config.h Make.Rules $(MAN)

man: $(POD)
	pod2man -d $(TARGET) -c $(TARGET) $(POD) > $(MAN)

test: unit_test.sh $(TARGET)
	@./unit_test.sh

DISTNAME=$(TARGET)

release:
	@if [ ! -f Make.Rules ]; then echo $(MAKE) Make.Rules first ;exit 1 ;fi
	@if [ ! -L ../$(DISTNAME)-$(MAJOR_REL).$(MINOR_REL) ]; then \
		echo generating ../$(DISTNAME)-$(MAJOR_REL).$(MINOR_REL) link ; \
		ln -sf $(DISTNAME) ../$(DISTNAME)-$(MAJOR_REL).$(MINOR_REL) ; \
		echo to ../$(DISTNAME) . ; fi
	@diff ../$(DISTNAME)-$(MAJOR_REL).$(MINOR_REL)/Make.Rules Make.Rules
	$(MAKE) distclean
	cd .. ; tar zvfc $(DISTNAME)-$(MAJOR_REL).$(MINOR_REL).tar.gz \
		--exclude .svn  --exclude '.#*' \
		$(DISTNAME)-$(MAJOR_REL).$(MINOR_REL)/*
