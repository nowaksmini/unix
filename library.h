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
#define CHUNKSIZE 100


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

volatile sig_atomic_t work;
Queue* queue;

task_type convert_uint32_to_task_type(uint32_t number);

uint32_t convert_task_type_to_uint32(task_type task);

void siginthandler(int sig);

void sethandler(void (*f)(int), int sigNo);

void compute_md5(char *str, unsigned char * sum);

Queue * createQueue(int maxElements);

void push(Queue* queue, char* message);

int top(Queue* queue, char* message);

ssize_t bulk_read(int fd, char *buf, size_t count);

ssize_t bulk_write(int fd, char *buf, size_t count);

task_type check_message_type(char * buf);

void save_massage_type_to_message(uint32_t type, char* buf);

void put_size_to_message(uint32_t value, char* buf);

void get_filename_from_message(char *buf, char* filename);

uint32_t get_id_from_message(char* buf);

uint32_t get_file_size_from_message(char*message);

void put_id_to_message(char * buf, uint32_t id_message);

unsigned get_file_size (const char * file_name);

char * read_whole_file (const char * file_name);

void* server_send_response_function(void * arg, char * type_name, task_type expected_type, void (*function) (char*, int, struct sockaddr_in));

#endif
 
