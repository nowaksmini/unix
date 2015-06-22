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

volatile sig_atomic_t id = 0;

/*
 * inform user about running program
 */
void usage(char *name)
{
	fprintf(stderr, "USAGE: %s port workdir \n",name);
	exit(EXIT_FAILURE);
}

void generate_failed_delete_response(char* message, int message_id, char* filepath, int socket, struct sockaddr_in client_addr)
{
	put_id_to_message(message, message_id);
	strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, DELETEERESPONSEERROR);
	fprintf(stderr, "Server pushed filename %s and id %u to unsuccessful delete register response message \n", filepath, message_id);
	send_message(socket, client_addr, message, DELETERESPONSESTRING);
}

/*
 * message send to client to inform about ability to delete file
 * generating new task id an removing file if can
 */
void generate_delete_response_message(task_type task, char* message, int socket, struct sockaddr_in client_addr)
{
	int i;
	int fd = -1;;
	char filepath[FILENAME];
	char * real_file_name;
	int first_empty_sign = FILENAME - 1;
	int message_id = 0;
	memset(filepath, 0, FILENAME);
	get_filename_from_message(message, filepath);
	save_massage_type_to_message(DELETERESPONSE, message);

	for(i = 0; i < FILENAME; i++)
	{
		if(filepath[i] == '\0' || filepath[i] == '\n')
		{
			first_empty_sign = i;
			break;
		}
	}
	fprintf(stderr, "Empty sign found at position : %d \n", first_empty_sign);
	real_file_name = (char *)calloc(first_empty_sign, sizeof(char));
	if(real_file_name == NULL)
	{
		generate_failed_delete_response(message, message_id, filepath, socket, client_addr);
	}
	for(i = 0; i < first_empty_sign; i++)
	{
		real_file_name[i] = filepath[i];
	}
	fprintf(stderr, "Real file name : %s \n", real_file_name);
	fprintf(stderr, "Real file name size %zu \n", strlen(real_file_name));
	save_massage_type_to_message(DELETERESPONSE, message);
	strcpy(message + 3*sizeof(uint32_t)/sizeof(char), filepath);
	if(open_file(real_file_name, &fd) == 1)
	{
		generate_failed_delete_response(message, message_id, filepath, socket, client_addr);
	}
	else
	{
		id = id + 1;
		message_id = id;
		put_id_to_message(message, message_id);
		strcpy(message + 3*sizeof(uint32_t)/sizeof(char), filepath);
		strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, DELETERESPONSESUCCESS);
		send_message(socket, client_addr, message, DELETERESPONSESTRING);
		sleep(1);
		while (1)
		{
			put_id_to_message(message, message_id);
			save_massage_type_to_message(DELETE, message);
			strcpy(message + 3*sizeof(uint32_t)/sizeof(char), filepath);
			if(remove(real_file_name) < 0)
			{
				if(errno == EBUSY)
					continue;
				fprintf(stderr, "Removed file failed %s \n", real_file_name);
				strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, DELETEACCESSDENYIED);
				break;
			}
			else
			{
				fprintf(stderr, "Removed file %s success\n", real_file_name);
				strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, DELETERESPONSESUCCESS);
				break;
			}
		}
		send_message(socket, client_addr, message, DELETESTRING);
	}
	free(real_file_name);
}

/* TO DO */

/*
 * message send to client to inform about receiving new file size, md5
 * generating new task id
 */
