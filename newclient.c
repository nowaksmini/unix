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



void init(pthread_t *thread, thread_arg *targ, int *socket, struct sockaddr_in* server_addr, task_type task,
		char filepath [FILENAME], int package_amount, int package_number);


/*
 * inform user about running program
 */
void usage(char *name)
{
	fprintf(stderr, "USAGE: %s server_port\n", name);
	exit(EXIT_FAILURE);
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
 * fill message with necessary information about registration
 */
void generate_register_message(char* message)
{
	memset(message, 0, CHUNKSIZE);
	save_massage_type_to_message(REGISTER, message);
	strcpy(message + sizeof(uint32_t)/sizeof(char), CLIENTREQUESTS);
}


/*
 * send request to download file
 */
void download_request_work(char* message_type, task_type task, int socket, struct sockaddr_in server_addr, char* filepath)
{
	char message_to_send[CHUNKSIZE];
	memset(message_to_send, 0, CHUNKSIZE);
	save_massage_type_to_message(task, message_to_send);
	strcpy(message_to_send + 3*sizeof(uint32_t)/sizeof(char), filepath);
	fprintf(stderr, "Path put inside message %s \n", filepath);
	if(send_message(socket, server_addr, message_to_send, message_type) < 0)
	{
		ERR("SEND");
	}
	sleep(1);
}

/*
 * create upload request, send request for server
 * message contains type, 0 id, size of file, name of file, string for server
 * if success return 0 else return 1
 */
uint8_t upload_request_work(char* message_to_send, int socket, struct sockaddr_in server_addr, char* filepath)
{
	int size;
	memset(message_to_send, 0, CHUNKSIZE);
	save_massage_type_to_message(UPLOAD, message_to_send);
	strcpy(message_to_send + 3*sizeof(uint32_t)/sizeof(char), filepath);
	fprintf(stderr, "Path put inside message %s \n", filepath);
	size = get_file_size (filepath);
	if(size == -1)
	{
		fprintf(stderr, "Could not upload file %s \n", filepath);
		return 1;
	}
	else
	{
		strcpy(message_to_send + 3*sizeof(uint32_t)/sizeof(char), filepath);
		put_size_to_message((uint32_t)size, message_to_send);
		strcpy(message_to_send + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, UPLOADSTRING);
	}
	if(send_message(socket, server_addr, message_to_send, UPLOADSTRING) < 0)
	{
		ERR("SEND");
	}
	sleep(1);
	return 0;
}


void delete_request_work(int socket, struct sockaddr_in server_addr, char* filepath)
{
	char message_to_send[CHUNKSIZE];
	memset(message_to_send, 0, CHUNKSIZE);
	save_massage_type_to_message(DELETE, message_to_send);
	strcpy(message_to_send + 3*sizeof(uint32_t)/sizeof(char), filepath);
	fprintf(stderr, "Path put inside message %s \n", filepath);
	if(send_message(socket, server_addr, message_to_send, DELETESTRING) < 0)
	{
		ERR("SEND");
	}
	sleep(1);
}


/*
 * thread function for listening anything from server
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
	if(fgets(input_message, max, stdin) == NULL)
	{
		ERR("fgets");
	}
	task = get_task_type_from_input(input_message);
	if(sscanf(input_message, "%s %s", request, filepath) == EOF)
	{
		fprintf(stderr, "PROBLEM WITH SSCANF \n");
	}
	else
	{
		if(task == LIST)
		{
			memset(filepath, 0 , FILENAME * sizeof(char));
			strcpy(filepath, LISTFILE);
		}
		fprintf(stderr, "Got task type %s \n", request);
		fprintf(stderr, "Got file name to work %s  \n", filepath);
		/*
		 * run new thread for communication and downloading
		 */
		init(&thread, &targ, &socket, &server_addr, task, filepath, 0 , 0);
	}
}

/*
 * thread function for pushing messages from server to queue
 */
