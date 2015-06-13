#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <signal.h>
#include <netdb.h>
#include <pthread.h>
#define ERR(source) (perror(source),\
		     fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
		     exit(EXIT_FAILURE))

#define HERR(source) (fprintf(stderr,"%s(%d) at %s:%d\n",source,h_errno,__FILE__,__LINE__),\
		     exit(EXIT_FAILURE))

#define CHUNKSIZE 576

typedef enum {REGISTER} task_type;


volatile sig_atomic_t work = 1;
volatile sig_atomic_t last_signal = 0 ;


/*
 * function responsible for handling SIGINT signal 
 */
void siginthandler(int sig)
{
	work = 0;
}

void sigalrm_handler(int sig) 
{
	last_signal = sig;
}


/*
 * inform user about running program
 */
void usage(char *name)
{
	fprintf(stderr, "USAGE: %s port\n", name);
	exit(EXIT_FAILURE);
}

/*
 * set handler for specified signal
 */
void sethandler(void (*f)(int), int sigNo)
{
	struct sigaction act;
	/*
	 * clear handler for number of signal sigNo
	 */
	memset(&act, 0x00, sizeof(struct sigaction));
	/*
	 * set new handler
	 */
	act.sa_handler = f;

	/*
	 * check if setting handler for  signal sigNo failed
	 * do not remember old handler
	 */
	if (-1 == sigaction(sigNo, &act, NULL))
		ERR("sigaction");
}

/*
 * buf - memory to write data read from file, must be allocated for minimum count size
 * fd - descriptor of file
 * count - amount of bytes to read
 */
ssize_t bulk_read(int fd, char *buf, size_t count)
{
	/*
	 * amount of read bytes in one step
	 */
	int c;
	size_t len = 0;
	do
	{
		/*
		 * if there was error during reading return -1
		 * if signal interruped reading try again
		 */
		c = TEMP_FAILURE_RETRY(read(fd, buf, count));
		if (c < 0)
			return c;
		/*
		 * end of file
		 */
		if (c == 0)
			return len;
		/*
		 * move iterator c signs right
		 */
		buf += c;
		/*
		 * add already read bytes
		 */
		len += c;
		/*
		 * decrease amount of bytes to read
		 */
		count -= c;
	}
	while (count > 0);

	/*
	 * return amount of read bytes
	 */
	return len;
}

/*
 * buf - memory to data written to file
 * fd - descriptor of file
 * count - amount of bytes to write
 */
ssize_t bulk_write(int fd, char *buf, size_t count)
{
	/*
	 * amount of written bytes in one step
	 */
	int c;
	size_t len = 0;

	do
	{
		/*
		 * if there was error during writting return -1
		 * if signal interruped writting try again
		 */
		c = TEMP_FAILURE_RETRY(write(fd, buf, count));
		if(c < 0)
			return c;
		/*
		 * move iterator c signs right
		 */
		buf += c;
		/*
		 * add already written bytes
		 */
		len += c;
		/*
		 * decrease amount of bytes to write
		 */
		count -= c;
	}
	while (count > 0);

	/*
	 * return amount of read written
	 */
	return len;
}

/*
 * create socket, connection
 * if bradcastenable == 1 than should broadcast
 */
int make_socket(int broadcastEnable)
{
	int sock;
	int t = 1;
	/*
	 * AF_INET - connection through internet
	 * SOC_DGRAM - udp connection
	 * 0 - default protocol for UDP
	 */
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		ERR("socket");
	/*
	 * enables binding many times to same port
	 */
	if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t))) 
	      ERR("setsockopt");
	/*
	 * set socket options
	 * SOL_SOCKET - level of setting options, (socket)
	 * SO_BROADCAST - cocket can receive and send for broadcast 
	 * broadcastenable - set option for true
	 */
	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)))
		ERR("setsockopt");
	return sock;
}

/*
 * create ip scructure from ip written in char array and port
 * if address null than address should be any
 * broadcast means if we should bradcast
 */
