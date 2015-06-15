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

#define DOWNLOADSTRING "download"
#define UPLOADSTRING "upload"
#define DELETESTRING "remove"
#define LISTSTRING "list"
#define REGISTERSTRING "register"
#define REGISTERRESPONSESTRING "register response"
#define DOWNLOADRESPONSESTRING "download response"
#define UPLOADRESPONSESTRING "upload response"
#define DELETERESPONSESTRING "delete response"
#define LISTRESPONSESTRING "list response"
#define ERRORSTRING "error"
#define NOACTIONSTRING "no action"


#define INSTRUCTION "\nINSTRUCTUIION\nOPTIONS\ndownload file_name\nupload file_name\ndelete file_name\nlist\n\n"
#define RECEIVEDINTHREAD "Received in thread" 

#define CLIENTREQUESTS "Client requests registration"
#define FILENAME 50		     
#define CHUNKSIZE 576

typedef enum {REGISTER, DOWNLOAD, UPLOAD, DELETE, LIST, REGISTERRESPONSE, DOWNLOADRESPONSE, 
  UPLOADROSPONSE, DELETERESPONSE, LISTRESPONSE, ERROR, NONE} task_type;


volatile sig_atomic_t work = 1;

typedef struct
{
	int id;
	int* socket;
	struct sockaddr_in *server_addr;
	pthread_mutex_t *mutex;
} thread_arg;

/*
 * function responsible for handling SIGINT signal 
 */
void siginthandler(int sig)
{
	work = 0;
}

/*
 * inform user about running program
 */
