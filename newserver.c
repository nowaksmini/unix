#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>
#include <pthread.h>
#include <string.h>
#include <sys/stat.h> 
#include <inttypes.h>
#define ERR(source) (perror(source),\
		     fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
		     exit(EXIT_FAILURE))
#define HERR(source) (fprintf(stderr,"%s(%d) at %s:%d\n",source,h_errno,__FILE__,__LINE__),\
		     exit(EXIT_FAILURE))
#define CHUNKSIZE 576
#define FILENAME 50

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


#define DOWNLOADRESPONSEERROR "No such file\n"
#define DOWNLOADRESPONSESUCCESS "Registered request to downolad file successfully"
#define REGISTERRESPONSESUCCESS "Registered client successfully"
#define DELETERESPONSESUCCESS "Registered request to delete file successfully"
#define LISTRESPONSESUCCESS "Registered request to list all files successfully"
#define UPLOADRESPONSESUCCESS "Registered request to upload file to server successfully"


typedef enum {REGISTER, DOWNLOAD, UPLOAD, DELETE, LIST, REGISTERRESPONSE, DOWNLOADRESPONSE, 
  UPLOADROSPONSE, DELETERESPONSE, LISTRESPONSE, ERROR, NONE} task_type;

volatile sig_atomic_t work = 1;
volatile sig_atomic_t id = 0;


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
	fprintf(stderr, "USAGE: %s port \n",name);
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


void put_id_to_message(char * buf, uint32_t* id_message)
{
    int i;  
    for(i = sizeof(uint32_t)/sizeof(char); i < 2*sizeof(uint32_t)/sizeof(char); i++) 
    { 
	buf[i] = ((char*)&id_message)[i];  
    }
    fprintf(stderr, "Put id %"PRIu32" to message \n", *id_message);
}

/*
 * puts value always on position 2*sizeof(uint32_t)/sizeof(char) after id of message transaction
 */
void put_value_int_to_message(uint32_t value, char* buf)
{
    int i;  
    for(i = 2*sizeof(uint32_t)/sizeof(char); i < 3*sizeof(uint32_t)/sizeof(char); i++) 
    { 
	buf[i] = ((char*)&value)[i];  
    }
    fprintf(stderr, "put value %d to message \n", value);
}

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
 * sending message
 */
int send_message (int socket, struct sockaddr_in client_addr, char* message, char* message_type, task_type task)
{
  char tmp[CHUNKSIZE];
  int port = client_addr.sin_port;
  fprintf(stderr, "Client port %d \n", port);
  fprintf(stderr, "Trying to sent message %s \n", message_type);
  /*
   * int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
   */
  
  if(TEMP_FAILURE_RETRY(sendto(socket, message, CHUNKSIZE, 0, &client_addr, sizeof(struct sockaddr_in))) < 0)
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
  if(task == REGISTERRESPONSE || task == ERROR)
  {
      strcpy(tmp, message + sizeof(uint32_t)/sizeof(char));
  }
  else
  {
      /* first four bytes is enym task_type, next four are id */
      strcpy(tmp, message + 2*sizeof(uint32_t)/sizeof(char));
  }
  fprintf(stderr, "Real message send =  %s  \n", tmp);
  return 0;
}

/*
 * receiving message and remembering client address
 */
int receive_message (int socket, struct sockaddr_in* received_client_addr, char* message)
{
	task_type task;
	char * message_type = ERRORSTRING;
	char tmp[CHUNKSIZE];
	fprintf(stderr, "Trying to receive message\n");
	/*
	* int sockfd, const void *buf, size_t len, int flags,
		    const struct sockaddr *dest_addr, socklen_t addrlen);
	*/
	socklen_t size = sizeof(struct sockaddr_in);
	if(TEMP_FAILURE_RETRY(recvfrom(socket, message, CHUNKSIZE, 0, received_client_addr, &size)) < 0)
	{
	      fprintf(stderr, "Failed receving message\n");
	      return -1;
	}
	/*
	* success
	*/
	task = check_message_type(message);
	if(task == REGISTER)
	{
	    message_type = REGISTERSTRING;
	}
	else if(task == DOWNLOAD)
	{
	    message_type = DOWNLOADSTRING;
	}
	else if(task == UPLOAD)
	{
	    message_type = UPLOADSTRING;
	}
	else if(task == LIST)
	{
	    message_type = LISTSTRING;
	}
	else 
	  return -1;
	fprintf(stderr, "Received message %s succeeded\n", message_type);
	strcpy(tmp, message + sizeof(uint32_t)/sizeof(char));
	fprintf(stderr, "Real message received = %s \n", tmp);
	return 0;
}

