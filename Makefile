# FIXME: this Makefile should not make the tree even if there is no need to do
# it

CC = gcc
CFLAGS = -O2 -Wall

all: binaries

binaries: src/read-devices.c src/write-message.c src/seat-parent-window.c
	$(CC) $(CFLAGS) src/read-devices.c -o src/read-devices
	$(CC) $(CFLAGS) src/write-message.c -o src/write-message `pkg-config --libs --cflags cairo xcb-aux`
	$(CC) $(CFLAGS) src/seat-parent-window.c -o src/seat-parent-window `pkg-config --libs --cflags xcb-aux`

clean:
	rm -f src/read-devices
	rm -f src/write-message
	rm -f src/seat-parent-window
