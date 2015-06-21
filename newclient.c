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
	  char filepath [FILENAME]);


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
void download_request_work(int socket, struct sockaddr_in server_addr, char* filepath)
{
	char message_to_send[CHUNKSIZE];
	memset(message_to_send, 0, CHUNKSIZE);
	save_massage_type_to_message(DOWNLOAD, message_to_send);
	strcpy(message_to_send + 3*sizeof(uint32_t)/sizeof(char), filepath);
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
	struct stat sts;
	memset(message_to_send, 0, CHUNKSIZE);
	save_massage_type_to_message(UPLOAD, message_to_send);
	strcpy(message_to_send + 3*sizeof(uint32_t)/sizeof(char), filepath);
	fprintf(stderr, "Size of path put inside message %zu \n", strlen(filepath));
	if (stat(filepath, &sts) != -1 && errno == ENOENT)
	{
	  fprintf(stderr, "Could not open file %s \n", filepath);
	  return;
	}
	else
	{	
	  strcpy(message_to_send + 3*sizeof(uint32_t)/sizeof(char), filepath);
	  put_size_to_message((uint32_t)sts.st_size, message_to_send);
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
	memset(message_to_send, 0, CHUNKSIZE);
	save_massage_type_to_message(DELETE, message_to_send);
	strcpy(message_to_send + 3*sizeof(uint32_t)/sizeof(char), filepath);
	fprintf(stderr, "Size of path put inside message %zu \n", strlen(filepath));
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
	  fprintf(stderr, "Got task type %s \n", request);
	  fprintf(stderr, "Got file name to work %s  \n", filepath);
	  /*
	   * run new thread for communication and downloading
	   */
	  init(&thread, &targ, &socket, &server_addr, task, filepath);
	}    
}

/*
 * thread function for pushing messages from server to queue
 */
void server_listening_work(int socket, struct sockaddr_in server_addr)
{
	task_type task = NONE;
	char message_received[CHUNKSIZE];
	char* information;
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
	fprintf(stderr, "%s: %s", RECEIVEDINTHREAD, information);
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
 */
uint8_t wait_for_download_response(char* error_file_path, char* message, task_type *type, int* id, char* file_path,
				char* old_file_path, char* communicate, char** real_file_name, int* package_amount, uint8_t** packages)
{
	int i;
	int first_empty_sign;
	int filesize = 0;
	int tmp_id = -1;
	int real_package_size = CHUNKSIZE - 3 * sizeof(uint32_t)/sizeof(char);
	while(work)
	{
	      if(check_top_of_queue("DOWNLOADRESPONSE", type, message, DOWNLOADRESPONSE, error_file_path) == 1)
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
		    fprintf(stdout, "Server confirmed downloading file  %s, \n  message : %s \n", file_path, communicate);
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
		    fprintf(stderr, "Real file name size %zu after malloc \n", strlen(*real_file_name));
		    return create_file(*real_file_name, &filesize, real_package_size , package_amount, packages, message);
		  }
		}
	      }
	}
	return 0;
}

/*
 * handle packages with id same as thread id an task type DOWNLOAD
 */