/*
 * used after receiving request for downloading file
 * in message first four bytes are message type, next are filename
 */
void convert_message_to_file_path(char* message, char* filepath)
{
    int i = sizeof(uint32_t)/sizeof(char);
    for(i = sizeof(uint32_t)/sizeof(char); i < FILENAME + sizeof(uint32_t)/sizeof(char); i++)
    {
	filepath[i-sizeof(uint32_t)/sizeof(char)] = message[i];
    }
}


/*
 * function for responding for download request,
 * checks if file exists, sends information about file size (DOWNLOADRESPONSE, FILESIZE, DOWNLOADSUCCESS)
 * or (DOWNLOADRESPONSE, -1, DOWNLOADFAILED).
 * if file exists tries to open if and than reads from it and sends parts to client
 * (DOWNLOAD, Number_of_part, data);
 */
void readfile(char* messagein, int socket, struct sockaddr_in client_addr)
{
	/*int fd; file desriptor */
	struct stat sts;
	char filepath[FILENAME];
	char message[CHUNKSIZE];
	int type = (int)DOWNLOADRESPONSE;
	int size = 0;
	uint32_t message_id = 0;
	convert_message_to_file_path(messagein, filepath);
	memset(message, 0, CHUNKSIZE);
	memset(filepath, 0, FILENAME);
	convert_int_to_char_array(type, message);

	
	if (stat(filepath, &sts) == -1 && errno == ENOENT)
	{
	  /* no such file, id is 0 */
	  put_id_to_message(message, &message_id);
	  strcpy(message + 2*sizeof(uint32_t)/sizeof(char), DOWNLOADRESPONSEERROR);
	}
	else
	{
	   size = sts.st_size;
	   id = id + 1;
	   put_id_to_message(message,(uint32_t*) &id);
	   put_value_int_to_message(size, message);
	   strcpy(message + 3*sizeof(uint32_t)/sizeof(char), DOWNLOADRESPONSESUCCESS);
	   
	}
	send_message(socket, client_addr, message, DOWNLOADRESPONSESTRING, DOWNLOAD);

	/*if ((fd = TEMP_FAILURE_RETRY(open(filepath, O_RDONLY))) == -1)
	{
		fprintf(stderr, "Could not open file %s \n", filepath);
	}
	else
	{
		if ((size = bulk_read(fd, buffer, CHUNKSIZE)) == -1)
			ERR("read");
	}
	if (TEMP_FAILURE_RETRY(sendto(clientfd, buffer, CHUNKSIZE, 0, &addr, sizeof(addr))) < 0 && errno != EPIPE)
		ERR("write");*/
}
/*
 * create socket, connection
 */