void server_listening_work(int socket, struct sockaddr_in server_addr)
{
	task_type task = NONE;
	char message_received[CHUNKSIZE];
	char* information = NULL;
	if(receive_message(socket, &server_addr, message_received) < 0)
	{
		fprintf(stderr, "Problem with receiving message \n");
	}
	task = check_message_type(message_received);
	fprintf(stderr, "Directly received from server task = %d \n", (int)task);
	push(queue, message_received);
	switch(task)
	{
	case DELETERESPONSE:
		information = DELETERESPONSESTRING;
		break;
	case UPLOADROSPONSE:
		information = UPLOADRESPONSESTRING;
		break;
	case DOWNLOADRESPONSE:
		information = DOWNLOADRESPONSESTRING;
		break;
	case LISTRESPONSE:
		information = LISTRESPONSESTRING;
		break;
	case DELETE:
		information = DELETESTRING;
		break;
	case UPLOAD:
		information = UPLOADSTRING;
		break;
	case LIST:
		information = LISTSTRING;
		break;
	case DOWNLOAD:
		information = DOWNLOADSTRING;
		break;
	default:
		information = ERRORSTRING;
	}
	fprintf(stderr, "%s: %s \n", RECEIVEDINTHREAD, information);
}

/*
 * thread function for listening messages from server after successful detecting server's address
 */
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

/*
 * thread function for listening commands from stdin
 */
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

/*
 * wait for download response, if thread should stop executing return 1 else 0
 * expected_task is DOWNLOADRESPONSE or LISTRESPONSE, message_type is a string of it
 */
uint8_t wait_for_download_response(task_type expected_task, char* message_type, char* error_file_path,
		char* message, task_type *type, int* id, char* file_path, char* old_file_path,
		char* communicate, char** real_file_name, int* package_amount, uint8_t** packages)
{
	int i;
	int first_empty_sign;
	int filesize = 0;
	int tmp_id = -1;
	int real_package_size = CHUNKSIZE - 3 * sizeof(uint32_t)/sizeof(char);
	while(work)
	{
		if(check_top_of_queue(message_type, type, message, expected_task, error_file_path) == 1)
		{
			continue;
		}
		/* waiting for confirmation or rejection of downloading file*/
		if(*id == -1)
		{
			/* check the filepath saved in message*/
			get_filename_from_message(message, file_path);
			for(i = 0; i < FILENAME; i++)
			{
				if(file_path[i] != old_file_path[i])
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
					fprintf(stdout, "Could not download file %s, reason : %s \n", file_path, communicate);
					push(queue, message);
					return 1;
				}
				else
				{
					*id = tmp_id;
					fprintf(stdout, "Server confirmed downloading file  %s, message : %s \n", file_path, communicate);
					fprintf(stdout, "Got id %d \n", *id);
					/* check if file exists if not create else check if is opened if not remove data and write own */
					for(i = 0; i < FILENAME; i++)
					{
						if(file_path[i] == '\0' || file_path[i] == '\n')
						{
							first_empty_sign = i;
							break;
						}
					}
					fprintf(stderr, "Empty sign found at position : %d \n", first_empty_sign);
					*real_file_name = (char*)calloc(first_empty_sign , sizeof(char));
					if(*real_file_name == NULL)
					{
						fprintf(stderr, "Could not allocate memory for file name \n");
						return 1;
					}
					for(i = 0; i < first_empty_sign; i++)
					{
						(*real_file_name)[i] = file_path[i];
					}
					fprintf(stderr, "Real file name size %d after malloc \n", first_empty_sign);
					fprintf(stderr, "EXPECTED TASK %d \n", (int)expected_task);
					if(expected_task == DOWNLOADRESPONSE)
						return create_file(*real_file_name, &filesize, real_package_size , package_amount, packages, message);
					else
					{
						generate_package_amount(&filesize, real_package_size, package_amount, message);
						return 0;
					}
				}
			}
		}
	}
	return 0;
}

/*
 * handle packages with id same as thread id and task expected_type DOWNLOAD or LIST
 */
