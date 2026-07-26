# Compiler and Flags
CC ?= gcc
CFLAGS ?= -Wall -Wextra -pedantic -std=gnu99 -g
LDFLAGS ?= -lpthread -lrt

# Target Executables
DAEMON_TARGET = sysmond
CLI_TARGET    = sys_monitor-cli

# Source and Object Files
DAEMON_SRCS = sysmond.c sys_monitor.c
DAEMON_OBJS = $(DAEMON_SRCS:.c=.o)

CLI_SRCS    = sys_monitor-CLI.c
CLI_OBJS    = $(CLI_SRCS:.c=.o)

# Default rule: build both targets
all: $(DAEMON_TARGET) $(CLI_TARGET)

# Build Daemon Executable
$(DAEMON_TARGET): $(DAEMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build CLI Executable
$(CLI_TARGET): $(CLI_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Generic pattern rule for object files
%.o: %.c sys_monitor.h
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(DAEMON_OBJS) $(CLI_OBJS) $(DAEMON_TARGET) $(CLI_TARGET)

.PHONY: all clean
