CC = gcc
CFLAGS = -Wall -g 
OBJS = main.o

all: myserver

myserver: $(OBJS)
	$(CC) $(OBJS) -o myserver $(LDFLAGS)

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

clean: 
	rm -rf *~ *.o myserver
