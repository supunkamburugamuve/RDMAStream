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

stream_verbs: stream_verbs.o
	$(CC) $(CFLAGS) stream_verbs.o -o stream_verbs $(LDFLAGS) 

stream_verbs.o: stream_verbs.c
	${CC} -c stream_verbs.c

server: server.o stream.o
	$(CC) $(CFLAGS) server.o stream.o -o server $(LDFLAGS) 

server.o: server.c stream.c
	${CC} -c server.c stream.c	
	
client: client.o stream.o
	$(CC) $(CFLAGS) client.o stream.o -o client $(LDFLAGS) 

client.o: client.c stream.c
	${CC} -c client.c stream.c		

strea.o: stream.c
	${CC} -c stream.c	
