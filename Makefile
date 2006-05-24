# $Id$

TARGET = netsend
OBJECTS = netsend.o
CFLAGS += -g 
WARNINGS = -Wall -W -Wwrite-strings -Wsign-compare       \
           -Wpointer-arith -Wcast-qual -Wcast-align      \
           -Wstrict-prototypes -Wmissing-prototypes      \
           -Wnested-externs -Winline -Wshadow -Wformat=2

XFLAGS = -DDEBUG


all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(WARNINGS) -o $(TARGET) $(OBJECTS)

%.o: %.c
	$(CC) $(XFLAGS) $(WARNINGS) $(CFLAGS) -c  $< -o $@

clean :
	-rm -rf $(TARGET) $(OBJECTS) core *~
