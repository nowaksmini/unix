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
#include "library.h"



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

/* crateQueue function takes argument the maximum number of elements the Queue can hold, 
 * creates a Queue according to it and returns a pointer to the Queue. 
 */
Queue * createQueue(int maxElements)
{
        /* Create a Queue */
        Queue *Q;
        Q = (Queue *)malloc(sizeof(Queue));
        /* Initialise it's properties */
        Q->elements = (char *)calloc(maxElements*CHUNKSIZE, sizeof(char));
        Q->size = 0;
        Q->capacity = maxElements;
        /* Return the pointer */
        Q->busy = 0;
        return Q;
}

/*
 * push element on the end of queue if queue has empty place for element
 */
void push(Queue* queue, char* message)
{
	int i;
	int move;
	while(queue->busy)
	{
	  sleep(1);
	}
	queue->busy = 1;
	move = (queue->size) * CHUNKSIZE;
	while(queue->size == queue->capacity)
	{
	    queue->busy = 0;
	    fprintf(stderr, "Too many elements in queue waiting... \n");
	    sleep(2);
	}
	queue->busy = 1;
	for(i = move; i < CHUNKSIZE + move; i++)
	{
	   queue->elements[i] = message[i - move];
	}
	queue->size = queue->size + 1;
	queue->busy = 0;
}

/*
 * pop element from top of queue,
 * save it to message
 */