struct sockaddr_in make_address(char *address, uint16_t port, int broadcast)
{
	struct sockaddr_in addr;
	struct hostent *hostinfo;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	
	if(address == NULL)
	{
	  if(broadcast == 0)
	  {
	    /*
	    * receving from anybody
	    */
	    addr.sin_addr.s_addr = htonl(INADDR_ANY);
	  }
	  else
	  {
	    /*
	     * sendidng data to whole subnet
	     */
	    addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	  }
	  
	}
	else
	{
	  /*
	   * used for sending data to specified address
	   */
	  hostinfo = gethostbyname(address);
	  if (hostinfo == NULL)
		  HERR("gethostbyname");
	  addr.sin_addr = *(struct in_addr*) hostinfo->h_addr;
	}
	
	return addr;
}

/*
 * create socket and return new socket for address for listening
 */
int connect_socket(int broadcastEnable, struct sockaddr_in address)
{
	int socketfd;
	socketfd = make_socket(broadcastEnable);
	if(bind(socketfd, (struct sockaddr *) &address, sizeof(address)) < 0)
	  ERR("bind");
	return socketfd;
}

/*
 * sending message
 */
int send_message (int socket, struct sockaddr_in server_addr, char* message)
{
  fprintf(stderr, "Trying to sent message %s \n", message);
  /*
   * int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
   */
  if(TEMP_FAILURE_RETRY(sendto(socket, message, CHUNKSIZE, 0, &server_addr, sizeof(struct sockaddr_in))) < 0)
  {
    /*
     * failure
     */
    fprintf(stderr, "Sending message %s failed \n", message);
    return -1;
  }
  /*
   * success
   */
  fprintf(stderr, "Sending message %s succeeded \n", message);
  return 0;
}

/*
 * receiving message
 */
int receive_message (int socket, struct sockaddr_in* received_server_addr, char* message)
{
  fprintf(stderr, "Trying to receive message \n");
  /*
   * int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
   */
  socklen_t size = sizeof(struct sockaddr_in);
  while(recvfrom(socket, message, CHUNKSIZE, 0, received_server_addr, &size) < 0)
	{
	      if(EINTR != errno)
	      {
		 fprintf(stderr, "Failed receving message \n");
		 return -1;
	      }
	      if(SIGALRM == last_signal)
	      {
		  fprintf(stderr, "SIG ALARM \n");
		  break;
	      }
	}
  /*
   * success
   */
  fprintf(stderr, "Received message %s succeeded \n", message);
  return 0;
}

/*
 * write first four bytesto array
 */
void convert_int_to_char_array(int argument, char* buf)
{
	int i;
	for(i = 0; i < sizeof(uint32_t)/sizeof(char); i++) 
	{ 
	    buf[i] = ((char*)&argument)[i];  
	}
	fprintf(stderr, "Converted data %s \n", buf);
}


void generate_register_message(char* message)
{
	int type = (int)REGISTER;
	memset(message, 0, CHUNKSIZE);
	convert_int_to_char_array(type, message);
}



int main(int argc, char **argv)
{
	/*
	 * my_endpoint_listening_addr - adress for listening from everybody
	 * broadcast_adrr - addres for sending to everybody
	 * server_addr - server address
	 */
	struct sockaddr_in server_addr, broadcast_adrr, my_endpoint_listening_addr;
	/*
	 * socket for sending and reciving data from specified address
	 */
	int socket;
	/*
	 * socket for sending broadcast
	 */	
	int broadcastsocket;
	char message[CHUNKSIZE];
	
	generate_register_message(message);
	
	if (argc!=2)
		usage(argv[0]);

	sethandler(SIG_IGN, SIGPIPE);
	sethandler(siginthandler, SIGINT);
	sethandler(sigalrm_handler, SIGALRM);

	
	broadcastsocket = make_socket(1);
	
	my_endpoint_listening_addr = make_address(NULL, atoi(argv[1]), 0);
	socket = connect_socket(0, my_endpoint_listening_addr);
	broadcast_adrr = make_address(NULL, atoi(argv[1]), 1);
	
	if(send_message(broadcastsocket, broadcast_adrr, message) < 0)
	{
	  ERR("SEND");
	}
		
	if(receive_message(socket, &server_addr, message) < 0)
	{
	  ERR("RECEIVE");
	}
	
	return EXIT_SUCCESS;
}

