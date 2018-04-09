CC = gcc
CFLAGS = -O2 -Wall
ORIG = src
DEST = usr/sbin

all: usr/sbin/oi-lab-multi-seat-config-read-devices \
     usr/sbin/oi-lab-multi-seat-config-message \
     usr/sbin/oi-lab-multi-seat-config-window

$(DEST)/oi-lab-multi-seat-config-read-devices: $(ORIG)/oi-lab-multi-seat-config-read-devices.c
	$(CC) $(CFLAGS) $< -o $@

$(DEST)/oi-lab-multi-seat-config-message: $(ORIG)/oi-lab-multi-seat-config-message.c
	$(CC) $(CFLAGS) $< -o $@ `pkg-config --libs --cflags cairo xcb-aux`

$(DEST)/oi-lab-multi-seat-config-window: $(ORIG)/oi-lab-multi-seat-config-window.c
	$(CC) $(CFLAGS) $< -o $@ `pkg-config --libs --cflags xcb-aux`

clean:
	rm -f usr/sbin/oi-lab-multi-seat-config-read-devices usr/sbin/oi-lab-multi-seat-config-message usr/sbin/oi-lab-multi-seat-config-window
