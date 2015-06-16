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


#define INSTRUCTION "\nINSTRUCTUIION\nOPTIONS\ndownload file_name\nupload file_name\nremove file_name\nlist\n\n"
#define RECEIVEDINTHREAD "Received in thread" 

#define CLIENTREQUESTS "Client requests registration"
#define FILENAME 50		     
#define CHUNKSIZE 576


typedef enum {REGISTER, DOWNLOAD, UPLOAD, DELETE, LIST, REGISTERRESPONSE, DOWNLOADRESPONSE, 
  UPLOADROSPONSE, DELETERESPONSE, LISTRESPONSE, ERROR, NONE} task_type;

typedef struct Queue
{
        int capacity;
        int size;
	int busy;
        char* elements;
}Queue;

Queue* queue;
volatile sig_atomic_t work = 1;

typedef struct
{
	int* socket;
	struct sockaddr_in *server_addr;
	char* filename;
} thread_arg;

void init(pthread_t *thread, thread_arg *targ, int *socket, struct sockaddr_in* server_addr, task_type task,
	  char filepath [FILENAME]);
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
	fprintf(stderr, "USAGE: %s server_port\n", name);
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
 * str = whole file data
 * sum = output counted
 */
void compute_md5(char *str, unsigned char * sum) {
  
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, str, strlen(str));
    MD5_Final(sum, &ctx);
}

/* crateQueue function takes argument the maximum number of elements the Queue can hold, creates
   a Queue according to it and returns a pointer to the Queue. */
Queue * createQueue(int maxElements)
{
        /* Create a Queue */
        Queue *Q;
        Q = (Queue *)malloc(sizeof(Queue));
        /* Initialise its properties */
        Q->elements = (char *)calloc(maxElements*CHUNKSIZE, sizeof(char));
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
	uint32_t i,Number = 0;
	for(i=0;i<sizeof(uint32_t)/sizeof(char);i++) 
	{
		((char*)&Number)[i] = buf[i];
	}
	fprintf(stderr, "Received task number  %d \n", (ntohl(Number)));
	return (task_type)(ntohl(Number));
}
/*
 * write first four bytes to array
 */
void convert_int_to_char_array(int argument, char* buf)
{
	int i;
	uint32_t Number = htonl(argument);
	for(i=0;i<sizeof(uint32_t)/sizeof(char);i++)
	{
		buf[i] = ((char*)&Number)[i];
	}	
}

/*
 * puts value always on position 2*sizeof(uint32_t)/sizeof(char) after id of message transaction
 */
void put_value_int_to_message(uint32_t value, char* buf)
{
	int i;
	uint32_t Number = htonl(value);
	for(i=0;i<sizeof(uint32_t)/sizeof(char);i++)
	{
		buf[i+2*sizeof(uint32_t)/sizeof(char)] = ((char*)&Number)[i];
	}	
}

void generate_register_message(char* message)
{
	int type = (int)REGISTER;
	memset(message, 0, CHUNKSIZE);
	convert_int_to_char_array(type, message);
	strcpy(message + sizeof(uint32_t)/sizeof(char), CLIENTREQUESTS);
}

void get_filename_from_message(char *buf, char* filename)
{
   strcpy(filename, buf + 3*sizeof(uint32_t)/sizeof(char));
}

uint32_t get_id_from_message(char* buf)
{
	uint32_t i,Number = 0;
	for(i=0;i<sizeof(uint32_t)/sizeof(char);i++) 
	{
		((char*)&Number)[i] = buf[i + sizeof(uint32_t)/sizeof(char)];
	}
	return (ntohl(Number));

}


int get_file_size_from_message(char*message)
{
    uint32_t i,Number = 0;
	for(i=0;i<sizeof(uint32_t)/sizeof(char);i++) 
	{
		((char*)&Number)[i] = message[i + 2*sizeof(uint32_t)/sizeof(char)];
	}
	fprintf(stderr, "Converted size data from message %d \n", (int)(ntohl(Number)));
	return (int)(ntohl(Number));
}


