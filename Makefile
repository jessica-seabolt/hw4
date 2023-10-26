
CC = gcc
CFLAGS = -Wall -g -std=gnu99

OSS_SRC = oss.c
WORKER_SRC = worker.c
OSS_OBJ = $(OSS_SRC:.c=.o)
WORKER_OBJ = $(WORKER_SRC:.c=.o)

all: oss worker

oss: $(OSS_OBJ)
		$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

worker: $(WORKER_OBJ)
		$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
		$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean
clean:
		rm -f oss worker $(OSS_OBJ) $(WORKER_OBJ)
