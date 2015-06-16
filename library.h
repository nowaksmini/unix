#ifndef _LAB_H
#define _LAB_H

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


#define DOWNLOADRESPONSEERROR "No such file\n"
#define DELETEERESPONSEERROR "No such file\n"
#define DELETEACCESSDENYIED "Access is denyied\n"
#define DOWNLOADRESPONSESUCCESS "Registered request to downolad file successfully"
#define REGISTERRESPONSESUCCESS "Registered client successfully"
#define DELETERESPONSESUCCESS "Registered request to delete file successfully"
#define LISTRESPONSESUCCESS "Registered request to list all files successfully"
#define UPLOADRESPONSESUCCESS "Registered request to upload file to server successfully"
#define UPLOADRESPONSEERROR "Registered request to upload file to server abandoned"


#define INSTRUCTION "\nINSTRUCTUIION\nOPTIONS\ndownload file_name\nupload file_name\nremove file_name\n\n"
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

typedef struct
{
	int* socket;
	struct sockaddr_in *server_addr;
	char* filename;
} thread_arg;


/*
 * function responsible for handling SIGINT signal 
 */
void siginthandler(int sig);

/*
 * set handler for specified signal
 */
void sethandler(void (*f)(int), int sigNo);

/*
 * str = whole file data
 * sum = output counted
 */
void compute_md5(char *str, unsigned char * sum);

/* crateQueue function takes argument the maximum number of elements the Queue can hold, creates
   a Queue according to it and returns a pointer to the Queue. */
Queue * createQueue(int maxElements);

void push(Queue* queue, char* message);

void top(Queue* queue, char* message);

/*
 * buf - memory to write data read from file, must be allocated for minimum count size
 * fd - descriptor of file
 * count - amount of bytes to read
 */
ssize_t bulk_read(int fd, char *buf, size_t count);

/*
 * buf - memory to data written to file
 * fd - descriptor of file
 * count - amount of bytes to write
 */
ssize_t bulk_write(int fd, char *buf, size_t count);
/*
 * check task type of message (first four bytes)
 */
task_type check_message_type(char * buf);
/*
 * write first four bytes to array
 */
void convert_int_to_char_array(int argument, char* buf);

/*
 * puts value always on position 2*sizeof(uint32_t)/sizeof(char) after id of message transaction
 */
void put_value_int_to_message(uint32_t value, char* buf);

void get_filename_from_message(char *buf, char* filename);

uint32_t get_id_from_message(char* buf);

int get_file_size_from_message(char*message);

void put_id_to_message(char * buf, uint32_t id_message);

unsigned get_file_size (const char * file_name);

/* This routine reads the entire file into memory. */

char * read_whole_file (const char * file_name);
#endif
 
