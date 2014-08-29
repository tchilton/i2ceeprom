# Makefile for i2ceeprom

SHELL = /bin/sh
CC    = gcc
 
FLAGS        = -std=gnu99 -Iinclude
CFLAGS       = -Wall -O
RFLAGS = -O2 
 
TARGET  = i2ceeprom
SOURCES = $(shell echo *.c)
COMMON  = 
HEADERS = $(shell echo *.h)
OBJECTS = $(SOURCES:.c=.o)
 
PREFIX = $(DESTDIR)/usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/man/man1
 
all: $(TARGET)
 
$(TARGET): $(OBJECTS) $(COMMON)
	$(CC) $(FLAGS) $(CFLAGS) $(RFLAGS) -o $(TARGET) $(SOURCES)

profile: CFLAGS += -pg
profile: $(TARGET)
 
install: $(TARGET)
	install -D -m 755 $(TARGET)   $(BINDIR)/$(TARGET)
	install -D -m 644 $(TARGET).1 $(MANDIR)/$(TARGET).1
 
uninstall:
	-rm $(BINDIR)/$(TARGET)
	-rm $(MANDIR)/$(TARGET).1
 
clean:
	-rm -f $(OBJECTS)
	-rm -f gmon.out
 
distclean: clean
	-rm -f $(TARGET)
 
.SECONDEXPANSION:
 
$(foreach OBJ,$(OBJECTS),$(eval $(OBJ)_DEPS = $(shell gcc -MM $(OBJ:.o=.c) | sed s/.*://)))
%.o: %.c $$($$@_DEPS)
	$(CC) $(FLAGS) $(CFLAGS) $(DEBUGFLAGS) -c -o $@ $<
 
# %.o: %.c $(HEADERS) $(COMMON)
#	$(CC) $(FLAGS) $(CFLAGS) $(DEBUGFLAGS) -c -o $@ $<
 
.PHONY : all profile install uninstall clean distclean
