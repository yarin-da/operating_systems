all: test clean

test: threadPool.o
	gcc test.c threadPool.o osqueue.o -lpthread

threadPool.o: osqueue.o
	gcc -c threadPool.c

osqueue.o:
	gcc -c osqueue.c

clean: 
	rm -f *.o