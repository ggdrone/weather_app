# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wpedantic -Wextra -g
LIBS = -lcurl -ljson-c

# Project files
OBJS = main.o weather_app.o
TARGET = wa

# Default target
all: $(TARGET)

# Link final executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

# Compile from .c -> .o
%.o: %.c weather_app.h
	$(CC) $(CFLAGS) -c $<

# Clean .o files
clean:
	rm -f $(OBJS) $(TARGET)
