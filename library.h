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
#define LISTFILE "list_file"
#define CLIENTLISTFILE "client_list_file"

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


#define DOWNLOADRESPONSEERROR "Could not download file\n"
#define DELETEERESPONSEERROR "Could not delete file\n"
#define DELETEACCESSDENYIED "Access is denyied\n"
#define DOWNLOADRESPONSESUCCESS "Registered request to downolad file successfully"
#define REGISTERRESPONSESUCCESS "Registered client successfully"
#define DELETERESPONSESUCCESS "Registered request to delete file successfully"
#define LISTRESPONSESUCCESS "Registered request to list all files successfully"
#define LISTRESPONSEERROR "Could not list files"
#define UPLOADRESPONSESUCCESS "Registered request to upload file to server successfully"
#define UPLOADRESPONSEERROR "Registered request to upload file to server abandoned"


#define INSTRUCTION "\nINSTRUCTUIION\nOPTIONS\ndownload file_name\nupload file_name\nremove file_name\nlist\n\n"
#define RECEIVEDINTHREAD "Received in thread"

#define CLIENTREQUESTS "Client requests registration"
#define FILENAME 50
#define CHUNKSIZE 576
#define QUEUECAPACITY 200

typedef enum {REGISTER, DOWNLOAD, UPLOAD, DELETE, LIST, REGISTERRESPONSE, DOWNLOADRESPONSE,
	UPLOADROSPONSE, DELETERESPONSE, LISTRESPONSE, ERROR, NONE} task_type;

	typedef struct Queue
	{
		int capacity;
		int size;
		int busy;
		pthread_mutex_t* access;
		char* elements;
	}Queue;

	typedef struct
	{
		int* socket;
		struct sockaddr_in *server_addr;
		char* filename;
		int task;
		int package_amount;
		int package_number;
	} thread_arg;

	volatile sig_atomic_t work;
	Queue* queue;
	pthread_mutex_t* file_access;
	pthread_mutex_t* common_file_access;

	void inicialize_file_mutex();

	void inicialize_common_file_mutex();

	void free_common_file_mutex();

	void free_file_mutex();

	int rand_range(int min_n, int max_n);

	task_type convert_uint32_to_task_type(uint32_t number);

	uint32_t convert_task_type_to_uint32(task_type task);

	void siginthandler(int sig);

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

	int get_file_size (const char * file_name);

	char * read_whole_file (char * file_name);

	uint32_t delete_status_from_list(char* file_name, char* searched_file_name);

	uint32_t write_status_to_list(int message_id, char* file_name, char* searched_file_name,  int percentage, int package_numbers, int last_package, int task);

	void* server_send_response_function(void * arg, char * type_name, task_type expected_type, void (*function) (task_type task, char*, int, struct sockaddr_in));

	int receive_message (int socket, struct sockaddr_in* received_addr, char* message);

	int send_message (int socket, struct sockaddr_in receiver_addr, char* message, char* message_type);

	void free_queue();

	uint8_t create_list_file(char* file_name);

	uint8_t read_all_files_to_list(char* file_name);

	uint8_t create_file(char* real_file_name, int* filesize, int real_package_size, int* package_amount, uint8_t** packages, char* message);

	void close_file(int* fd, char* real_file_name);

	void generate_package_amount(int* filesize, int real_package_size, int* package_amount, char* message);

	uint8_t open_file(char* real_file_name, int *fd);

	uint8_t check_top_of_queue(char* message_type, task_type* task, char* message, task_type expected_task, char* error_file_path);

#endif

