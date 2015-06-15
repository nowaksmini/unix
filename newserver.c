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
#include <openssl/md5.h>
#define ERR(source) (perror(source),\
		     fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
		     exit(EXIT_FAILURE))
#define HERR(source) (fprintf(stderr,"%s(%d) at %s:%d\n",source,h_errno,__FILE__,__LINE__),\
		     exit(EXIT_FAILURE))
#define CHUNKSIZE 576
#define FILENAME 50
#define MD5LENGTH 200

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

#define QUEUE_NAME  "/serverqueue"

#define CHECK(x) \
    do { \
        if (!(x)) { \
            fprintf(stderr, "%s:%d: ", __func__, __LINE__); \
            perror(#x); \
            exit(-1); \
        } \
    } while (0) \

typedef struct Queue
{
        int capacity;
        int size;
	int busy;
        char* elements;
}Queue;

    
typedef enum {REGISTER, DOWNLOAD, UPLOAD, DELETE, LIST, REGISTERRESPONSE, DOWNLOADRESPONSE, 
  UPLOADROSPONSE, DELETERESPONSE, LISTRESPONSE, ERROR, NONE} task_type;

volatile sig_atomic_t work = 1;
volatile sig_atomic_t id = 0;
Queue* queue;

typedef struct
{
	int id;
	int* socket;
	struct sockaddr_in *server_addr;
	pthread_mutex_t *mutex;
} thread_arg;


/* crateQueue function takes argument the maximum number of elements the Queue can hold, creates
   a Queue according to it and returns a pointer to the Queue. */
Queue * createQueue(int maxElements)
{
        /* Create a Queue */
        Queue *Q;
        Q = (Queue *)malloc(sizeof(Queue));
        /* Initialise its properties */
        Q->elements = (char *)malloc(sizeof(char)*maxElements*CHUNKSIZE);
        Q->size = 0;
        Q->capacity = maxElements;
        /* Return the pointer */
        Q->busy = 0;
        return Q;
}

void push(Queue* queue, char* message)
{
	int i;
	int move = (queue->size) * CHUNKSIZE;
	queue->busy = 1;
	if(queue->size == queue->capacity)
	{
	    fprintf(stderr, "Too many elements in queue \n");
	}
	else
	{
	    for(i = move; i < CHUNKSIZE + move; i++)
	    {
	      queue->elements[i] = message[i - move];
	    }
	    fprintf(stderr, "Successfully copied message = %s to queue \n", message);
	    queue->size = queue->size + 1;
	}
	queue->busy = 0;
}

void top(Queue* queue, char* message)
{
	int i;
	queue->busy = 1;
	if(queue->size == 0) return;
	char messages[queue->capacity];
	memset(messages, 0, queue->capacity);
	for(i = 0; i< CHUNKSIZE; i++)
	{
	  message[i] = queue->elements[i];
	}
	for(i = CHUNKSIZE; i< queue->capacity; i++)
	{
	  messages[i - CHUNKSIZE] = queue->elements[i];
	}
	for(i = 0; i< queue->capacity; i++)
	{
	   queue->elements[i] = messages[i];
	}
	queue->size = queue->size -1;
	queue->busy = 0;
}

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
 * str = whole file data
 * sum = output counted
 */
void compute_md5(char *str, unsigned char * sum) {
  
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, str, strlen(str));
    MD5_Final(sum, &ctx);
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
      /* first four bytes is enum task_type, next four are id, next size or part of file*/
      strcpy(tmp, message + 3*sizeof(uint32_t)/sizeof(char));
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
	if(recvfrom(socket, message, CHUNKSIZE, 0, received_client_addr, &size) < 0)
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
    int i;
    for(i = 0; i < FILENAME; i++)
    {
	filepath[i] = message[i + sizeof(uint32_t)/sizeof(char)];
	/*fprintf(stderr, "File char read from incomming message = %c \n", filepath[i]); */
    }
}


/*
 * This routine returns the size of the file it is called with. 
 */

static unsigned get_file_size (const char * file_name)
{
    struct stat sb;
    if (stat (file_name, & sb) != 0) {
        fprintf (stderr, "'stat' failed for '%s': %s.\n",
                 file_name, strerror (errno));
        return -1;
    }
    return sb.st_size;
}

/* This routine reads the entire file into memory. */