void generate_upload_response_message(int* my_id, char* message, int socket, struct sockaddr_in client_addr)
{
	int fd; /*file descriptor */
	int i;
	struct stat sts;
	char filepath[FILENAME];
	char * real_file_name;
	unsigned char md5_sum[MD5LENGTH];
	int first_empty_sign = FILENAME - 1;
	int filesize = 0;
	int message_id = 0;
	int tmp_id;
	int package_amount;
	int package_number = 0;
	char * file_contents;
	int real_package_size = CHUNKSIZE - 3 * sizeof(uint32_t)/sizeof(char);
	char *package;
	char *buf;
	FILE *file;
	package = malloc(real_package_size);
	memset(message, 0, CHUNKSIZE);
	memset(filepath, 0, FILENAME);
	memset(md5_sum, 0, MD5LENGTH);
	get_filename_from_message(message, filepath);
	save_massage_type_to_message(UPLOADROSPONSE, message);
	tmp_id = get_id_from_message(message);
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
	for(i = 0; i < first_empty_sign; i++)
	{
		real_file_name[i] = filepath[i];
	}
	fprintf(stderr, "Real file name : %s \n", real_file_name);
	fprintf(stderr, "Real file name size %zu \n", strlen(real_file_name));
	filesize = get_file_size_from_message(message);
	if(filesize == filesize / real_package_size * real_package_size)
		package_amount = filesize / real_package_size;
	else
		package_amount = filesize / real_package_size + 1;
	if(tmp_id == 0)
	{
		if (stat(real_file_name, &sts) == -1 && errno == ENOENT)
		{
			/* no such file, id is 0 */
			id = id + 1;
			*my_id = id;
			save_massage_type_to_message(UPLOADROSPONSE, message);
			put_id_to_message(message, id);
			strcpy(message + 3*sizeof(uint32_t)/sizeof(char), filepath);
			strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, UPLOADRESPONSESUCCESS);
			fd = open(real_file_name, O_RDWR | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH);
			fprintf(stderr, "Creating new file \n");
			buf = calloc(filesize, sizeof(char));
			for(i = 0; i< filesize; i++)
				buf[i] = '0';
			bulk_write(fd, buf, filesize);
			fprintf(stderr, "Server pushed filename %s and id %u to successfull upload register response message \n", filepath, id);
			send_message(socket, client_addr, message, UPLOADRESPONSESTRING);
			close(fd);

		}
		else
		{
			save_massage_type_to_message(UPLOADROSPONSE, message);
			put_id_to_message(message,message_id);
			strcpy(message + 3*sizeof(uint32_t)/sizeof(char), filepath);
			strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, UPLOADRESPONSEERROR);
			fprintf(stderr, "Server pushed filename %s and id %u to unsuccessfull upload register response message \n", filepath, id);
			send_message(socket, client_addr, message, UPLOADRESPONSESTRING);
			return;
		}
	}
	else
	{
		if(tmp_id != *my_id)
		{
			push(queue, message);
		}
		else
		{
			fprintf(stderr, "Got data for update \n");
			package_number = get_file_size_from_message(message);
			if(package_number == package_amount)
			{
				fprintf(stderr, "Checking md5 sums \n");
				file_contents = read_whole_file (real_file_name);
				if(file_contents == NULL)
				{
					free (file_contents);
					free (package);
					free(real_file_name);
					return;
				}
				compute_md5(file_contents, md5_sum);
				for(i = 0; i< MD5LENGTH; i++)
				{
					if(md5_sum[i] != package[i])
					{
						fprintf(stdout, "Wrong md5 sum %s \n", real_file_name);
						return;
					}
				}

			}
			else
			{
				fprintf(stderr, "Write data to uploade file");
				fd = open(real_file_name, O_RDWR | O_CREAT, S_IRUSR | S_IRGRP | S_IROTH);
				strcpy(package, message + 3 * sizeof(uint32_t)/sizeof(char) + FILENAME);
				file = fopen(real_file_name, "w");
				fputs(package, file);
				fseek(file, package_number * real_package_size, SEEK_SET);
				fclose(file);
				close(fd);
			}

		}
	}

	sleep(1);


	free (file_contents);
	free (package);
	free(real_file_name);
}

/*
 * generate message to inform client about any error during download task
 */
void generate_failed_download(task_type response_task, char* response_error, char* message, char * filepath, int message_id)
{
	save_massage_type_to_message(response_task, message);
	put_id_to_message(message, message_id);
	strcpy(message + 3*sizeof(uint32_t)/sizeof(char), filepath);
	strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, response_error);
	fprintf(stderr, "Server pushed filename %s to unsuccessfull download register response message \n", filepath);
}

/*
 * send download response, return 1 if error, 0 means that client can download file
 */
uint8_t generate_download_response_message(task_type response_task, char* response_error, char* response_success,
											char* real_file_name, char* message, char* filepath, int message_id, 
											char** file_contents, unsigned char* md5_sum, int* tmp_id, int* size)
{
	int fd;
	struct stat sts;
	if (stat(real_file_name, &sts) == -1)
	{
		generate_failed_download(response_task, response_error, message, filepath, message_id);
		return 1;
	}
	else
	{
		*size = sts.st_size;
		if ((fd = TEMP_FAILURE_RETRY(open(real_file_name, O_RDONLY))) == -1)
		{
			fprintf(stderr, "Could not open file %s \n", real_file_name);
			generate_failed_download(response_task, response_error, message, filepath, message_id);
			return 1;
		}
		else
		{
			id = id + 1;
			*tmp_id = id;
			put_id_to_message(message, id);
			put_size_to_message((uint32_t)*size, message);
			*file_contents = read_whole_file (real_file_name);
			if(file_contents == NULL)
			{
				generate_failed_download(response_task, response_error, message, filepath, message_id);
				return 1;
			}
			compute_md5(*file_contents, md5_sum);
			strcpy(message + 3*sizeof(uint32_t)/sizeof(char), filepath);
			strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, response_success);
		}
		close_file(&fd, real_file_name);
	}
	return 0;
}

