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
#include <dirent.h> 
#include "library.h"


void inicialize_file_mutex()
{
	pthread_mutex_t *mut = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
	*mut = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	file_access = mut;
}

void free_file_mutex()
{
	free(file_access);
}

/*
 * random number from min and max value
 */
int rand_range(int min_n, int max_n)
{
	return rand() % (max_n - min_n + 1) + min_n;
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
 * function responsible for handling SIGINT signal
 */
void siginthandler(int sig)
{
	work = 0;
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
	pthread_mutex_t *mut = (pthread_mutex_t*) malloc(sizeof(pthread_mutex_t));
	*mut = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
	Q = (Queue *)malloc(sizeof(Queue));
	/* Initialize it's properties */
	Q->elements = (char *)calloc(maxElements*CHUNKSIZE, sizeof(char));
	Q->size = 0;
	Q->capacity = maxElements;
	/* Return the pointer */
	Q->busy = 0;
	Q->access = mut;
	return Q;
}

/*
 * push element on the end of queue if queue has empty place for element
 */
void push(Queue* queue, char* message)
{
	int i;
	int move;
	while(1)
	{
		pthread_mutex_lock(queue->access);
		if (queue->busy)
		{
			pthread_mutex_unlock(queue->access);
			sleep(1);
		}
		else
			break;
	}
	queue->busy = 1;
	pthread_mutex_unlock(queue->access);
	
	move = (queue->size) * CHUNKSIZE;
	while(queue->size == queue->capacity)
	{
		pthread_mutex_lock(queue->access);
		queue->busy = 0;
		pthread_mutex_unlock(queue->access);
		fprintf(stderr, "Too many elements in queue waiting... \n");
		sleep(2);
	}
	pthread_mutex_lock(queue->access);
	queue->busy = 1;
	pthread_mutex_unlock(queue->access);
	for(i = move; i < CHUNKSIZE + move; i++)
	{
		queue->elements[i] = message[i - move];
	}
	queue->size = queue->size + 1;
	pthread_mutex_lock(queue->access);
	queue->busy = 0;
	pthread_mutex_unlock(queue->access);
}

/*
 * pop element from top of queue,
 * save it to message
 */
int top(Queue* queue, char* message)
{
	int i;
	while(1)
	{
		pthread_mutex_lock(queue->access);
		if (queue->busy)
		{
			pthread_mutex_unlock(queue->access);
			sleep(1);
		}
		else
			break;
	}
	queue->busy = 1;
	pthread_mutex_unlock(queue->access);
	if(queue->size == 0)
	{
		pthread_mutex_lock(queue->access);
		queue->busy = 0;
		pthread_mutex_unlock(queue->access);
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
	pthread_mutex_lock(queue->access);
	queue->busy = 0;
	pthread_mutex_unlock(queue->access);
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
		 * if signal interrupted reading try again
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
 * gets file size or number of package from message
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

/* return file size if it is possible else return -1 */
int get_file_size (const char * file_name)
{
	struct stat sb;
	if (stat (file_name, & sb) != 0) {
		fprintf (stderr, "'Stat' failed for '%s': %s.\n", file_name, strerror (errno));
		return -1;
	}
	return sb.st_size;
}

/*
 *This routine reads the entire file into memory.
 */
char * read_whole_file (const char * file_name)
{
	int s;
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
void* server_send_response_function(void * arg, char * type_name, task_type expected_type, void (*function) (task_type task, char*, int, struct sockaddr_in))
{
	int clientfd;
	struct sockaddr_in client_addr;
	thread_arg targ;
	char* message;
	memcpy(&targ, arg, sizeof(targ));
	task_type task = NONE;
	char* error_file_path;
	message = (char*)malloc(CHUNKSIZE * sizeof(char));
	if(message == NULL)
	{
		fprintf(stderr, "Server send response has problems with allocation memory for message \n");
	}
	error_file_path = (char *)malloc(FILENAME * sizeof(char));
	if(error_file_path == NULL)
	{
		fprintf(stderr, "Server send response has problems with allocation memory for file path \n");
	}
	while (work)
	{
		/* top from queue */
		if(check_top_of_queue(type_name, &task, message, expected_type, error_file_path) == 1)
			continue;
		else
		{
			clientfd = *targ.socket;
			client_addr = *targ.server_addr;
			(*function)(expected_type, message, clientfd, client_addr);
			sleep(1);
			break;
		}
	}
	free(message);
	free(error_file_path);
	fprintf(stderr, "Destroying %s thread\n" , type_name);
	pthread_exit(&targ);
	return NULL;
}

/*
 * receiving message and remembering address
 */
int receive_message (int socket, struct sockaddr_in* receiver_addr, char* message)
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
	if(recvfrom(socket, message, CHUNKSIZE, 0, receiver_addr, &size) < 0)
	{
		fprintf(stderr, "Failed receiving message\n");
		return -1;
	}
	/*
	 * success
	 */
	task = check_message_type(message);
	switch(task)
	{
	case REGISTER:
		message_type = REGISTERSTRING;
		break;
	case DOWNLOAD:
		message_type = DOWNLOADSTRING;
		break;
	case UPLOAD:
		message_type = UPLOADSTRING;
		break;
	case DELETE:
		message_type = DELETESTRING;
		break;
	case LIST:
		message_type = LISTSTRING;
		break;
	case REGISTERRESPONSE:
		message_type = REGISTERRESPONSESTRING;
		break;
	case DOWNLOADRESPONSE:
		message_type = DOWNLOADRESPONSESTRING;
		break;
	case UPLOADROSPONSE:
		message_type = UPLOADRESPONSESTRING;
		break;
	case DELETERESPONSE:
		message_type = DELETERESPONSESTRING;
		break;
	case LISTRESPONSE:
		message_type = LISTRESPONSESTRING;
		break;
	default:
		return -1;
	}
	fprintf(stderr, "Received message %s succeeded\n", message_type);
	strcpy(tmp, message + sizeof(uint32_t)/sizeof(char));
	fprintf(stderr, "Real message received = %s \n", tmp);
	return 0;
}

/*
 * sending message
 */
int send_message (int socket, struct sockaddr_in receiver_addr, char* message, char* message_type)
{
	char tmp[CHUNKSIZE];
	int port = receiver_addr.sin_port;
	fprintf(stderr, "Receiver port %d \n", port);
	fprintf(stderr, "Trying to send message %s \n", message_type);

	/*
	 * int sockfd, const void *buf, size_t len, int flags,
               const struct sockaddr *dest_addr, socklen_t addrlen);
	 */
	if(TEMP_FAILURE_RETRY(sendto(socket, message, CHUNKSIZE, 0, &receiver_addr, sizeof(struct sockaddr_in))) < 0)
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

	strcpy(tmp, message + 3*sizeof(uint32_t)/sizeof(char));

	fprintf(stderr, "Real message send =  %s  \n", tmp);
	return 0;
}

/*
 * at the end of executing programs free queue
 */
void free_queue()
{
	free(queue->elements);
	free(queue->access);
	free(queue);
}

void generate_package_amount(int* filesize, int real_package_size, int* package_amount, char* message)
{
	*filesize = get_file_size_from_message(message);
	if(*filesize == *filesize / real_package_size * real_package_size)
		*package_amount = *filesize / real_package_size;
	else
		*package_amount = *filesize / real_package_size + 1;
}

/*
 * create specified file, handle errors
 */
uint8_t create_file(char* real_file_name, int* filesize, int real_package_size,
		int* package_amount, uint8_t** packages, char* message)
{
	int fd;
	while(1)
	{
		fd = open(real_file_name, O_RDWR);
		if(fd < 0)
		{
			if(errno == ENOENT)
			{
				/* file does not exists */
				while(1)
				{
					fd = open(real_file_name, O_RDWR | O_CREAT, S_IRWXU | S_IRGRP | S_IROTH);
					if(fd < 0)
					{
						if(errno == EINTR)
							continue;
						else
						{
							fprintf(stderr, "Could not open file %s %s \n", real_file_name, strerror(errno));
							return 1;
						}
					}
					else
					{
						close_file(&fd, real_file_name);
						break;
					}
				}
				fprintf(stderr, "Creating new file \n");
				generate_package_amount(filesize, real_package_size, package_amount, message);
				*packages = (uint8_t*) calloc(*package_amount, sizeof(uint8_t));
				if(*packages == NULL)
				{
					fprintf(stderr, "Could not allocate memory for packages \n");
					return 1;
				}
				break;
			}
			else if(errno == EINTR)
				continue;
			else
			{
				fprintf(stderr, "Could not open file %s %s", real_file_name, strerror(errno));
				return 1;
			}
		}
		else
		{
			fprintf(stdout, "File already exists!\n");
			close_file(&fd, real_file_name);
			return 1;
		}
	}
	return 0;
}

uint32_t delete_status_from_list(char* file_name, char* searched_file_name)
{
	pthread_mutex_lock(file_access);
	int id, i;
	char* tmp_percentage = NULL;
	char filepath[FILENAME];
	char* done = NULL;
	FILE* file = fopen(file_name, "r");
	if(file == NULL)
		return 1;/* should check the result */
    char line[CHUNKSIZE];
	char all[CHUNKSIZE * CHUNKSIZE];
	int last = 0;
    while (fgets(line, sizeof(line), file)) {
		if(sscanf(line, "%d %s %s %s\n", &id, filepath, tmp_percentage, done) == EOF)
		{
			fprintf(stderr, "END OF SSCANF \n");
		}
		else
		{
			if(strcmp(filepath, searched_file_name) == 0)
			{
				memset(line, 0, CHUNKSIZE * sizeof(char));
			}
			for(i = 0; i < CHUNKSIZE; i++)
			{
				all[i + last] = line[i];
			}
			last = last + strlen(line);
		}
    }
    if(fclose(file) == EOF)
	{
		fprintf(stderr, "Could not close file %s \n", file_name);
				    pthread_mutex_unlock(file_access);
		return 1;
	}
    file = fopen(file_name, "w");
    if(file == NULL)
    {
		    pthread_mutex_unlock(file_access);
		return 1;/* should check the result */
	}
	fprintf(file, "%s", all);
	if(fclose(file) == EOF)
	{
		fprintf(stderr, "Could not close file %s \n", file_name);
		    pthread_mutex_unlock(file_access);

		return 1;
	}
    pthread_mutex_unlock(file_access);
    return 0;
}

/*
 * try to write record to file list, retrn 1 on errro, 0 on success
 */
uint32_t write_status_to_list(int message_id, char* file_name, char* searched_file_name, int percentage, int package_numbers, int last_package)
{
	pthread_mutex_lock(file_access);
	int id, i;
	char* tmp_percentage = NULL;
	char filepath[FILENAME];
	int tmp_package_numbers = 0;
	int tmp_last_package = 0;
	FILE* file = fopen(file_name, "r");
	if(file == NULL)
		return 1;/* should check the result */
    char line[CHUNKSIZE];
	char all[CHUNKSIZE * CHUNKSIZE];
	int last = 0;
	int exists = 0;
	if(percentage > 100)
		percentage = 100;
    while (fgets(line, sizeof(line), file)) {
		if(sscanf(line, "%d %s %s %d %d\n", &id, filepath, tmp_percentage, &tmp_package_numbers, &tmp_last_package) == EOF)
		{
			fprintf(stderr, "END OF SSCANF \n");
		}
		else
		{
			if(strcmp(filepath, searched_file_name) == 0)
			{
				memset(line, 0, CHUNKSIZE * sizeof(char));
				sprintf(line, "%d %s %d%% %d %d\n", message_id, searched_file_name, percentage, package_numbers, last_package);
				fprintf(stderr, "GOT LINE %s", line);
				exists = 1;
			}
			for(i = 0; i < CHUNKSIZE; i++)
			{
				all[i + last] = line[i];
			}
			last = last + strlen(line);
		}
    }
    if(exists == 0)
    {
		sprintf(line, "%d %s %d%% %d %d\n", message_id, searched_file_name, percentage, package_numbers, last_package);
		fprintf(stderr, "GOT NEW LINE %s", line);
		for(i = 0; i < CHUNKSIZE; i++)
			{
				all[i + last] = line[i];
			}
	}
    if(fclose(file) == EOF)
	{
		fprintf(stderr, "Could not close file %s \n", file_name);
				    pthread_mutex_unlock(file_access);
		return 1;
	}
    file = fopen(file_name, "w");
    if(file == NULL)
    {
		    pthread_mutex_unlock(file_access);
		return 1;/* should check the result */
	}
	fprintf(file, "%s", all);
	if(fclose(file) == EOF)
	{
		fprintf(stderr, "Could not close file %s \n", file_name);
		    pthread_mutex_unlock(file_access);

		return 1;
	}
    pthread_mutex_unlock(file_access);
    return 0;
}

/*
 * return 0 for successfull opening and creating file -> descriptor is closed
 */
uint8_t create_list_file(char* file_name)
{
	int fd;
	pthread_mutex_lock(file_access);
	while(1)
	{
		fd = open(file_name, O_RDWR | O_CREAT, S_IRWXU | S_IRGRP | S_IROTH);
		if(fd < 0)
		{
			if(errno == EINTR)
				continue;
			else
			{
				fprintf(stderr, "Could not open file %s %s", file_name, strerror(errno));
				    pthread_mutex_unlock(file_access);
				return 1;
			}
		}
		else
		{
			fprintf(stdout, "Success in creating list file\n");
			close_file(&fd, file_name);
			    pthread_mutex_unlock(file_access);
			return 0;
		}
	}
	    pthread_mutex_unlock(file_access);
	return 0;
}

/*
 * read all files to list if file is empty else do nothing
 */
uint8_t read_all_files_to_list(char* file_name)
{
	int i;
	int size = 0;
	DIR *d;
	int fd;
	struct dirent *dir;
	d = opendir(".");
	char record[CHUNKSIZE];
	char* file;
	pthread_mutex_lock(file_access);
	while(1)
	{
		fd = open(file_name, O_RDWR);
		if(fd < 0)
		{
			if(errno == EINTR)
				continue;
			else
			{
				fprintf(stderr, "Could not open file %s %s", file_name, strerror(errno));
				    pthread_mutex_unlock(file_access);
				return 1;
			}
		}
		else
		{
			fprintf(stdout, "Success in opening list file\n");
			size = get_file_size (file_name);
			if(size == -1) 
			{
				fprintf(stdout, "Could not read file size of list %s\n", file_name);
				    pthread_mutex_unlock(file_access);
				return 1;
			}
			if(size == 0)
			{
				/* add new records task_id file_name percentage array */
				if (d)
				{
					while ((dir = readdir(d)) != NULL)
					{
					  memset(record, 0, CHUNKSIZE);
					  file = dir->d_name;
					  if(strlen(file) == 0 || file[0] == '\0')
						continue;
					  record[0] = '0';
					  record[1] = ' ';
					  for(i = 0; i< strlen(file); i++)
					  {
						  record[2 + i] = file[i];
					  }
					  record[2+i] = ' ';
					  record[2+i+1] = '1';
					  record[2+i+2] = '0';
					  record[2+i+3] = '0';
					  record[2+i+4] = '%';
					  record[2+i+5] = ' ';
					  record[2+i+6] = '0';
					  record[2+i+7] = ' ';
					  record[2+i+8] = '0';
					  record[2+i+9] = '\n';
					  record[2+i+10] = '\0';
					  if(bulk_write(fd, record, strlen(record)) != strlen(record))
					  {
						  fprintf(stderr, "Could not write all bytes \n");
					  }
					}
					if(closedir(d) < 0)
						{
							fprintf(stderr, "Could not close dir\n");
							    pthread_mutex_unlock(file_access);
							return 1;
						}
				}
			}
			close_file(&fd, file_name);
			    pthread_mutex_unlock(file_access);
			return 0;
		}
	}
	    pthread_mutex_unlock(file_access);
	return 0;
}

/* return 0 if file exists */
uint8_t open_file(char* real_file_name, int *fd)
{
	while(1)
	{
		*fd = open(real_file_name, O_RDWR);
		if(*fd < 0)
		{
			if(errno == ENOENT)
			{
				/* file does not exists */
				return 1;
			}
			else if(errno == EINTR)
				continue;
			else
			{
				fprintf(stderr, "Could not open file %s %s", real_file_name, strerror(errno));
				return 1;
			}
		}
		else
		{
			fprintf(stdout, "File already exists!\n");
			close_file(fd, real_file_name);
			return 0;
		}
	}
	return 0;
}

/*
 * close file, handle errors
 */
void close_file(int* fd, char* real_file_name)
{
	while(close(*fd) < 0)
	{
		if(errno == EINTR)
			continue;
		else
		{
			fprintf(stderr, "Could not close file %s %s", real_file_name, strerror(errno));
			break;
		}
	}
}

/*
 * check top of queue and if type of message is not as expected push message to queue again
 * if any existed and return 1 else return 0;
 */
uint8_t check_top_of_queue(char* message_type, task_type* task, char* message, task_type expected_task, char* error_file_path)
{
	/* top from queue */
	fprintf(stderr, "Waiting to receive %s \n", message_type);
	if (top(queue, message) < 0)
	{
		fprintf(stderr, "Queue is empty, nothing to show \n");
		sleep(2);
		memset(message, 0, CHUNKSIZE * sizeof(char));
		/*message = calloc(CHUNKSIZE, sizeof(char));*/
		if(message == NULL)
		{
			fprintf(stderr, "Problem with allocating memory for message \n");
		}
		return 1;
	}
	*task = check_message_type(message);
	fprintf(stderr, "Real task number %d \n", (int)(*task));
	if(*task == ERROR)
	{
		get_filename_from_message(message, error_file_path);
		fprintf(stderr, "Got task with type ERROR for filename %s \n", error_file_path);
		return 1;
	}
	if(*task != expected_task)
	{
		/* push to the end of queue */
		fprintf(stderr, "Task type is not %s\n", message_type);
		push(queue, message);
		sleep(1);
		return 1;
	}
	return 0;
}