void* wait_for_packages(char *message, char* error_file_path, int id, int package_amount, int should_download, char* real_file_name, uint8_t* packages)
{
	task_type task;
	int i;
	int tmp_id;
	int package_number;
	char* file_contents;
	unsigned char md5_sum[MD5LENGTH];
	char* package;
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
	  fprintf(stderr, "Waiting to receive DOWNLOAD \n");
	  if (top(queue, message) < 0)
	  {
	    fprintf(stderr, "Queue is empty, nothing to show \n");
	    sleep(2);
	    message = (char*)calloc(CHUNKSIZE, sizeof(char));
	    if(message == NULL)
	    {
	      fprintf(stderr, "Problems with allocating memory for message \n");
	      break;
	    }
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
	  if(task != DOWNLOAD)
	  {
	    /* push to the end of queue */
	    fprintf(stderr, "Task type is not DOWNLOAD\n");
	    push(queue, message);
	    sleep(1);
	    continue;
	  }
	  else
	  {
	    tmp_id = get_id_from_message(message);
	    if(tmp_id == id)
	    {
	      /*file exists or some problems occured earlier
	       need to take from queue all messages for this task*/
	      
	      fprintf(stderr, "Id of message and thread are the same \n");
	      fprintf(stderr, "Got data for update \n");
	      package_number = get_file_size_from_message(message);
	      if(package_number == package_amount)
	      {
		if(should_download == 1) 
		  break;
		fprintf(stderr, "Checking md5 sums \n");
		file_contents = read_whole_file (real_file_name);
		if(file_contents == NULL) 
		{
		  break;
		}
		compute_md5(file_contents, md5_sum);
		package = malloc(real_package_size);
		if(package == NULL)
		{
		    fprintf(stderr, "Problem with allocating memory for package \n");
		    break;
		}
		strcpy(package, message + 3 * sizeof(uint32_t)/sizeof(char));
		fprintf(stderr, "Got md5 sum %s for file %s \n", md5_sum, real_file_name);
		for(i = 0; i< MD5LENGTH; i++)
		{
		  if(md5_sum[i] == '\0') break;
		  if(((int)md5_sum[i] - (int)package[i] ) % 256 != 0)
		  {
		    fprintf(stdout, "Wrong md5 sum %s for field %d %d %d \n", real_file_name, i, (int)md5_sum[i], (int)package[i]);
		    break;
		  }
		}
		fprintf(stdout, "Md5 sums correct for file %s \n", real_file_name);
		break;
	      }
	      else
	      {
		if(should_download == 1) 
		  continue;
		fprintf(stderr, "Write data to update file \n");
		package = malloc(real_package_size);
		if(package == NULL)
		{
		    fprintf(stderr, "Problem with allocating memory for package \n");
		    break;
		}
		strcpy(package, message + 3 * sizeof(uint32_t)/sizeof(char));
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
		  bulk_write(fd, package, strlen(package));
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
	    else
	    {
	      fprintf(stderr, "Pushing message back to queue \n");
	      push(queue, message);
	    }
	  }
	}
	free(package);
	return NULL;
}

/*
 * thread function for downloading file
 */
void *download_thread_function(void *arg)
{
	int clientfd;
	int id = -1;
	int package_amount = 0;
	struct sockaddr_in server_addr;
	thread_arg targ;
	char* filepath;
	int i;
	char* error_file_path;
	char oldFilePath[FILENAME];
	char communicate [CHUNKSIZE - 3* sizeof(uint32_t)/ sizeof(char) - FILENAME];
	char* real_file_name;
	char* message;
	task_type task;
	uint8_t * packages ;
	uint8_t should_download;
	message = calloc(CHUNKSIZE, sizeof(char));
	memcpy(&targ, arg, sizeof(targ));
	error_file_path = (char*) calloc(FILENAME, sizeof(char));
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
	should_download = wait_for_download_response(error_file_path, message, &task, &id, filepath,
				oldFilePath, communicate, &real_file_name, &package_amount, &packages);

	fprintf(stderr, "Waiting for datagrams with file content\n");
	
	wait_for_packages(message, error_file_path, id, package_amount, should_download, real_file_name, packages);
	
	free(message);
	free(error_file_path);
	free(real_file_name);
	fprintf(stderr, "Destroing DOWNLOAD thread\n");
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
	char* error_file_path;
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
	error_file_path = malloc(FILENAME);
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
	fprintf(stderr, "Waiting for datagrams with file content\n");

	/*
	 * receive communicate from server about response for update
	 */
	while(work)
	{
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
		fprintf(stderr, "Got task with type ERROR for filename %s \n", error_file_path );
		continue;
	      }
	      if(task != UPLOADROSPONSE)
	      {
		/* push to the end of queue */
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
		    for(i = 0; i < first_empty_sign; i++)
		    {
			real_file_name[i] = filepath[i];
		    }
		    fprintf(stderr, "Real file name size %zu after malloc \n", strlen(real_file_name));		    
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
				  put_size_to_message((uint32_t)i, message);
				  strcpy(message + 3*sizeof(uint32_t)/sizeof(char), package);
				  fprintf(stderr, "Sending data package number %d for task id %d : %s \n", i,tmp_id, package);
				  send_message(clientfd, server_addr, message, UPLOADSTRING);
				  sleep(1);			    
		     }
	             put_size_to_message((uint32_t)i, message);
		     fprintf(stderr, "Sending md5 sum for task id %d \n", tmp_id);
		     strcpy(message + 3*sizeof(uint32_t)/sizeof(char), (char*)md5_sum);
	             send_message(clientfd, server_addr, message, UPLOADSTRING);
		      sleep(1);      
		    break;
		  }
		}
	      }
	}
	
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
	char* error_file_path;
	error_file_path = malloc(FILENAME);
	task_type task;
	memcpy(&targ, arg, sizeof(targ));
	message = calloc(CHUNKSIZE, sizeof(char));
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
	while(work)
	{
	      if(check_top_of_queue("DELETERESPONSE", &task, message, DELETERESPONSE, error_file_path) == 1)
		continue;
	      /* waiting for confirmation or rejection of downloading file*/
	      if(id == -1)
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
		    wait_for_delete_raport = 0;
		    break;
		  }
		  else
		  {
		    id = tmpid;
		    fprintf(stdout, "Server confirmed planned deleting file  %s, message : %s \n", filepath, communicate);
		    fprintf(stdout, "Got id %d \n", id);
		    /* check if file exists if not create else check if is opened if not remove data and write own */
		    break;
		  }
		}
	      }
	}
	
	fprintf(stderr, "Waiting for datagrams abour deleting file\n");

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
		      fprintf(stdout, "Server sends raport about deleting file  %s, message : %s \n", filepath, communicate);
		      break;
		  }
		  else
		  {
		    push(queue, message);
		  }
	      }
	  }
	free(message);
	free(error_file_path);
	fprintf(stderr, "Destroing DELETE thread\n");
	pthread_exit(&targ);
	return NULL;
}

/*
 * create thread for defined purpose
 */
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
	work = 1;
	if (argc!=2)
		usage(argv[0]);
	queue = createQueue(QUEUECAPACITY);

	fprintf(stdout,"%s", INSTRUCTION);
	
	sethandler(SIG_IGN, SIGPIPE);
	sethandler(siginthandler, SIGINT);

	generate_register_message(message);

	my_endpoint_listening_addr = make_address(NULL, atoi(argv[1])+1 , 0);
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
	 * inicialize 2 main threads for listening from input and server
	 */
	init(thread, targ, &socket, &server_addr, NONE, NULL);
	
	do_work(socket);
	free_queue();
	return EXIT_SUCCESS;
}

