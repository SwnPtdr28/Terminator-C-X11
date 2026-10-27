CC = gcc
CFLAGS = -Wall -Wextra -pthread -Iinclude
LDFLAGS = -lX11 -pthread

SOURCES = src/main.c src/gui.c src/tabs.c src/executor.c src/shell.c src/history.c src/engine.c
HEADERS = include/terminator.h include/gui.h include/tabs.h include/executor.h include/shell.h include/history.h include/engine.h

all: terminator

terminator: $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(SOURCES) -o terminator $(LDFLAGS)

clean:
	rm -f terminator

.PHONY: all clean