void* wait_for_packages(char* message_type, task_type expected_type, char *message, char* error_file_path,
		int id, int package_amount, int should_download, char* real_file_name, uint8_t* packages)
{
	task_type task;
	int i;
	int correct_sum = 0;
	int tmp_id;
	int package_number;
	char* file_contents = NULL;
	unsigned char md5_sum[MD5LENGTH];
	char* package = NULL;
	int real_package_size = CHUNKSIZE - 3 * sizeof(uint32_t)/sizeof(char);
	package = malloc(real_package_size);
	int fd;
	if(package == NULL)
	{
		fprintf(stderr, "Problem with allocating memory for package \n");
		free(package);
		return NULL;
	}

	while(work)
	{
		fprintf(stderr, "Waiting to receive %s \n", message_type);
		if(check_top_of_queue(message_type, &task, message, expected_type, error_file_path) == 1)
		{
			continue;
		}
		else
		{
			tmp_id = get_id_from_message(message);
			if(tmp_id == id)
			{
				/*file exists or some problems occurred earlier
	       need to take from queue all messages for this task*/

				fprintf(stderr, "Id of message and thread are the same \n");
				fprintf(stderr, "Got data for update \n");
				package_number = get_file_size_from_message(message);
				memset(package, 0, real_package_size * sizeof(char));
				strcpy(package, message + 3 * sizeof(uint32_t)/sizeof(char));
				if(write_status_to_list(tmp_id, CLIENTLISTFILE, real_file_name, package_number * 100 / package_amount, package_amount, package_number, (int)expected_type) == 1)
				{
					fprintf(stderr, "Could not update list \n");
				}
				if(expected_type == LIST)
				{
					if(package_number != package_amount)
					{
						fprintf(stdout, "LIST PART %d : %s \n", package_number, package);
						continue;
					}
					else
					{
						fprintf(stdout, "Whole list received \n");
						break;
					}
				}
				else
				{
					if(package_number == package_amount)
					{
						fprintf(stderr, "Checking md5 sums \n");
						file_contents = read_whole_file (real_file_name);
						if(file_contents == NULL)
						{
							break;
						}
						memset(md5_sum, 0, MD5LENGTH);
						compute_md5(file_contents, md5_sum);
						fprintf(stderr, "Got md5 sum %s for file %s \n", md5_sum, real_file_name);
						for(i = 0; i< MD5LENGTH; i++)
						{
							if(md5_sum[i] == '\0')
							{
								correct_sum = 1;
								break;
							}
							if(((int)md5_sum[i] - (int)package[i] ) % 256 != 0)
							{
								correct_sum = 0;
								break;
							}
						}
						if(correct_sum == 1 || i == MD5LENGTH)
							fprintf(stdout, "Md5 sums correct for file %s \n", real_file_name);
						else
							fprintf(stdout, "Wrong md5 sum %s for field %d %d %d \n", real_file_name, i, (int)md5_sum[i], (int)package[i]);
						break;
					}
					else
					{
						fprintf(stderr, "Write data to update file \n");
						if ((fd = TEMP_FAILURE_RETRY(open(real_file_name, O_RDWR))) == -1)
						{
							fprintf(stderr, "Could not open file %s %s \n", real_file_name, strerror(errno));
							break;
						}
						fprintf(stderr, "Package number %d \n", package_number);
						if(package_number == 0 || packages[package_number-1] == 1)
						{
							packages[package_number] = 1;
							if(lseek(fd, 0, SEEK_END) < 0)
							{
								fprintf(stderr, "Could not write.\n");
								close_file(&fd, real_file_name);
								continue;
							}
							/* strlen(package) */
							bulk_write(fd, package, real_package_size );
							fprintf(stderr, "Package was written to file %s \n", real_file_name);
							close_file(&fd, real_file_name);
						}
						else
						{
							fprintf(stderr, "Pushing message back to queue \n");
							push(queue, message);
						}
					}
				}

			}
			else
			{
				fprintf(stderr, "Pushing message back to queue \n");
				push(queue, message);
			}
		}
	}
	free(package);
	if(expected_type == DOWNLOAD)
		free(file_contents);
	return NULL;
}

/*
 * wait for delete response
 */
void wait_for_delete_response(task_type* task, char* error_file_path, char* old_file_path, char* message, int* id,
		char* filepath, char* communicate, int* wait_for_delete_raport)
{
	int tmpid = -1;
	int i;
	while(work)
	{
		if(check_top_of_queue("DELETERESPONSE", task, message, DELETERESPONSE, error_file_path) == 1)
			continue;
		/* waiting for confirmation or rejection of downloading file*/
		if(*id == -1)
		{
			/* check the filepath saved in message*/
			get_filename_from_message(message, filepath);
			for(i = 0; i < FILENAME; i++)
			{
				if(filepath[i] != old_file_path[i])
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
					*wait_for_delete_raport = 0;
					break;
				}
				else
				{
					*id = tmpid;
					fprintf(stdout, "Server confirmed planned deleting file  %s, message : %s \n", filepath, communicate);
					fprintf(stdout, "Got id %d \n", *id);
					break;
				}
			}
		}
	}
}

/*
 * download expected_type is DOWNLOAD or LIST, message_type is string of type
 */
