# Compiler and flags
CC = gcc
CFLAGS = -Wall -std=c11 -O2

# Cross-platform commands
CLEANUP = rm -f

# Detect OS for executable extension
ifeq ($(OS),Windows_NT)
    EXE = .exe
else
    EXE =
endif

# Program names
PROGRAM = vnaScan$(EXE)
SOURCES = VnaScanMultithreaded.c VnaScanMultithreadedMain.c
OBJECTS = $(SOURCES:.c=.o)

# Libraries
LIBS = -lserialport -lpthread -lm

# Default target
all: $(PROGRAM)

# Build executable
$(PROGRAM): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(PROGRAM) $(LIBS)

# Compile source files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	$(CLEANUP) $(PROGRAM) $(OBJECTS)

.PHONY: all clean