void usage(char *name)
{
	fprintf(stderr, "USAGE: %s server_port client_port\n", name);
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
 * check only first letter because user could make some mistakes in typing word
 */
task_type get_task_type_from_input(char * message)
{
    int t = message[0];
    if(t == 'd')
      return DOWNLOAD;
    if(t == 'l')
      return LIST;
    if(t == 'r')
      return DELETE;
    if(t == 'u')
      return UPLOAD;
    return ERROR;
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
 * check task type of message (first four bytes)
 */
task_type check_message_type(char * buf)
{
	fprintf(stderr, "Checking task type \n");
	int i,type = 0; 
	for(i = 0; i < sizeof(uint32_t)/sizeof(char); i++) 
	{
	    ((char*)&type)[i] = buf[i]; 
	} 
	return (task_type)(type); 
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
	strcpy(message + sizeof(uint32_t)/sizeof(char), CLIENTREQUESTS);
}


void generate_request_download_message(char* message, char* filepath)
{
	int type = (int)DOWNLOAD;
	memset(message, 0, CHUNKSIZE);
	convert_int_to_char_array(type, message);
	strcpy(message + sizeof(uint32_t)/sizeof(char), filepath);
}


/*
 * sending message
 */
int send_message (int socket, struct sockaddr_in server_addr, char* message, char* message_type)
{
	char tmp[1000];
	fprintf(stderr, "Trying to sent message %s \n", message_type);
	/*
	* int sockfd, const void *buf, size_t len, int flags,
		    const struct sockaddr *dest_addr, socklen_t addrlen);
	*/
	if(TEMP_FAILURE_RETRY(sendto(socket, message, CHUNKSIZE, 0, &server_addr, sizeof(struct sockaddr_in))) < 0)
	{
	  /*
	  * failure
	  */
	  fprintf(stderr, "Sending message %s failed \n", message_type);
	  return -1;
	}
	/*
	* success
	*/
	fprintf(stderr, "Sending message %s succeeded \n", message_type);
	strcpy(tmp, message + sizeof(uint32_t)/sizeof(char));
	fprintf(stderr, "Real message send =  %s  \n", tmp);
	return 0;
}

/*
 * receiving message
 */
task_type receive_message (int socket, struct sockaddr_in* received_server_addr, char* message)
{
	task_type task = NONE;
	char * message_type = NOACTIONSTRING;
	char tmp[CHUNKSIZE];
	fprintf(stderr, "\n Trying to receive message \n");
	
	/*
	* int sockfd, const void *buf, size_t len, int flags,
		    const struct sockaddr *dest_addr, socklen_t addrlen);
	*/
	socklen_t size = sizeof(struct sockaddr_in);
	if(recvfrom(socket, message, CHUNKSIZE, 0, received_server_addr, &size) < 0)
	{
	  if(EINTR != errno)
	  {
	    fprintf(stderr, "Failed receving message \n");
	    return ERROR;
	  }
	}
	/*
	* success
	*/
	fprintf(stderr, "Preparing for parcing received message \n");
	task = check_message_type(message);
	if(task == REGISTERRESPONSE)
	{
	  message_type = REGISTERRESPONSESTRING;
	}
	else if(task == DOWNLOADRESPONSE)
	{
	  /* got md5 of file name */
	  message_type = DOWNLOADRESPONSESTRING;
	}
	else if(task == DELETERESPONSE)
	{
	  message_type = DELETERESPONSESTRING;
	}
	else if(task == UPLOADROSPONSE)
	{
	  message_type = UPLOADRESPONSESTRING;
	}
	if(task == REGISTER)
		strcpy(tmp, message + sizeof(uint32_t)/sizeof(char));
	else 
		strcpy(tmp, message + 3*sizeof(uint32_t)/sizeof(char));
	fprintf(stderr, "Real message received = %s \n", tmp);
	fprintf(stderr, "Received message %s succeeded\n", message_type);
	return task;
}


/*
 * thread function for listening  anything from 
 */
void server_listening_work(int socket, struct sockaddr_in server_addr)
{
	task_type task = NONE;
	char message_received[CHUNKSIZE];
	task = receive_message(socket, &server_addr, message_received);
	if(task == ERROR)
	{
	   perror("Receiving message ");
	}
	else if(task == DELETERESPONSE)
	{
	    fprintf(stderr, "%s: %s", RECEIVEDINTHREAD, DELETERESPONSESTRING);
	    /*
	     * Open new thread with given task id, wait inside for DELETE message
	     */
	}
	else if(task == UPLOADROSPONSE)
	{
	    fprintf(stderr, "%s : %s", RECEIVEDINTHREAD, UPLOADRESPONSESTRING);
	}
	else if(task == DOWNLOADRESPONSE)
	{
	    fprintf(stderr, "%s : %s", RECEIVEDINTHREAD, DOWNLOADRESPONSESTRING);
	}
	else if(task == LISTRESPONSE)
	{
	    fprintf(stderr, "%s : %s", RECEIVEDINTHREAD, LISTRESPONSESTRING);
	}
	/*
	 * for task UPLOAD, DELETE, LIST, UPDATE push message to global queue and threads will know what to do
	 */
    
}

/*
 * thread function for listening  anything from 
 */
void input_listening_work(int socket, struct sockaddr_in server_addr)
{
	task_type task = NONE;
	int max = 9 + FILENAME;
	char input_message[max];
	char filepath[FILENAME];
	char request[20];
	char message_to_send[CHUNKSIZE];
	
	fgets(input_message, max, stdin);
	task = get_task_type_from_input(input_message);
	if(task == DOWNLOAD)
	{
	    if(sscanf(input_message, "%s %s", request, filepath) == EOF)
	    {
		fprintf(stderr, "PROBLEM WITH SSCANF");
	    }
	    else
	    {	
	        fprintf(stderr, "Got task type %s \n", request);
		fprintf(stderr, "Got file name %s  \n", filepath);
		generate_request_download_message(message_to_send, filepath);
		if(send_message(socket, server_addr, message_to_send, DOWNLOADSTRING) < 0)
		{	
		   ERR("SEND");
		}
	    }
	}
    
}


void *server_listen_thread_function(void *arg)
{
	int clientfd;
	struct sockaddr_in server_addr;
	thread_arg targ;

	memcpy(&targ, arg, sizeof(targ));

	while (work)
	{
		clientfd = *targ.socket;
		server_addr = *targ.server_addr;
		server_listening_work(clientfd, server_addr);
	}
	pthread_exit(&targ);
	return NULL;
}


void *input_listen_thread_function(void *arg)
{
	int clientfd;
	struct sockaddr_in server_addr;
	thread_arg targ;

	memcpy(&targ, arg, sizeof(targ));

	while (work)
	{
		clientfd = *targ.socket;
		server_addr = *targ.server_addr;
		input_listening_work(clientfd, server_addr);
	}
	pthread_exit(&targ);
	return NULL;
}


void init(pthread_t *thread, thread_arg *targ,  pthread_mutex_t *mutex, int *socket, struct sockaddr_in* server_addr)
{
	int i;

	for (i = 0; i < 2; i++)
	{
		targ[i].id = i - 2; /* there won't be ane conflicts with ids from server */
		targ[i].mutex = mutex;
		targ[i].socket = socket;
		targ[i].server_addr = server_addr;
		if(i == 0)
		{
		    if (pthread_create(&thread[i], NULL, input_listen_thread_function, (void *) &targ[i]) != 0)
			ERR("pthread_create");
		}
		else
		{
		    if (pthread_create(&thread[i], NULL, server_listen_thread_function, (void *) &targ[i]) != 0)
			ERR("pthread_create");
		}
	}
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
	 * SOC_DGRAM - udp connection
	 * 0 - default protocol for UDP
	 */
	sock = socket(PF_INET, SOCK_DGRAM, 0);
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
 * used to maintain program working
 */
void do_work(int socket)
{
    while(work) 
    {
      continue;
    }

    if(TEMP_FAILURE_RETRY(close(socket)) < 0)
	  ERR("CLOSE");
    fprintf(stderr,"Client has terminated.\n");
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
	task_type task = NONE;
	char message[CHUNKSIZE];
	
	pthread_t thread[2];
	thread_arg targ[2];
	pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
	
	if (argc!=3)
		usage(argv[0]);
	
	fprintf(stdout,"%s", INSTRUCTION);

	sethandler(SIG_IGN, SIGPIPE);
	sethandler(siginthandler, SIGINT);
	

	generate_register_message(message);

	
	my_endpoint_listening_addr = make_address(NULL, atoi(argv[2]) , 0);
	socket = connect_socket(0, my_endpoint_listening_addr);
	broadcast_adrr = make_address(NULL, atoi(argv[1]), 1);
	broadcastsocket = connect_socket(1, my_endpoint_listening_addr);

	if(send_message(broadcastsocket, broadcast_adrr, message, REGISTERSTRING) < 0)
	{
	  ERR("SEND");
	}
	
	while(task != REGISTERRESPONSE)
	{
	  task = receive_message(broadcastsocket, &server_addr, message);
	  if(task == ERROR)
	    ERR("RECEIVE");
	}
	
	if(TEMP_FAILURE_RETRY(close(broadcastsocket)) < 0)
	  ERR("CLOSE");
	
	init(thread, targ, &mutex, &socket, &server_addr);
	
	do_work(socket);
	
	return EXIT_SUCCESS;
}