void *common_download_function(void *arg, char* message_type, char* task_string, task_type expected_type)
{
	int clientfd;
	int id = -1;
	int package_amount = 0;
	struct sockaddr_in server_addr;
	thread_arg targ;
	char* filepath = NULL;
	int i;
	char* error_file_path = NULL;
	char oldFilePath[FILENAME];
	char communicate [CHUNKSIZE - 3* sizeof(uint32_t)/ sizeof(char) - FILENAME];
	char* real_file_name = NULL;
	char* message = NULL;
	task_type task;
	uint8_t * packages = NULL ;
	uint8_t should_download;
	task_type expected_repsponse_type;
	char* response_message_type = NULL;
	message = calloc(CHUNKSIZE, sizeof(char));
	memcpy(&targ, arg, sizeof(targ));
	error_file_path = (char*) calloc(FILENAME, sizeof(char));
	if(error_file_path != NULL && message != NULL)
	{
		if (work)
		{
			clientfd = *targ.socket;
			server_addr = *targ.server_addr;
			filepath = targ.filename;
			download_request_work(task_string, expected_type, clientfd, server_addr, filepath);
		}
		for(i = 0; i< FILENAME; i++)
		{
			oldFilePath[i] = filepath[i];
		}
		/*
		 * receive communicate from server about existing file or not
		 */
		if(expected_type == DOWNLOAD)
		{
			expected_repsponse_type = DOWNLOADRESPONSE;
			response_message_type = "DOWNLOADRESPONSE";
		}
		else
		{
			expected_repsponse_type = LISTRESPONSE;
			response_message_type = "LISTRESPONSE";
		}
		should_download = wait_for_download_response(expected_repsponse_type, response_message_type, error_file_path, message, &task, &id, filepath,
				oldFilePath, communicate, &real_file_name, &package_amount, &packages);
		if(should_download == 0)
		{
			fprintf(stderr, "Waiting for datagrams with file content\n");
			wait_for_packages(message_type, expected_type, message, error_file_path, id, package_amount, should_download, real_file_name, packages);
		}
	}

	free(message);
	free(error_file_path);
	free(real_file_name);
	fprintf(stderr, "Destroying %s thread\n", message_type);
	pthread_exit(&targ);
	return NULL;
}

/*
 * thread function for downloading file
 */
void *download_thread_function(void *arg)
{
	return common_download_function(arg, "DOWNLOAD", DOWNLOADSTRING, DOWNLOAD);
}

/*
 * thread function for uploading file
 */
