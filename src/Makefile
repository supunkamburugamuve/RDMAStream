CC=gcc
CFLAGS=-Wall -g
#LDFLAGS= -lrdmacm -libverbs
LDFLAGS= -libverbs

all: rdma

rdma: rdma.o
	$(CC) $(CFLAGS) rdma.o -o rdma $(LDFLAGS)	

rdma.o: rdma.c
	${CC} -c rdma.c

clean_rdma:
	rm -f rdma rdma.o

stream: stream_verbs.o
	$(CC) $(CFLAGS) stream_verbs.o -o stream_verbs $(LDFLAGS) 

stream.o: stream_verbs.c
	${CC} -c stream_verbs.c

server: server.o common.o
	$(CC) $(CFLAGS) server.o common.o -o server $(LDFLAGS) 

server.o: server.c
	${CC} -c stream_verbs.c	
	
client: client.o common.o
	$(CC) $(CFLAGS) client.o common.o -o client $(LDFLAGS) 

client.o: client.c
	${CC} -c client.c		

common.o: common.c
	${CC} -c common.c	