char * read_whole_file (const char * file_name)
{
    unsigned s;
    char * contents;
    FILE * f;
    size_t bytes_read;
    int status;

    s = get_file_size (file_name);
    if(s == -1) return NULL;
    contents = malloc (s + 1);
    if (! contents) {
        fprintf (stderr, "Not enough memory.\n");
        return NULL;
    }

    f = fopen (file_name, "r");
    if (! f) {
        fprintf (stderr, "Could not open '%s': %s.\n", file_name, strerror (errno));
        return NULL;
    }
    bytes_read = fread (contents, sizeof (unsigned char), s, f);
    if (bytes_read != s) {
        fprintf (stderr, "Short read of '%s': expected %d bytes "
                 "but got %zu: %s.\n", file_name, s, bytes_read,
                 strerror (errno));
        return NULL;
    }
    status = fclose (f);
    if (status != 0) {
        fprintf (stderr, "Error closing '%s': %s.\n", file_name,
                 strerror (errno));
        return NULL;
    }
    return contents;
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
	int fd; /*file desriptor */
	int i;
	struct stat sts;
	char filepath[FILENAME];
	char message[CHUNKSIZE];
	char * real_file_name;
	unsigned char md5_sum[MD5LENGTH];
	int type = (int)DOWNLOADRESPONSE;
	int size = 0;
	int first_empty_sign = FILENAME - 1;
	uint32_t message_id = 0;
	int tmp_id;
	int package_amount;
	char * file_contents;
	int real_package_size = CHUNKSIZE - 3 * sizeof(uint32_t)/sizeof(char);
	char *package;
	package = malloc(real_package_size);
	memset(message, 0, CHUNKSIZE);
	memset(filepath, 0, FILENAME);
	memset(md5_sum, 0, MD5LENGTH);
	convert_message_to_file_path(messagein, filepath);
	convert_int_to_char_array(type, message);

	for(i = 0; i < FILENAME; i++)
	{
	    if(filepath[i] == '\0')
	    {
	      first_empty_sign = i;
	      break;
	    }
	}
	fprintf(stderr, "Empty sign found at position : %d \n", first_empty_sign);
	real_file_name = malloc(first_empty_sign);
	memset(real_file_name, 0, first_empty_sign);
	for(i = 0; i < first_empty_sign; i++)
	{
	    real_file_name[i] = filepath[i];
	}
	
	fprintf(stderr, "Real file name : %s \n", real_file_name);
	
	if (stat(real_file_name, &sts) == -1 && errno == ENOENT)
	{
	  /* no such file, id is 0 */
	  put_id_to_message(message, &message_id);
	  strcpy(message + 2*sizeof(uint32_t)/sizeof(char), DOWNLOADRESPONSEERROR);
	}
	else
	{
	   size = sts.st_size;
	   id = id + 1;
	   tmp_id = id;
	   if ((fd = TEMP_FAILURE_RETRY(open(real_file_name, O_RDONLY))) == -1)
	   {
		  fprintf(stderr, "Could not open file %s \n", real_file_name);
		  put_id_to_message(message, &message_id);
		  strcpy(message + 2*sizeof(uint32_t)/sizeof(char), DOWNLOADRESPONSEERROR);
	   }
	   else
	   {
		  put_id_to_message(message,(uint32_t*) &id);
		  put_value_int_to_message(size, message);
			      
		  file_contents = read_whole_file (real_file_name);
		  compute_md5(file_contents, md5_sum);
		  
		  strcpy(message + 3*sizeof(uint32_t)/sizeof(char), DOWNLOADRESPONSESUCCESS);
	   }
	}
	send_message(socket, client_addr, message, DOWNLOADRESPONSESTRING, DOWNLOADRESPONSE);
	free(real_file_name);
	sleep(1);
	
	/*
	 * divide whole file to smaller one packages of size CHUNKSIZE - TYPE_LENGTH - TASK_ID_LENGTH - PACKAGE_NUMBER
	 */
	type = DOWNLOAD;
	convert_int_to_char_array(type, message);
	if(tmp_id != 0)
	{
	  if(strlen(file_contents) == strlen(file_contents) / real_package_size * real_package_size)
	    package_amount = strlen(file_contents) / real_package_size;
	  else 
	    package_amount = strlen(file_contents) / real_package_size + 1;
	  
	  for(i = 0; i < package_amount; i++)
	  {
	      strcpy(package, file_contents + i * real_package_size);
	      put_id_to_message(message,(uint32_t*) &tmp_id);
	      put_value_int_to_message(i, message);
	      strcpy(message + 3*sizeof(uint32_t)/sizeof(char), package);
	      fprintf(stderr, "Sending data package number %d for task id %d : %s \n", i,tmp_id, package);
	      send_message(socket, client_addr, message, DOWNLOADSTRING, DOWNLOAD);
	      sleep(1);
	  }
	  put_value_int_to_message(i, message);
	  fprintf(stderr, "Sending md5 sum for task id %d \n", tmp_id);
	  strcpy(message + 3*sizeof(uint32_t)/sizeof(char), (char*)md5_sum);
	  send_message(socket, client_addr, message, DOWNLOADSTRING, DOWNLOAD);
	  sleep(1);
	}
	free (file_contents);
	/*if ((fd = TEMP_FAILURE_RETRY(open(real_file_name, O_RDONLY))) == -1)
	{
		fprintf(stderr, "Could not open file %s \n", real_file_name);
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

void *server_send_register_response_function(void *arg)
{
	int clientfd;
	struct sockaddr_in server_addr;
	thread_arg targ;
	char message[CHUNKSIZE];
	memcpy(&targ, arg, sizeof(targ));
	task_type task = NONE;
	while (work)
	{
		/* top from queue */
		while(queue->busy)
		{
		    sleep(1);
		}
		fprintf(stderr, "Size of queue before top = %d \n", queue->size);
		top(queue, message);
		fprintf(stderr, "Size of queue after top = %d \n", queue->size);
		task = check_message_type(message);
		if(task != REGISTER)
		{
		  /* push to the end of queue */
		  fprintf(stderr, "Task was not REGISTER \n");
		  while(queue->busy)
		  {
		    sleep(1);
		  }
		  push(queue, message);
		  sleep(1);
		  continue;
		}
		else
		{
		  clientfd = *targ.socket;
		  server_addr = *targ.server_addr;
		  generate_register_response_message(message);
		  if(send_message(clientfd, server_addr, message, REGISTERRESPONSESTRING, REGISTERRESPONSE) < 0)
		  {
		    ERR("SEND REGISTERRESPONSE");
		  }
		  sleep(1);
		  break;
		}
	}
	pthread_exit(&targ);
	return NULL;
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