void send_file_packages(task_type task, char* task_message, char* message, int tmp_id, char* file_contents, char* package, unsigned char* md5_sum,
		int socket, struct sockaddr_in client_addr)
{
	int i, j;
	int package_amount;
	int real_package_size = CHUNKSIZE - 3 * sizeof(uint32_t)/sizeof(char);
	save_massage_type_to_message(task, message);
	put_id_to_message(message, tmp_id);
	if(tmp_id != 0)
	{
		if(strlen(file_contents) == strlen(file_contents) / real_package_size * real_package_size)
			package_amount = strlen(file_contents) / real_package_size;
		else
			package_amount = strlen(file_contents) / real_package_size + 1;

		for(i = 0; i < package_amount; i++)
		{
			memset(message, 0, CHUNKSIZE);
			save_massage_type_to_message(task, message);
			for(j = 0; j < real_package_size; j ++)
			{
				if(i * real_package_size + j < strlen(file_contents))
				{
					package[j] = file_contents[i * real_package_size + j];
				}
				else
				{
					package[j] = '\0';
				}
			}
			put_id_to_message(message,tmp_id);
			put_size_to_message((uint32_t)i, message);
			strcpy(message + 3*sizeof(uint32_t)/sizeof(char), package);
			fprintf(stderr, "Sending data package number %d for task id %d : %s \n", i, tmp_id, package);
			send_message(socket, client_addr, message, task_message);
			sleep(1);
		}
		memset(message, 0, CHUNKSIZE);
		save_massage_type_to_message(task, message);
		put_size_to_message((uint32_t)i, message);
		put_id_to_message(message,tmp_id);
		fprintf(stderr, "Sending md5 sum for task id %d \n", tmp_id);
		strcpy(message + 3*sizeof(uint32_t)/sizeof(char), (char*)md5_sum);
		send_message(socket, client_addr, message, task_message);
		sleep(1);
	}
}

/*
 * function for responding for download request and list request
 */
void read_file(task_type task, char* messagein, int socket, struct sockaddr_in client_addr)
{
	int i;
	char filepath[FILENAME];
	char message[CHUNKSIZE];
	char * real_file_name;
	unsigned char md5_sum[MD5LENGTH];
	int size = 0;
	int first_empty_sign = FILENAME - 1;
	int message_id = 0;
	int tmp_id;
	char * file_contents;
	int real_package_size = CHUNKSIZE - 3 * sizeof(uint32_t)/sizeof(char);
	char *package;
	char * task_message;
	task_type response_task;
	char* response_error;
	char* response_success;
	package = malloc(real_package_size);
	if(package == NULL)
	{
		fprintf(stderr, "Problem with allocation memory for package \n");
		return;
	}
	memset(message, 0, CHUNKSIZE);
	memset(filepath, 0, FILENAME);
	memset(md5_sum, 0, MD5LENGTH);
	get_filename_from_message(messagein, filepath);
	if(task == DOWNLOAD)
	{
		response_task = DOWNLOADRESPONSE;
		response_error = DOWNLOADRESPONSEERROR;
		response_success = DOWNLOADRESPONSESUCCESS;
	}
	else
	{
		response_task = LISTRESPONSE;
		response_error = LISTRESPONSEERROR;
		response_success = LISTRESPONSESUCCESS;
	}
	save_massage_type_to_message(response_task, message);

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
	if(real_file_name == NULL)
	{
		fprintf(stderr, "Problem with allocation memory for file name \n");
		free(package);
		return;
	}
	for(i = 0; i < first_empty_sign; i++)
	{
		real_file_name[i] = filepath[i];
	}
	fprintf(stderr, "Real file name : %s \n", real_file_name);
	fprintf(stderr, "Real file name size %zu \n", strlen(real_file_name));

	if(generate_download_response_message(response_task, response_error, response_success, real_file_name, message, 
		filepath, message_id, &file_contents, md5_sum, &tmp_id, &size) == 0)
	{
		if(task == DOWNLOAD)
			task_message = DOWNLOADRESPONSESTRING;
		else
			task_message = LISTRESPONSESTRING;
		send_message(socket, client_addr, message, task_message);
		sleep(1);
		/*
		 * clear message
		 */
		memset(message, 0, CHUNKSIZE);
		/*
		 * divide whole file to smaller one packages of size CHUNKSIZE - TYPE_LENGTH - TASK_ID_LENGTH - PACKAGE_NUMBER
		 */
		if(task == DOWNLOAD)
			task_message = DOWNLOADSTRING;
		else
			task_message = LISTSTRING;
		send_file_packages(task, task_message, message, tmp_id, file_contents, package, md5_sum,
				socket, client_addr);
	}
	free (file_contents);
	free (package);
	free(real_file_name);
	return;
}

