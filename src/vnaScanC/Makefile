CC=clang
CFLAGS=-Wall -Werror

all: scan scan-multithreaded

scan:
	$(CC) $(CFLAGS) vnaScan.c -o vnaScan 

scan-multithreaded:
	$(CC) $(CFLAGS) vnaScanMultithreaded.c -o vnaScanMultithreaded -lpthread -lm