void init(pthread_t *thread, thread_arg *targ,  pthread_mutex_t *mutex, int *socket, struct sockaddr_in* server_addr, int i)
{
	targ[0].id = i; /* there won't be ane conflicts with ids from server */
	targ[0].mutex = mutex;
	targ[0].socket = socket;
	targ[0].server_addr = server_addr;
	if (pthread_create(&thread[0], NULL, server_send_register_response_function, (void *) &targ[0]) != 0)
		ERR("pthread_create");
}

/*
 * main thread fnction receiving all communicated and creating new threads
 */
void do_work(int socket)
{
    char message[CHUNKSIZE];
    task_type task;
    struct sockaddr_in client_addr;
    pthread_t thread;
    thread_arg targ;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    while(work)
	  {
	      if(receive_message(socket, &client_addr, message) < 0)
	      {
		fprintf(stderr, "Receiving message \n");
	      }
	      /* receive the message */
	      else
	      {
		  fprintf(stderr, "Trying to generate threads \n");
		  /* create threads and/or push message to queue */
		  task = check_message_type(message);
		  if(task == REGISTER)
		  {
		      while(queue->busy)
		      {
			sleep(1);
		      }
		      push(queue, message);
		      init(&thread, &targ,  &mutex, &socket, &client_addr, 0);
		  }
		  else if(task == DOWNLOAD)
		  {
		      readfile(message, socket, client_addr);
		  }
		  else if(task == LIST)
		  {
		      generate_list_response_message(message);
		      if(send_message(socket, client_addr, message, LISTRESPONSESTRING, LISTRESPONSE) < 0)
		      {
			ERR("SEND LISTRESPONSE");
		      }
		  }
		  else if(task == UPLOAD)
		  {
		      generate_upload_response_message(message);
		      if(send_message(socket, client_addr, message, UPLOADRESPONSESTRING, UPLOADROSPONSE) < 0)
		      {
			ERR("SEND UPLOADDRESPONSE");
		      }
		  }
		  else if(task == DELETE)
		  {
		      generate_delete_response_message(message);
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
	
	queue = createQueue(100);
	fprintf(stderr, "Created queue \n");

	do_work(socket);
	
	return EXIT_SUCCESS;
}