void *upload_thread_function(void *arg)
{
	int clientfd;
	int id = -1;
	int tmp_id = -1;
	int i, j;
	unsigned char md5_sum[MD5LENGTH];
	struct sockaddr_in server_addr;
	thread_arg targ;
	char* filepath = NULL;
	char* error_file_path = NULL;
	char oldFilePath[FILENAME];
	char communicate [CHUNKSIZE - 3* sizeof(uint32_t)/ sizeof(char) - FILENAME];
	char * real_file_name = NULL;
	int filesize = 0;
	char* message = NULL;
	int first_empty_sign;
	task_type task;
	int package_amount = 0;
	char * file_contents = NULL;
	int real_package_size = CHUNKSIZE - 3 * sizeof(uint32_t)/sizeof(char);
	char *package = NULL;
	package = malloc(real_package_size);

	if(package == NULL)
	{
		fprintf(stderr, "Could not allocate memory for package \n");
	}
	else
	{
		error_file_path = malloc(FILENAME);
		if(error_file_path == NULL)
		{
			fprintf(stderr, "Could not allocate memory for tmp path file \n");
		}
		else
		{
			message = calloc(CHUNKSIZE, sizeof(char));
			if(message == NULL)
			{
				fprintf(stderr, "Could not allocate memory for message \n");
			}
			else
			{
				memcpy(&targ, arg, sizeof(targ));
				if (work)
				{
					clientfd = *targ.socket;
					server_addr = *targ.server_addr;
					filepath = targ.filename;
					if(upload_request_work(message, clientfd, server_addr, filepath) == 0)
					{
						for(i = 0; i< FILENAME; i++)
						{
							oldFilePath[i] = filepath[i];
						}
						fprintf(stderr, "Waiting for uploadrequest\n");
						/*
						 * receive communicate from server about response for update
						 */
						while(work)
						{
							if(check_top_of_queue("UPLOADRESPONSE", &task, message, UPLOADROSPONSE, error_file_path) == 1)
								continue;
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
										break;
									}
									else
									{
										id = tmp_id;
										fprintf(stdout, "Server confirmed uploading file  %s, message : %s \n", filepath, communicate);
										fprintf(stdout, "Got id %d \n", id);
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
											break;
										}
										for(i = 0; i < first_empty_sign; i++)
										{
											real_file_name[i] = filepath[i];
										}
										fprintf(stderr, "Real file name size %d after malloc \n", first_empty_sign);
										file_contents = read_whole_file (real_file_name);
										if(file_contents == NULL)
										{
											fprintf(stderr, "Could not read file \n");
											break;
										}
										generate_package_amount(&filesize, real_package_size, &package_amount, message);
										compute_md5(file_contents, md5_sum);
										for(i = 0; i < package_amount; i++)
										{
											memset(package, 0, real_package_size * sizeof(char));
											for(j = 0; j < real_package_size; j++)
											{
												package[j] = file_contents[i* real_package_size + j];
											}
											memset(message, 0, CHUNKSIZE * sizeof(char));
											save_massage_type_to_message(UPLOADROSPONSE, message);
											put_id_to_message(message,tmp_id);
											put_size_to_message((uint32_t)i, message);
											strcpy(message + 3*sizeof(uint32_t)/sizeof(char), package);
											fprintf(stderr, "Sending data package number %d for task id %d : %s \n", i,tmp_id, package);
											send_message(clientfd, server_addr, message, UPLOADSTRING);
											if(write_status_to_list(tmp_id, CLIENTLISTFILE, real_file_name, i * 100 / package_amount, package_amount, i, (int)UPLOAD) == 1)
											{
												fprintf(stderr, "Could not update list \n");
											}
											sleep(1);
										}
										memset(message, 0, CHUNKSIZE * sizeof(char));
										save_massage_type_to_message(UPLOADROSPONSE, message);
										put_id_to_message(message,tmp_id);
										put_size_to_message((uint32_t)i, message);
										strcpy(message + 3*sizeof(uint32_t)/sizeof(char), (char*)md5_sum);
										fprintf(stderr, "Sending md5 sum for task id %d \n", tmp_id);
										send_message(clientfd, server_addr, message, UPLOADSTRING);
										if(write_status_to_list(tmp_id, CLIENTLISTFILE, real_file_name,  100 , package_amount, package_amount, (int)UPLOAD) == 1)
											{
												fprintf(stderr, "Could not update list \n");
											}
										sleep(1);
										break;
									}
								}
							}
						}
					}
				}
			}
		}
	}
	free(package);
	free(error_file_path);
	free(message);
	free(file_contents);
	free(real_file_name);
	fprintf(stderr, "Destroying UPLOAD thread\n");
	pthread_exit(&targ);
	return NULL;
}

/*
 * thread function for deleting file
 */
void *delete_thread_function(void *arg)
{
	int clientfd;
	int id = -1;
	int tmpid = -1;
	int i;
	struct sockaddr_in server_addr;
	thread_arg targ;
    char* filepath = NULL;
	char oldFilePath[FILENAME];
	char communicate [CHUNKSIZE - 3* sizeof(uint32_t)/ sizeof(char) - FILENAME];
	char* message = NULL;
	char* error_file_path = NULL;
	error_file_path = malloc(FILENAME);
	task_type task;
	memcpy(&targ, arg, sizeof(targ));
	message = calloc(CHUNKSIZE, sizeof(char));
	if(message != NULL && error_file_path != NULL)
	{
		int wait_for_delete_raport = 1;
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
		 * receive communicate from server about confirmation of delete or not
		 */
		wait_for_delete_response(&task, error_file_path, oldFilePath, message, &id, filepath, communicate, &wait_for_delete_raport);

		fprintf(stderr, "Waiting for datagrams about deleting file\n");

		while(work && wait_for_delete_raport)
		{
			if(check_top_of_queue("DELETE", &task, message, DELETE, error_file_path) == 1)
				continue;
			else
			{
				tmpid = get_id_from_message(message);
				if(tmpid == id)
				{
					/*one of datagrams from servers*/
					fprintf(stderr, "Id of message and thread are the same \n");
					strcpy(communicate, message + 3 * sizeof(uint32_t)/ sizeof(char) + FILENAME);
					fprintf(stdout, "Server sends report about deleting file  %s, message : %s \n", filepath, communicate);
					break;
				}
				else
				{
					push(queue, message);
				}
			}
		}
	}

	free(message);
	free(error_file_path);
	fprintf(stderr, "Destroying DELETE thread\n");
	pthread_exit(&targ);
	return NULL;
}

