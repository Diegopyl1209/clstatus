CC = gcc
CFLAGS = -Wall -O2 $(shell pkg-config --cflags libpulse) $(shell pkg-config --cflags x11)
LDFLAGS = $(shell pkg-config --libs libpulse) $(shell pkg-config --libs x11)

#CFLAGS += $(shell pkg-config --cflags x11) -DX11
#LDFLAGS += $(shell pkg-config --libs x11)

TARGET = clstatus
SRC = main.c
PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(BINDIR)/$(TARGET)

uninstall:
	rm -f $(BINDIR)/$(TARGET)
