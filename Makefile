# $Id$

ifeq ($(shell test \! -f Make.Rules || echo yes),yes)
		include Make.Rules
endif

DESTDIR=
TARGET = netsend
OBJECTS =    error.o    \
						 file.o     \
						 getopt.o   \
						 main.o     \
						 net.o      \
						 receive.o  \
						 transmit.o \
						 xfuncs.o

# Inline workaround:
# max-inline-insns-single specified the maximum size
# of a function (counted in internal gcc instructions).
# Default: 300
CFLAGS += --param max-inline-insns-single=400

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
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS)

%.o: %.c global.h
	$(CC) $(CFLAGS) -c  $< -o $@

install: all
	install $(TARGET) $(DESTDIR)$(BINDIR)

uninstall:
	rm $(DESTDIR)$(BINDIR)/$(TARGET)

clean :
	@rm -rf $(TARGET) $(OBJECTS) core *~

distclean: clean
	@rm -f config.h Make.Rules

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