/*
 * thread function for listing files
 */
void *list_thread_function(void *arg)
{
	return common_download_function(arg, "LIST", LISTSTRING, LIST);
}

/*
 * create thread for defined purpose
 */
void init(pthread_t *thread, thread_arg *targ, int *socket, struct sockaddr_in* server_addr, task_type task,
		char filepath [FILENAME], int package_amount, int package_number)
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
			if (pthread_detach(thread[i]) !=  0)
				ERR("detach");
		}
	}
	else
	{
		targ[0].socket = socket;
		targ[0].server_addr = server_addr;
		targ[0].filename = filepath;
		targ[0].task = (int)task;
		targ[0].package_amount = package_amount;
		targ[0].package_number = package_number;
		switch(task)
		{
		case DOWNLOAD:
			if (pthread_create(&thread[0], NULL, download_thread_function, (void *) &targ[0]) != 0)
				ERR("pthread_create");
			break;
		case UPLOAD:
			if (pthread_create(&thread[0], NULL, upload_thread_function, (void *) &targ[0]) != 0)
				ERR("pthread_create");
			break;
		case DELETE:
			if (pthread_create(&thread[0], NULL, delete_thread_function, (void *) &targ[0]) != 0)
				ERR("pthread_create");
			break;
		case LIST:
			if (pthread_create(&thread[0], NULL, list_thread_function, (void *) &targ[0]) != 0)
				ERR("pthread_create");
			break;
		default:
			fprintf(stderr, "Could not create thread \n");
		}
		if (pthread_detach(thread[0]) !=  0)
				ERR("detach");
	}
}


/*
 * create socket, connection
 * if broadcast_enable == 1 than should broadcast
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
	 * SO_BROADCAST - socket can receive and send for broadcast
	 * broadcast_enable - set option for true
	 */
	if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable)))
		ERR("setsockopt");
	return sock;
}

/*
 * create ip structure from ip written in char array and port
 * if address null than address should be any
 * broadcast means if we should broadcast
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
			 * receiving from anybody
			 */
			addr.sin_addr.s_addr = htonl(INADDR_ANY);
		}
		else
		{
			/*
			 * sending data to whole subnet
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
	 * my_endpoint_listening_addr - address for listening from everybody
	 * broadcast_adrr - address for sending to everybody
	 * server_addr - server address
	 */
	struct sockaddr_in server_addr, broadcast_adrr, my_endpoint_listening_addr;
	/*
	 * socket for sending and receiving data from specified address
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
	work = 1;
	if (argc!=2)
		usage(argv[0]);
	queue = createQueue(QUEUECAPACITY);
	inicialize_file_mutex();
	if(create_list_file(CLIENTLISTFILE) == 1)
		fprintf(stderr, "Problem with creating list file \n");
		else
		{
			if(read_all_files_to_list(CLIENTLISTFILE) == 1)
			{
				fprintf(stderr, "Problem with writing record to list \n");
			}
		}
	fprintf(stdout,"%s", INSTRUCTION);

	sethandler(SIG_IGN, SIGPIPE);
	sethandler(siginthandler, SIGINT);

	generate_register_message(message);

	my_endpoint_listening_addr = make_address(NULL, rand_range(1000, 65000), 0);
	socket = connect_socket(0, my_endpoint_listening_addr);
	broadcast_adrr = make_address(NULL, atoi(argv[1]), 1);
	broadcastsocket = connect_socket(1, my_endpoint_listening_addr);

	while(task != REGISTERRESPONSE)
	{
		if(send_message(broadcastsocket, broadcast_adrr, message, REGISTERSTRING) < 0)
		{
			ERR("SEND");
		}
		sleep(1);
		if(receive_message(broadcastsocket, &server_addr, message) == 0)
		{
			task = check_message_type(message);
		}
	}

	if(TEMP_FAILURE_RETRY(close(broadcastsocket)) < 0)
		ERR("CLOSE");

	/*
	 * Initialize 2 main threads for listening from input and server
	 */
	init(thread, targ, &socket, &server_addr, NONE, NULL, 0 , 0);

	/* inicialize threads from list*/
	
	
	do_work(socket);
	free_queue();
	free_file_mutex();
	return EXIT_SUCCESS;
}