void put_id_to_message(char * buf, uint32_t id_message)
{
	int i;
	uint32_t Number = htonl(id_message);
	for(i=0;i<sizeof(uint32_t)/sizeof(char);i++)
	{
		buf[i+sizeof(uint32_t)/sizeof(char)] = ((char*)&Number)[i];
	}		
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
 * Message = TYPE
 */
void download_request_work(int socket, struct sockaddr_in server_addr, char* filepath)
{
	char message_to_send[CHUNKSIZE];
	int type = (int)DOWNLOAD;
	memset(message_to_send, 0, CHUNKSIZE);
	convert_int_to_char_array(type, message_to_send);
	strcpy(message_to_send + sizeof(uint32_t)/sizeof(char), filepath);
	fprintf(stderr, "Size of path put inside message %zu \n", strlen(filepath));
	if(send_message(socket, server_addr, message_to_send, DOWNLOADSTRING) < 0)
	{	
	  ERR("SEND");
	}
	sleep(1);
}

void upload_request_work(int socket, struct sockaddr_in server_addr, char* filepath)
{
	char message_to_send[CHUNKSIZE];
	int type = (int)UPLOAD;
	struct stat sts;
	memset(message_to_send, 0, CHUNKSIZE);
	convert_int_to_char_array(type, message_to_send);
	strcpy(message_to_send + sizeof(uint32_t)/sizeof(char), filepath);
	fprintf(stderr, "Size of path put inside message %zu \n", strlen(filepath));
	if (stat(filepath, &sts) != -1 && errno == ENOENT)
	{
	  fprintf(stderr, "Could not open file %s \n", filepath);
	  return;
	}
	else
	{	
	  strcpy(message_to_send + 3*sizeof(uint32_t)/sizeof(char), filepath);
	  put_value_int_to_message(sts.st_size, message_to_send);
	  strcpy(message_to_send + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, UPLOADSTRING);
	}
	if(send_message(socket, server_addr, message_to_send, UPLOADSTRING) < 0)
	{	
	  ERR("SEND");
	}
	sleep(1);
}

void delete_request_work(int socket, struct sockaddr_in server_addr, char* filepath)
{
	char message_to_send[CHUNKSIZE];
	int type = (int)DELETE;
	memset(message_to_send, 0, CHUNKSIZE);
	convert_int_to_char_array(type, message_to_send);
	strcpy(message_to_send + sizeof(uint32_t)/sizeof(char), filepath);
	fprintf(stderr, "Size of path put inside message %zu \n", strlen(filepath));
	if(send_message(socket, server_addr, message_to_send, DELETESTRING) < 0)
	{	
	  ERR("SEND");
	}
	sleep(1);
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
	
	if(task == DOWNLOAD)
	{
	  message_type = DOWNLOADSTRING;
	}
	if(task == DOWNLOADRESPONSE)
	{
	  message_type = DOWNLOADRESPONSESTRING;
	}
	if(task == DELETE)
	{
	  message_type = DELETESTRING;
	}
	if(task == DELETERESPONSE)
	{
	  message_type = DELETERESPONSESTRING;
	}
	if(task == UPLOAD)
	{
	  message_type = UPLOADSTRING;
	}
	if(task == UPLOADROSPONSE)
	{
	  message_type = UPLOADRESPONSESTRING;
	}
	if(task == LIST)
	{
	  message_type = LISTSTRING;
	}
	if(task == LISTRESPONSE)
	{
	  message_type = LISTRESPONSESTRING;
	}
	if(task == REGISTERRESPONSE)
	{
	  message_type = REGISTERRESPONSESTRING;
	}
	if(task == ERROR)
	{
	  message_type = ERRORSTRING;
	}
	strcpy(tmp, message + 3*sizeof(uint32_t)/sizeof(char));
	fprintf(stderr, "Real message received = %s \n", tmp);
	fprintf(stderr, "Received message %s succeeded\n", message_type);
	return task;
}

/*
 * thread function for listening  anything from server
 */
void input_listening_work(int socket, struct sockaddr_in server_addr)
{
	task_type task = NONE;
	int max = 9 + FILENAME;
	char input_message[max];
	char filepath[FILENAME];
	char request[20];
	pthread_t thread;
	thread_arg targ;
	
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
		fprintf(stderr, "Got file name to download %s  \n", filepath);
		/*
		 * run new thread for communication and downloading
		 */
		init(&thread, &targ, &socket, &server_addr, DOWNLOAD, filepath);
	    }
	}
	else if(task == DELETE)
	{
	    if(sscanf(input_message, "%s %s", request, filepath) == EOF)
	    {
		fprintf(stderr, "PROBLEM WITH SSCANF");
	    }
	    else
	    {	
	        fprintf(stderr, "Got task type %s \n", request);
		fprintf(stderr, "Got file name to delete %s  \n", filepath);
		/*
		 * run new thread for communication and downloading
		 */
		init(&thread, &targ, &socket, &server_addr, DELETE, filepath);
	    }
	}else if(task == UPLOAD)
	{
	    if(sscanf(input_message, "%s %s", request, filepath) == EOF)
	    {
		fprintf(stderr, "PROBLEM WITH SSCANF");
	    }
	    else
	    {	
	        fprintf(stderr, "Got task type %s \n", request);
		fprintf(stderr, "Got file name to upload %s  \n", filepath);
		/*
		 * run new thread for communication and downloading
		 */
		init(&thread, &targ, &socket, &server_addr, UPLOAD, filepath);
	    }
	}
    
}