/*
 * message send to client to inform about server address
 */
void generate_register_response_message(char* message)
{
	memset(message, 0, CHUNKSIZE);
	save_massage_type_to_message(REGISTERRESPONSE, message);
	strcpy(message + 3*sizeof(uint32_t)/sizeof(char), REGISTERRESPONSESUCCESS);
}

/*
 * send register response confirmation
 */
void send_register_response(task_type task, char* messagein, int socket, struct sockaddr_in client_addr)
{
	generate_register_response_message(messagein);
	if(send_message(socket, client_addr, messagein, REGISTERRESPONSESTRING) < 0)
	{
		ERR("SEND REGISTERRESPONSE");
	}
}

void *server_send_register_response_function(void *arg)
{
	return server_send_response_function(arg, "REGISTER", REGISTER, send_register_response);
}

void *server_send_download_response_function(void *arg)
{
	return server_send_response_function(arg, "DOWNLOAD", DOWNLOAD, read_file);
}

void *server_send_list_response_function(void *arg)
{
	return server_send_response_function(arg, "LIST", LIST, read_file);
}

void *server_send_delete_response_function(void *arg)
{
	return server_send_response_function(arg, "DELETE", DELETE, generate_delete_response_message);
}

void *server_send_upload_response_function(void *arg)
{
	return server_send_response_function(arg, "UPDATE", UPLOAD, read_file);
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
 * create ip structure from ip written in char array and port
 */
struct sockaddr_in make_address(uint16_t port)
{
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	fprintf(stderr, "My address %u \n", htons(port));
	/*
	 * receiving from anybody
	 */
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	return addr;
}

/*
 * create socket and return new socket for address for listening
 */
int connect_socket(struct sockaddr_in address)
{
	int socketfd;
	socketfd = make_socket();
	if(bind(socketfd, (struct sockaddr*) &address, sizeof(address)) < 0)
		ERR("bind");
	return socketfd;
}

/*
 * create threads
 */
void init(pthread_t *thread, thread_arg *targ, int *socket, struct sockaddr_in* client_addr, task_type task)
{
	targ[0].socket = socket;
	targ[0].server_addr = client_addr;
	switch(task)
	{
	case REGISTER:
		if (pthread_create(&thread[0], NULL, server_send_register_response_function, (void *) &targ[0]) != 0)
			ERR("pthread_create");
		break;
	case DOWNLOAD:
		if (pthread_create(&thread[0], NULL, server_send_download_response_function, (void *) &targ[0]) != 0)
			ERR("pthread_create");
		break;
	case DELETE:
		if (pthread_create(&thread[0], NULL, server_send_delete_response_function, (void *) &targ[0]) != 0)
			ERR("pthread_create");
		break;
	case UPLOAD:
		if (pthread_create(&thread[0], NULL, server_send_upload_response_function, (void *) &targ[0]) != 0)
			ERR("pthread_create");
		break;
	case LIST:
		if (pthread_create(&thread[0], NULL, server_send_list_response_function, (void *) &targ[0]) != 0)
			ERR("pthread_create");
		break;
	default:
		ERR("INIT");
	}
	if (pthread_detach(thread[0]) !=  0)
		ERR("detach");
}

/*
 * main thread function receiving all communicated and creating new threads
 */
void do_work(int socket)
{
	char message[CHUNKSIZE];
	task_type task;
	struct sockaddr_in client_addr;
	pthread_t thread;
	thread_arg targ;
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
			if(task == REGISTER || task == DOWNLOAD || task == DELETE || task == UPLOAD || task == LIST)
			{
				push(queue, message);
				init(&thread, &targ, &socket, &client_addr, task);
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
	 * my_endpoint_listening_addr - address for listening from everybody
	 */
	struct sockaddr_in  my_endpoint_listening_addr;

	/*
	 * socket for sending and receiving data from specified address
	 */
	int socket;
	work = 1;
	if (argc!=3)
		usage(argv[0]);
	if (chdir(argv[2]) == -1)
		ERR("chdir");
	sethandler(SIG_IGN, SIGPIPE);
	sethandler(siginthandler, SIGINT);

	my_endpoint_listening_addr = make_address(atoi(argv[1]));

	socket = connect_socket(my_endpoint_listening_addr);

	queue = createQueue(QUEUECAPACITY);
	fprintf(stderr, "Created queue \n");

	do_work(socket);
	free_queue();
	return EXIT_SUCCESS;
}
