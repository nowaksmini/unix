all: client server
client: newclient.c
	gcc -Wall -o client newclient.c -lpthread
server: newserver.c	
	gcc -Wall -o server newserver.c -lpthread -lcrypto -lrt
.PHONY: clean
clean:
	-rm client server
