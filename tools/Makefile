CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

.PHONY: all clean

all: uart_bl_update

uart_bl_update: uart_bl_update.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f uart_bl_update
