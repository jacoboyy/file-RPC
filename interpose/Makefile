all: mylib.so server

mylib.o: mylib.c
	gcc -Wall -fPIC -DPIC -c mylib.c -I../include -L../lib

util.o: util.c
	gcc -Wall -o util.o -c util.c

mylib.so: mylib.o util.o
	ld -shared -o mylib.so mylib.o util.o -ldl -I../include -L../lib

server: server.c util.o
	gcc -Wall server.c util.o -o server -I../include -L../lib -ldirtree -ldl 

clean:
	rm -f *.o *.so server