unsigned get_file_size (const char * file_name)
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
 * thread function for listening  anything from 
 */
void server_listening_work(int socket, struct sockaddr_in server_addr)
{
	task_type task = NONE;
	char message_received[CHUNKSIZE];
	task = receive_message(socket, &server_addr, message_received);
	fprintf(stderr, "SERVER_LISTENING, task = %d \n", (int)task);
	push(queue, message_received);
	if(task == ERROR)
	{
	   perror("Receiving message ");
	}
	else if(task == DELETERESPONSE)
	{
	    fprintf(stderr, "%s: %s", RECEIVEDINTHREAD, DELETERESPONSESTRING);
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
	else if(task == DELETE)
	{
	    fprintf(stderr, "%s: %s", RECEIVEDINTHREAD, DELETESTRING);
	}
	else if(task == UPLOAD)
	{
	    fprintf(stderr, "%s : %s", RECEIVEDINTHREAD, UPLOADSTRING);
	}
	else if(task == DOWNLOAD)
	{
	    fprintf(stderr, "%s : %s", RECEIVEDINTHREAD, DOWNLOADSTRING);
	}
	else if(task == LIST)
	{
	    fprintf(stderr, "%s : %s", RECEIVEDINTHREAD, LISTSTRING);
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

void *download_thread_function(void *arg)
{
	int clientfd;
	int id = -1;
	int tmpid = -1;
	int i;
	int fd;
	int package_amount = 0;
	struct sockaddr_in server_addr;
	thread_arg targ;
	char* filepath;
	char oldFilePath[FILENAME];
	char communicate [CHUNKSIZE - 3* sizeof(uint32_t)/ sizeof(char) - FILENAME];
	int real_package_size = CHUNKSIZE - 3 * sizeof(uint32_t)/sizeof(char);
	char * real_file_name;
	int filesize = 0;
	char* message;
	int first_empty_sign;
	task_type task;
	char *buf;
	char * package;
	int package_number;
	char * file_contents;
	message = calloc(CHUNKSIZE, sizeof(char));
	unsigned char md5_sum[MD5LENGTH];
	FILE* file;
	memcpy(&targ, arg, sizeof(targ));
	struct stat sts;
	package = malloc(real_package_size);
	if (work)
	{
		clientfd = *targ.socket;
		server_addr = *targ.server_addr;
		filepath = targ.filename;
		download_request_work(clientfd, server_addr, filepath);
	}
	for(i = 0; i< FILENAME; i++)
	{
	  oldFilePath[i] = filepath[i];
	}
	
	/*
	 * receive communicate from server about existing file or not
	 */
	while(work)
	{
	      /* top from queue */
	      while(queue->busy)
	      {
		  sleep(1);
	      }
	      /*fprintf(stderr, "Size of queue before top = %d \n", queue->size);*/
	      top(queue, message);
	      /*fprintf(stderr, "Size of queue after top = %d \n", queue->size);*/
	      
	      task = check_message_type(message);
	      fprintf(stderr, "real task nummber %d \n", (int)task);
	      if(task != DOWNLOADRESPONSE)
	      {
		/* push to the end of queue */
		while(queue->busy)
		{
		  sleep(1);
		}
		push(queue, message);
		sleep(1);
		continue;
	      }
	      /* waiting for confirmation or rejection of downloading file*/
	      else if(id == -1)
	      {
		
		/* check the filepath saved in message*/
		get_filename_from_message(message, filepath);
		for(i = 0; i < FILENAME; i++)
		{
		    if(filepath[i] != oldFilePath[i])
		    {
		      fprintf(stderr, "Not this message, wrong filepath \n");
		      push(queue, message);
		      break;
		    }
		}
		/* filepaths are the same */
		if(i == FILENAME)
		{
		  /* check if id > 0 else show error got from server */
		  tmpid = get_id_from_message(message);
		  strcpy(communicate, message + 3 * sizeof(uint32_t)/ sizeof(char) + FILENAME);
		  if(tmpid == 0)
		  {
		    /* error with file , show message */
		    fprintf(stdout, "Could not download file %s, reason : %s \n", filepath, communicate);
		    push(queue, message);
		    pthread_exit(&targ);
		    return NULL;
		  }
		  else
		  {
		    id = tmpid;
		    fprintf(stdout, "Server confirmed downloading file  %s, \n  message : %s \n", filepath, communicate);
		    fprintf(stdout, "Got id %d \n", id);
		    /* check if file exists if not create else check if is opened if not remove data and write own */
		    for(i = 0; i < FILENAME; i++)
		    {
			if(filepath[i] == '\0' || filepath[i] == '\n')
			{
			  first_empty_sign = i;
			  break;
			}
		    }
		    fprintf(stderr, "Empty sign found at position : %d \n", first_empty_sign);
		    real_file_name = calloc(first_empty_sign , sizeof(char));
		    fprintf(stderr, "Real file name size %zu after malloc \n", strlen(real_file_name));
		    for(i = 0; i < first_empty_sign; i++)
		    {
			real_file_name[i] = filepath[i];
		    }
		    if(open(real_file_name, O_RDWR) < 0)
		    {
		      fd = open(real_file_name, O_RDWR | O_CREAT);
		      
			  fprintf(stderr, "Creating new file \n");
			  filesize = get_file_size_from_message(message);
			  buf = calloc(filesize, sizeof(char));
			  for(i = 0; i< filesize; i++)
			    buf[i] = '0';
			  bulk_write(fd, buf, filesize);
			  if(filesize == filesize / real_package_size * real_package_size)
			    package_amount = filesize / real_package_size;
			  else 
			      package_amount = filesize / real_package_size + 1;	
		    }
		    else
		    {
			 fprintf(stdout, "File already exists!\n");
			 pthread_exit(&targ);
			 return NULL;
		    }
		    break;
		  }
		}
	      }
	}
	
	fprintf(stderr, "Waiting for datagrams with file content\n");

	while(work)
	{
	      /* top from queue */
	      while(queue->busy)
	      {
		  sleep(1);
	      }
	      /*fprintf(stderr, "Size of queue before top = %d \n", queue->size);*/
	      top(queue, message);
	      /*fprintf(stderr, "Size of queue after top = %d \n", queue->size);*/
	      task = check_message_type(message);
	      /*fprintf(stderr, "Real task nummber %d \n", (int)task);*/
	      if(task != DOWNLOAD)
	      {
		/* push to the end of queue */
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
		  tmpid = get_id_from_message(message);
		  if(tmpid == id)
		  {
		      /*one of datagrams from servers*/
		      fprintf(stderr, "Id of message and thread are the same \n");
		      fprintf(stderr, "Got data for update \n");
		      package_number = get_file_size_from_message(message);
		      if(package_number == package_amount)
		      {
			fprintf(stderr, "Checking md5 sums \n");
			file_contents = read_whole_file (real_file_name);
			  if(file_contents == NULL) 
			  {
			    free (file_contents);
			    free(real_file_name);
			    free(package);
			    return NULL;
			  }
			compute_md5(file_contents, md5_sum);
			strcpy(package, message + 3 * sizeof(uint32_t)/sizeof(char) + FILENAME);
			for(i = 0; i< MD5LENGTH; i++)
			{
			  if(md5_sum[i] != package[i])
			  {
			    fprintf(stdout, "Wrong md5 sum %s \n", real_file_name);
			    return NULL;
			  }
			}		
		      }
		      else
		      {
			fprintf(stderr, "Write data to update file \n");
			strcpy(package, message + 3 * sizeof(uint32_t)/sizeof(char) + FILENAME);
			if (stat(real_file_name, &sts) != -1)
			{
			  fprintf(stderr, "Could not open file %s \n", real_file_name);
			  return NULL;
			}
			file = fopen(real_file_name, "rw");
			if(file == NULL)
			{
			  if(errno == EINVAL)
			  {
			    fprintf(stderr, "Wrong mode of oppening file \n");
			  }
			  return NULL;
			}
			fprintf(stderr, "Package number %d \n", package_number);
			if(fseek(file, package_number * real_package_size, SEEK_SET) < 0)
			{
			  if(errno == EBADF)
			  {
			    fprintf(stderr, "The stream specified is not a seekable stream.\n");
			  }
			}
			fputs(package, file);
			fclose(file);
		      }
		  }
		  else
		  {
		    push(queue, message);
		  }
	      }
	  }

	pthread_exit(&targ);
	return NULL;
}

void *upload_thread_function(void *arg)
{
	int clientfd;
	int id = -1;
	int tmp_id = -1;
	int i;
	unsigned char md5_sum[MD5LENGTH];
	struct sockaddr_in server_addr;
	thread_arg targ;
	char* filepath;
	char oldFilePath[FILENAME];
	char communicate [CHUNKSIZE - 3* sizeof(uint32_t)/ sizeof(char) - FILENAME];
	char * real_file_name;
	int filesize = 0;
	char* message;
	int first_empty_sign;
	task_type task;
	int package_amount = 0;
	char * file_contents;
	int real_package_size = CHUNKSIZE - 3 * sizeof(uint32_t)/sizeof(char);
	char *package;
	package = malloc(real_package_size);
	memcpy(&targ, arg, sizeof(targ));
	message = calloc(CHUNKSIZE, sizeof(char));
	if (work)
	{
		clientfd = *targ.socket;
		server_addr = *targ.server_addr;
		filepath = targ.filename;
		upload_request_work(clientfd, server_addr, filepath);
	}
	for(i = 0; i< FILENAME; i++)
	{
	  oldFilePath[i] = filepath[i];
	}
	
	/*
	 * receive communicate from server about response for update
	 */
	while(work)
	{
	      /* top from queue */
	      while(queue->busy)
	      {
		  sleep(1);
	      }
	      /*fprintf(stderr, "Size of queue before top = %d \n", queue->size);*/
	      top(queue, message);
	      /*fprintf(stderr, "Size of queue after top = %d \n", queue->size);*/
	      
	      task = check_message_type(message);
	      fprintf(stderr, "real task nummber %d \n", (int)task);
	      if(task != UPLOADROSPONSE)
	      {
		/* push to the end of queue */
		while(queue->busy)
		{
		  sleep(1);
		}
		push(queue, message);
		sleep(1);
		continue;
	      }
	      /* waiting for confirmation or rejection of uploading file*/
	      else if(id == -1)
	      {
		
		/* check the filepath saved in message*/
		get_filename_from_message(message, filepath);
		for(i = 0; i < FILENAME; i++)
		{
		    if(filepath[i] != oldFilePath[i])
		    {
		      fprintf(stderr, "Not this message, wrong filepath \n");
		      push(queue, message);
		      break;
		    }
		}
		/* filepaths are the same */
		if(i == FILENAME)
		{
		  /* check if id > 0 else show error got from server */
		  tmp_id = get_id_from_message(message);
		  strcpy(communicate, message + 3 * sizeof(uint32_t)/ sizeof(char) + FILENAME);
		  if(tmp_id == 0)
		  {
		    /* error with file , show message */
		    fprintf(stdout, "Could not upload file %s, reason : %s \n", filepath, communicate);
		    push(queue, message);
		    pthread_exit(&targ);
		    return NULL;
		  }
		  else
		  {
		    id = tmp_id;
		    fprintf(stdout, "Server confirmed uploading file  %s, \n  message : %s \n", filepath, communicate);
		    fprintf(stdout, "Got id %d \n", id);
		    /* check if file exists if not create else check if is opened if not remove data and write own */
		    for(i = 0; i < FILENAME; i++)
		    {
			if(filepath[i] == '\0' || filepath[i] == '\n')
			{
			  first_empty_sign = i;
			  break;
			}
		    }
		    fprintf(stderr, "Empty sign found at position : %d \n", first_empty_sign);
		    real_file_name = calloc(first_empty_sign , sizeof(char));
		    fprintf(stderr, "Real file name size %zu after malloc \n", strlen(real_file_name));
		    for(i = 0; i < first_empty_sign; i++)
		    {
			real_file_name[i] = filepath[i];
		    }
		    		    
		    file_contents = read_whole_file (real_file_name);
		    filesize = strlen(file_contents);
		    if(filesize == filesize / real_package_size * real_package_size)
		      package_amount = filesize / real_package_size;
		    else 
			package_amount = filesize / real_package_size + 1;
		    if(file_contents == NULL) 
		    {
		      free (file_contents);
		      free(real_file_name);
		      free(package);
		      return NULL;
		    }
		    compute_md5(file_contents, md5_sum);
		    for(i = 0; i < package_amount; i++)
		    {
			strcpy(package, file_contents + i * real_package_size);
			put_id_to_message(message,tmp_id);
				  put_value_int_to_message(i, message);
				  strcpy(message + 3*sizeof(uint32_t)/sizeof(char), package);
				  fprintf(stderr, "Sending data package number %d for task id %d : %s \n", i,tmp_id, package);
				  send_message(clientfd, server_addr, message, UPLOADSTRING);
				  sleep(1);			    
		     }
	             put_value_int_to_message(i, message);
		     fprintf(stderr, "Sending md5 sum for task id %d \n", tmp_id);
		     strcpy(message + 3*sizeof(uint32_t)/sizeof(char), (char*)md5_sum);
	             send_message(clientfd, server_addr, message, UPLOADSTRING);
		      sleep(1);      
		    break;
		  }
		}
	      }
	}
	
	fprintf(stderr, "Waiting for datagrams with file content\n");

	pthread_exit(&targ);
	return NULL;
}

void *delete_thread_function(void *arg)
{
	int clientfd;
	int id = -1;
	int tmpid = -1;
	int i;
	struct sockaddr_in server_addr;
	thread_arg targ;
	char* filepath;
	char oldFilePath[FILENAME];
	char communicate [CHUNKSIZE - 3* sizeof(uint32_t)/ sizeof(char) - FILENAME];
	char* message;
	task_type task;
	memcpy(&targ, arg, sizeof(targ));
	message = calloc(CHUNKSIZE, sizeof(char));
	if (work)
	{
		clientfd = *targ.socket;
		server_addr = *targ.server_addr;
		filepath = targ.filename;
		delete_request_work(clientfd, server_addr, filepath);
	}
	for(i = 0; i< FILENAME; i++)
	{
	  oldFilePath[i] = filepath[i];
	}
	
	/*
	 * receive communicate from server about existing file or not
	 */
	while(work)
	{
	      /* top from queue */
	      while(queue->busy)
	      {
		  sleep(1);
	      }
	      /*fprintf(stderr, "Size of queue before top = %d \n", queue->size);*/
	      top(queue, message);
	      /*fprintf(stderr, "Size of queue after top = %d \n", queue->size);*/
	      
	      task = check_message_type(message);
	      fprintf(stderr, "real task nummber %d \n", (int)task);
	      if(task != DELETERESPONSE)
	      {
		/* push to the end of queue */
		while(queue->busy)
		{
		  sleep(1);
		}
		push(queue, message);
		sleep(1);
		continue;
	      }
	      /* waiting for confirmation or rejection of downloading file*/
	      else if(id == -1)
	      {
		
		/* check the filepath saved in message*/
		get_filename_from_message(message, filepath);
		for(i = 0; i < FILENAME; i++)
		{
		    if(filepath[i] != oldFilePath[i])
		    {
		      fprintf(stderr, "Not this message, wrong filepath \n");
		      push(queue, message);
		      break;
		    }
		}
		/* filepaths are the same */
		if(i == FILENAME)
		{
		  /* check if id > 0 else show error got from server */
		  tmpid = get_id_from_message(message);
		  strcpy(communicate, message + 3 * sizeof(uint32_t)/ sizeof(char) + FILENAME);
		  if(tmpid == 0)
		  {
		    /* error with file , show message */
		    fprintf(stdout, "Could not delete  file %s, reason : %s \n", filepath, communicate);
		    push(queue, message);
		    pthread_exit(&targ);
		    return NULL;
		  }
		  else
		  {
		    id = tmpid;
		    fprintf(stdout, "Server confirmed planned deleting file  %s, \n  message : %s \n", filepath, communicate);
		    fprintf(stdout, "Got id %d \n", id);
		    /* check if file exists if not create else check if is opened if not remove data and write own */
		    break;
		  }
		}
	      }
	}
	
	fprintf(stderr, "Waiting for datagrams abour deleting file\n");

	while(work)
	{
	      /* top from queue */
	      while(queue->busy)
	      {
		  sleep(1);
	      }
	      /*fprintf(stderr, "Size of queue before top = %d \n", queue->size);*/
	      top(queue, message);
	      /*fprintf(stderr, "Size of queue after top = %d \n", queue->size);*/
	      task = check_message_type(message);
	      fprintf(stderr, "Real task nummber %d \n", (int)task);
	      if(task != DELETE)
	      {
		/* push to the end of queue */
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
		  tmpid = get_id_from_message(message);
		  if(tmpid == id)
		  {
		      /*one of datagrams from servers*/
		      fprintf(stderr, "Id of message and thread are the same \n");
		      strcpy(communicate, message + 3 * sizeof(uint32_t)/ sizeof(char) + FILENAME);
		      fprintf(stdout, "Server confirmed deleting file  %s, \n  message : %s \n", filepath, communicate);
		      break;
		  }
		  else
		  {
		    push(queue, message);
		  }
	      }
	  }

	pthread_exit(&targ);
	return NULL;
}

void *list_thread_function(void *arg)
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

void init(pthread_t *thread, thread_arg *targ, int *socket, struct sockaddr_in* server_addr, task_type task,
	  char filepath [FILENAME])
{
	int i;
	if(task == NONE)
	{
		for (i = 0; i < 2; i++)
		{
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
	else
	{
		targ[0].socket = socket;
		targ[0].server_addr = server_addr;
		if(task == DOWNLOAD)
		{
		  targ[0].filename = filepath;
		  if (pthread_create(&thread[0], NULL, download_thread_function, (void *) &targ[0]) != 0)
			    ERR("pthread_create");
		}
		else if(task == UPLOAD)
		{
		    targ[0].filename = filepath;
		    if (pthread_create(&thread[0], NULL, upload_thread_function, (void *) &targ[0]) != 0)
			    ERR("pthread_create");
		}
		else if(task == DELETE)
		{
		    targ[0].filename = filepath;
		    if (pthread_create(&thread[0], NULL, delete_thread_function, (void *) &targ[0]) != 0)
			    ERR("pthread_create");
		}
		else if(task == LIST)
		{
		    if (pthread_create(&thread[0], NULL, list_thread_function, (void *) &targ[0]) != 0)
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
	
	if (argc!=2)
		usage(argv[0]);
	
	queue = createQueue(100);

	fprintf(stdout,"%s", INSTRUCTION);

	sethandler(SIG_IGN, SIGPIPE);
	sethandler(siginthandler, SIGINT);
	

	generate_register_message(message);

	
	my_endpoint_listening_addr = make_address(NULL, atoi(argv[1])+1 , 0);
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
	
	/*
	 * inicialize 2 main threads for listening from input and server
	 */
	init(thread, targ, &socket, &server_addr, NONE, NULL);
	
	do_work(socket);
	
	return EXIT_SUCCESS;
}

