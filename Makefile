CC = gcc
CFLAGS = -Wall -g 
OBJS = main.o

all: server

server: $(OBJS)
	$(CC) $(OBJS) -o server $(LDFLAGS)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

clean: 
	rm -rf *~ *.o server