int make_socket()
{
	int t = 1;
	int sock;
	/*
	 * AF_INET - connection through internet
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
	
	return sock;
}

/*
 * create ip scructure from ip written in char array and port
 */
struct sockaddr_in make_address(uint16_t port)
{
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	/*
	* receving from anybody
	*/
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	return addr;
}

/*
 * create socket and return new socket for address for listening
 */
int connect_socket( struct sockaddr_in address)
{
	int socketfd;
	socketfd = make_socket();
	if(bind(socketfd, (struct sockaddr*) &address, sizeof(address)) < 0)
	  ERR("bind");
	return socketfd;
}

/*
 * message send to client to inform about server address
 */
void generate_register_response_message(char* message)
{
	int type = (int)REGISTERRESPONSE;
	memset(message, 0, CHUNKSIZE);
	convert_int_to_char_array(type, message);
	strcpy(message + sizeof(uint32_t)/sizeof(char), REGISTERRESPONSESUCCESS);
}

/*
 * message send to client to inform about existing or not file to download,
 * generating id for new task and informing about file size
 */
void generate_downolad_response_message(char* message)
{
	int type = (int)DOWNLOADRESPONSE;
	memset(message, 0, CHUNKSIZE);
	convert_int_to_char_array(type, message);
	id = id + 1;
	put_id_to_message(message,(uint32_t*) &id);
	strcpy(message + 2*sizeof(uint32_t)/sizeof(char), DOWNLOADRESPONSESUCCESS);
}

/*
 * message send to client to inform about receiving new file size, md5
 * generating new task id
 */
void generate_upload_response_message(char* message)
{
	int type = (int)UPLOADROSPONSE;
	memset(message, 0, CHUNKSIZE);
	convert_int_to_char_array(type, message);
	id = id + 1;
	put_id_to_message(message,(uint32_t*) &id);
	strcpy(message + 2*sizeof(uint32_t)/sizeof(char), UPLOADRESPONSESUCCESS);
}

/*
 * message send to client to inform about ability to delete file
 * generating new task id
 */
void generate_delete_response_message(char* message)
{
	int type = (int)DELETERESPONSE;
	memset(message, 0, CHUNKSIZE);
	convert_int_to_char_array(type, message);
	id = id + 1;
	put_id_to_message(message,(uint32_t*) &id);
	strcpy(message + 2*sizeof(uint32_t)/sizeof(char), DELETERESPONSESUCCESS);
}

/*
 * message send to client to inform about receving listing files request
 * generating new task id
 */
void generate_list_response_message(char* message)
{
	int type = (int)LISTRESPONSE;
	memset(message, 0, CHUNKSIZE);
	convert_int_to_char_array(type, message);
	id = id + 1;
	put_id_to_message(message, (uint32_t*)&id);
	strcpy(message + 2*sizeof(uint32_t)/sizeof(char), LISTRESPONSESUCCESS);
}

/*
 * main thread fnction receiving all communicated and creating new threads
 */
void do_work(int socket)
{
    char message[CHUNKSIZE];
    task_type task;
    struct sockaddr_in client_addr;
    while(work)
	  {
	      if(receive_message(socket, &client_addr, message) < 0)
	      {
		perror("Receiving message \n");
	      }
	      else
	      {
		  task = check_message_type(message);
		  if(task == REGISTER)
		  {
		      generate_register_response_message(message);
		      if(send_message(socket, client_addr, message, REGISTERRESPONSESTRING, REGISTERRESPONSE) < 0)
		      {
			ERR("SEND REGISTERRESPONSE");
		      }
		  }
		  else if(task == DOWNLOAD)
		  {
		    
		      readfile(message, socket, client_addr);
		      /*generate_downolad_response_message(message);
		      if(send_message(socket, client_addr, message, DOWNLOADRESPONSESTRING, DOWNLOADRESPONSE) < 0)
		      {
			ERR("SEND DOWNLOADRESPONSE");
		      }*/
		      
		  }
		  else if(task == LIST)
		  {
		      generate_downolad_response_message(message);
		      if(send_message(socket, client_addr, message, LISTRESPONSESTRING, LISTRESPONSE) < 0)
		      {
			ERR("SEND LISTRESPONSE");
		      }
		  }
		  else if(task == UPLOAD)
		  {
		      generate_downolad_response_message(message);
		      if(send_message(socket, client_addr, message, UPLOADRESPONSESTRING, UPLOADROSPONSE) < 0)
		      {
			ERR("SEND UPLOADDRESPONSE");
		      }
		  }
		  else if(task == DELETE)
		  {
		      generate_downolad_response_message(message);
		      if(send_message(socket, client_addr, message, DELETERESPONSESTRING, DELETERESPONSE) < 0)
		      {
			ERR("SEND DELETERESPONSE");
		      }
		  }
		  else
		  {
		      fprintf(stderr, "Else : %s \n", message + sizeof(uint32_t)/sizeof(char));
		  }
	      }
	  }
	  
	  if(TEMP_FAILURE_RETRY(close(socket)) < 0)
	    ERR("CLOSE");
	  
	  fprintf(stderr,"Server has terminated.\n");
}


int main(int argc, char **argv)
{
	/*
	 * my_endpoint_listening_addr - adress for listening from everybody
	 */
	struct sockaddr_in  my_endpoint_listening_addr;
	/*
	 * socket for sending and reciving data from specified address
	 */
	int socket;
		
	if (argc!=2)
		usage(argv[0]);

	sethandler(SIG_IGN, SIGPIPE);
	sethandler(siginthandler, SIGINT);
	
	my_endpoint_listening_addr = make_address(atoi(argv[1]));
	
	socket = connect_socket(my_endpoint_listening_addr);
	
	do_work(socket);

	return EXIT_SUCCESS;
}