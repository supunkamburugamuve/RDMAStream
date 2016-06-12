CC=gcc
CFLAGS=-Wall -g -ggdb
#LDFLAGS= -lrdmacm -libverbs
LDFLAGS= -libverbs -pthread

all: rdma

rdma: rdma.o
	$(CC) $(CFLAGS) rdma.o -o rdma $(LDFLAGS)	

rdma.o: rdma.c
	${CC} -c rdma.c

clean_rdma:
	rm -f rdma rdma.o

server: server.o stream.o
	$(CC) $(CFLAGS) server.o stream.o -o server $(LDFLAGS) 

server.o: server.c stream.c
	${CC} $(CFLAGS) -c server.c stream.c	
	
client: client.o stream.o
	$(CC) $(CFLAGS) client.o stream.o -o client $(LDFLAGS) 

client.o: client.c stream.c
	${CC} $(CFLAGS) -c client.c stream.c		

stream.o: stream.c
	${CC} $(CFLAGS) -c stream.c	
