
all: client server 

client: newclient.c library.c 
	gcc -Wall -o library.o library.c -lpthread -lcrypto -lrt -c 
	gcc -Wall -o client.o newclient.c -lpthread -lcrypto -lrt -c 
	gcc -o client library.o client.o -lpthread -lcrypto -lrt
server: newserver.c library.c
	gcc -Wall -o library.o library.c -lpthread -lcrypto -lrt -c 
	gcc -Wall -o server.o newserver.c -lpthread -lcrypto -lrt -c 
	gcc -o server library.o server.o -lpthread -lcrypto -lrt

.PHONY: clean 

clean: 
	-rm client server client.o library.o