int top(Queue* queue, char* message)
{
	int i;
	while(queue->busy)
	{
	  sleep(1);
	}
	queue->busy = 1;
	if(queue->size == 0) 
	{
	  queue->busy = 0;
	  fprintf(stderr, "Empty queue \n");
	  return -1;
	}
	char messages[queue->capacity * CHUNKSIZE];
	for(i = 0; i< CHUNKSIZE; i++)
	{
	  message[i] = queue->elements[i];
	}
	for(i = CHUNKSIZE; i< queue->capacity * CHUNKSIZE; i++)
	{
	  messages[i - CHUNKSIZE] = queue->elements[i];
	}
	for(i = 0; i< queue->capacity * CHUNKSIZE; i++)
	{
	   queue->elements[i] = messages[i];
	}
	queue->size = queue->size -1;
	queue->busy = 0;
	return 0;
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
 * convert number to task_type
 * default value ERROR
 */
task_type convert_uint32_to_task_type(uint32_t number)
{
  switch(number)
  {
    case 0:
      return REGISTER;
    case 1:
      return DOWNLOAD;
    case 2:
      return UPLOAD;
    case 3:
      return DELETE;
    case 4:
      return LIST;
    case 5:
      return REGISTERRESPONSE;
    case 6:
      return DOWNLOADRESPONSE;
    case 7:
      return UPLOADROSPONSE;
    case 8:
      return DELETERESPONSE;
    case 9:
      return LISTRESPONSE;
    default :
      return ERROR;
  }
}

/*
 * convert task_type to uint32_t
 * default value = 10 (ERROR)
 */
uint32_t convert_task_type_to_uint32(task_type task)
{
  switch(task)
  {
    case REGISTER:
      return 0;
    case DOWNLOAD:
      return 1;
    case UPLOAD:
      return 2;
    case DELETE:
      return 3;
    case LIST:
      return 4;
    case REGISTERRESPONSE:
      return 5;
    case DOWNLOADRESPONSE:
      return 6;
    case UPLOADROSPONSE:
      return 7;
    case DELETERESPONSE:
      return 8;
    case LISTRESPONSE:
      return 9;
    default :
      return 10;
  }
}

/*
 * check task type of message (first four bytes)
 */
task_type check_message_type(char * buf)
{
	uint32_t i,number = 0;
	for(i = 0; i < sizeof(uint32_t)/sizeof(char); i++) 
	{
		((char*)&number)[i] = buf[i];
	}
	number = ntohl(number);
	fprintf(stderr, "Received task number  %d \n", number);
	return convert_uint32_to_task_type(number);
}

/*
 * write task_type to four first bytes of message
 */
void save_massage_type_to_message(task_type task, char* buf)
{
	int i;
	uint32_t number = htonl(convert_task_type_to_uint32(task));
	fprintf(stderr, "Saving to message task type %u \n", convert_task_type_to_uint32(task));
	for(i = 0; i < sizeof(uint32_t)/sizeof(char); i++)
	{
		buf[i] = ((char*)&number)[i];
	}	
}

/*
 * puts value always on position 2*sizeof(uint32_t)/sizeof(char) after id of message transaction
 */
void put_size_to_message(uint32_t value, char* buf)
{
	int i;
	uint32_t number = htonl(value);
	fprintf(stderr, "Size written to message %u \n", value);
	for(i = 0; i < sizeof(uint32_t)/sizeof(char); i++)
	{
		buf[i + 2*sizeof(uint32_t)/sizeof(char)] = ((char*)&number)[i];
	}	
}

/*
 * get from message filename and save it to *filename 
 */
void get_filename_from_message(char *buf, char* filename)
{
   strcpy(filename, buf + 3*sizeof(uint32_t)/sizeof(char));
   fprintf(stderr, "Got file name %s \n", filename);
}


/*
 * return id of message (data transfer)
 */
uint32_t get_id_from_message(char* buf)
{
	uint32_t i,number = 0;
	for(i = 0; i < sizeof(uint32_t)/sizeof(char); i++) 
	{
		((char*)&number)[i] = buf[i + sizeof(uint32_t)/sizeof(char)];
	}
	fprintf(stderr, "Got id of message %u \n", ntohl(number));
	return (ntohl(number));

}

/*
 * gets file size or number of package frommessage
 */
uint32_t get_file_size_from_message(char* message)
{
    uint32_t i,Number = 0;
	for(i = 0; i < sizeof(uint32_t)/sizeof(char); i++) 
	{
		((char*)&Number)[i] = message[i + 2*sizeof(uint32_t)/sizeof(char)];
	}
	fprintf(stderr, "Converted size data from message %d \n", (int)(ntohl(Number)));
	return (int)(ntohl(Number));
}

/*
 * put id to message at position 4
 */
void put_id_to_message(char * buf, uint32_t id_message)
{
	int i;
	uint32_t Number = htonl(id_message);
	for(i = 0; i < sizeof(uint32_t)/sizeof(char); i++)
	{
		buf[i + sizeof(uint32_t)/sizeof(char)] = ((char*)&Number)[i];
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

/* 
 *This routine reads the entire file into memory. 
 */

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
 * base function to response for client requests
 */
void* server_send_response_function(void * arg, char * type_name, task_type expected_type, void (*function) (char*, int, struct sockaddr_in))
{
	int clientfd;
	struct sockaddr_in client_addr;
	thread_arg targ;
	char* message;
	memcpy(&targ, arg, sizeof(targ));
	task_type task = NONE;
	char* error_file_path;
	message = calloc(CHUNKSIZE, sizeof(char));
	error_file_path = (char *)calloc(FILENAME, sizeof(char));
	while (work)
	{
		/* top from queue */
		if (top(queue, message) < 0)
		{
		  fprintf(stderr, "Queue is empty, nothing to show \n");
		  sleep(2);
		  message = calloc(CHUNKSIZE, sizeof(char));
		  continue;
		}  
		task = check_message_type(message);
		fprintf(stderr, "Real task nummber %d \n", (int)task);
		if(task == ERROR)
		{
		  get_filename_from_message(message, error_file_path);
		  fprintf(stderr, "Got task with type ERROR for filename %s \n", error_file_path);
		  continue;
		}
		if(task != expected_type)
		{
		  /* push to the end of queue */
		  fprintf(stderr, "Task was not %s \n", type_name);
		  push(queue, message);
		  sleep(1);
		  continue;
		}
		else
		{
		  clientfd = *targ.socket;
		  client_addr = *targ.server_addr;
		  (*function)(message, clientfd, client_addr);
		  sleep(1);
		  break;
		}
	}
	fprintf(stderr, "Destroing %s thread\n" , type_name);
	pthread_exit(&targ);
	return NULL;
}