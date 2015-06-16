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

volatile sig_atomic_t work = 1;
volatile sig_atomic_t id = 0;
Queue* queue;


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
	fprintf(stderr, "USAGE: %s port workdir \n",name);
	exit(EXIT_FAILURE);
}


/*
 * sending message
 */
int send_message (int socket, struct sockaddr_in client_addr, char* message, char* message_type, task_type task)
{
  char tmp[CHUNKSIZE];
  int port = client_addr.sin_port;
  fprintf(stderr, "Client port %d \n", port);
  fprintf(stderr, "Trying to send message %s \n", message_type);
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
  
  strcpy(tmp, message + 3*sizeof(uint32_t)/sizeof(char));

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
	else if(task == DELETE)
	{
	    message_type = DELETESTRING;
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
    }
}

/*
 * message send to client to inform about ability to delete file
 * generating new task id
 */
void generate_delete_response_message(char* message, int socket, struct sockaddr_in client_addr)
{
	int i;
	struct stat sts;
	char filepath[FILENAME];
	char * real_file_name;
	int type = (int)DELETERESPONSE;
	int first_empty_sign = FILENAME - 1;
	int message_id = 0;
	memset(filepath, 0, FILENAME);
	convert_message_to_file_path(message, filepath);
	convert_int_to_char_array(type, message);

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
	
	fprintf(stderr, "Real file name : %s \n", real_file_name);
	fprintf(stderr, "Real file name size %zu \n", strlen(real_file_name));
	convert_int_to_char_array(DELETERESPONSE, message);
	strcpy(message + 3*sizeof(uint32_t)/sizeof(char), filepath);
	if (stat(real_file_name, &sts) == -1 && errno == ENOENT)
	{
	  /* no such file, id is 0 */
	  put_id_to_message(message, message_id);
	  strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, DELETEERESPONSEERROR);
	  fprintf(stderr, "Server pushed filename %s and id %u to unsuccessfull delete register response message \n", filepath, message_id);
	  send_message(socket, client_addr, message, DELETERESPONSESTRING, DELETERESPONSE);
	}
	else
	{
	   id = id + 1;
	   message_id = id;
	   put_id_to_message(message, message_id);
	   strcpy(message + 3*sizeof(uint32_t)/sizeof(char), filepath);
	   strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, DELETERESPONSESUCCESS);
	   send_message(socket, client_addr, message, DELETERESPONSESTRING, DELETERESPONSE);

	   if (remove(real_file_name) < 0)
	   {
		  fprintf(stderr, "Removed file failed %s \n", real_file_name);
		  put_id_to_message(message, message_id);
		  convert_int_to_char_array(DELETE, message);
		  strcpy(message + 3*sizeof(uint32_t)/sizeof(char), filepath);
		  strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, DELETEACCESSDENYIED);
	   }  
	   else
	   {
		  fprintf(stderr, "Removed file %s success\n", real_file_name);
		  put_id_to_message(message, message_id);
		  convert_int_to_char_array(DELETE, message);
		  strcpy(message + 3*sizeof(uint32_t)/sizeof(char), filepath);
		  strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, DELETERESPONSESUCCESS);
	   }
	   
	}
        send_message(socket, client_addr, message, DELETESTRING, DELETE);

	sleep(1);
	free(real_file_name);
}

/*
 * message send to client to inform about receiving new file size, md5
 * generating new task id
 */
void generate_upload_response_message(int* my_id, char* message, int socket, struct sockaddr_in client_addr)
{
	int fd; /*file desriptor */
	int i;
	struct stat sts;
	char filepath[FILENAME];
	char * real_file_name;
	unsigned char md5_sum[MD5LENGTH];
	int type = (int)UPLOADROSPONSE;
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
	convert_message_to_file_path(message, filepath);
	convert_int_to_char_array(type, message);
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
	fprintf(stderr, "Real file name size %zu after malloc \n", strlen(real_file_name));
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
	      convert_int_to_char_array(UPLOADROSPONSE, message);
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
	      send_message(socket, client_addr, message, UPLOADRESPONSESTRING, UPLOADROSPONSE);
	      close(fd);

	    }
	    else
	    {
	      convert_int_to_char_array(UPLOADROSPONSE, message);
	      put_id_to_message(message,message_id);
	      strcpy(message + 3*sizeof(uint32_t)/sizeof(char), filepath);
	      strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, UPLOADRESPONSEERROR);
	      fprintf(stderr, "Server pushed filename %s and id %u to unsuccessfull upload register response message \n", filepath, id);
	      send_message(socket, client_addr, message, UPLOADRESPONSESTRING, UPLOADROSPONSE);
	      return;
	    }
	}
	else
	{
	    type = UPLOAD;
	    if(tmp_id != *my_id)
	    {
		  while(queue->busy)
		  {
		    sleep(1);
		  }
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
	int message_id = 0;
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
	
	fprintf(stderr, "Real file name : %s \n", real_file_name);
	fprintf(stderr, "Real file name size %zu \n", strlen(real_file_name));
	if (stat(real_file_name, &sts) == -1 && errno == ENOENT)
	{
	  /* no such file, id is 0 */
	  convert_int_to_char_array(REGISTERRESPONSE, message);
	  put_id_to_message(message, message_id);
	  strcpy(message + 3*sizeof(uint32_t)/sizeof(char), filepath);
	  strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, DOWNLOADRESPONSEERROR);
	  fprintf(stderr, "Server pushed filename %s and id %u to unsuccessfull download register response message \n", filepath, message_id);
	  return;
	}
	else
	{
	   size = sts.st_size;
	   id = id + 1;
	   tmp_id = id;
	   if ((fd = TEMP_FAILURE_RETRY(open(real_file_name, O_RDONLY))) == -1)
	   {
		  fprintf(stderr, "Could not open file %s \n", real_file_name);
		  put_id_to_message(message, message_id);
		  strcpy(message + 3*sizeof(uint32_t)/sizeof(char), filepath);
		  strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, DOWNLOADRESPONSEERROR);
	   }
	   else
	   {
		  put_id_to_message(message, id);
		  put_value_int_to_message(size, message);
		  file_contents = read_whole_file (real_file_name);
		  if(file_contents == NULL) 
		  {
		    free (file_contents);
		    free (package);
		    free(real_file_name);
		  }
		  compute_md5(file_contents, md5_sum);
		  strcpy(message + 3*sizeof(uint32_t)/sizeof(char), filepath);
		  strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, DOWNLOADRESPONSESUCCESS);
	   }
	   if (TEMP_FAILURE_RETRY(close(fd)) == -1)
	   {
	     fprintf(stderr, "Could not close file %s \n", real_file_name);
	   }
	}
	send_message(socket, client_addr, message, DOWNLOADRESPONSESTRING, DOWNLOADRESPONSE);
	

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
	      put_id_to_message(message,tmp_id);
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
	free (package);
	free(real_file_name);
}


void *server_send_download_response_function(void *arg)
{
	int clientfd;
	struct sockaddr_in client_addr;
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
		if(task != DOWNLOAD)
		{
		  /* push to the end of queue */
		  fprintf(stderr, "Task was not DOWNLOAD \n");
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
		  client_addr = *targ.server_addr;
		  readfile(message, clientfd, client_addr);
		  sleep(1);
		  break;
		}
	}
	fprintf(stderr, "Destroing download thread\n");
	pthread_exit(&targ);
	return NULL;
}


void *server_send_delete_response_function(void *arg)
{
	int clientfd;
	struct sockaddr_in client_addr;
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
		if(task != DELETE)
		{
		  /* push to the end of queue */
		  fprintf(stderr, "Task was not DELETE \n");
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
		  client_addr = *targ.server_addr;
		  generate_delete_response_message(message, clientfd, client_addr);
		  sleep(1);
		  break;
		}
	}
	fprintf(stderr, "Destroing delete thread\n");
	pthread_exit(&targ);
	return NULL;
}


void *server_send_upload_response_function(void *arg)
{
	int clientfd;
	struct sockaddr_in client_addr;
	thread_arg targ;
	char message[CHUNKSIZE];
	memcpy(&targ, arg, sizeof(targ));
	task_type task = NONE;
	int my_id;
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
		if(task != UPLOAD)
		{
		  /* push to the end of queue */
		  fprintf(stderr, "Task was not UPLOAD \n");
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
		  client_addr = *targ.server_addr;
		  generate_upload_response_message(&my_id, message, clientfd, client_addr);
		  sleep(1);
		  break;
		}
	}
	fprintf(stderr, "Destroing upload thread\n");
	pthread_exit(&targ);
	return NULL;
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
	strcpy(message + 3*sizeof(uint32_t)/sizeof(char), REGISTERRESPONSESUCCESS);
}

void *server_send_register_response_function(void *arg)
{
	int clientfd;
	struct sockaddr_in client_addr;
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
		  client_addr = *targ.server_addr;
		  generate_register_response_message(message);
		  if(send_message(clientfd, client_addr, message, REGISTERRESPONSESTRING, REGISTERRESPONSE) < 0)
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



void init(pthread_t *thread, thread_arg *targ, int *socket, struct sockaddr_in* client_addr, task_type task)
{
	targ[0].socket = socket;
	targ[0].server_addr = client_addr;
	if(task == REGISTER)
	{
	  if (pthread_create(&thread[0], NULL, server_send_register_response_function, (void *) &targ[0]) != 0)
		ERR("pthread_create");
	}
	if(task == DOWNLOAD)
	{
	   if (pthread_create(&thread[0], NULL, server_send_download_response_function, (void *) &targ[0]) != 0)
		ERR("pthread_create");
	}
	if(task == DELETE)
	{
	   if (pthread_create(&thread[0], NULL, server_send_delete_response_function, (void *) &targ[0]) != 0)
		ERR("pthread_create");
	}
	if(task == UPLOAD)
	{
	   if (pthread_create(&thread[0], NULL, server_send_upload_response_function, (void *) &targ[0]) != 0)
		ERR("pthread_create");
	}
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
		  if(task == REGISTER || task == DOWNLOAD || task == DELETE || task == UPLOAD)
		  {
		      while(queue->busy)
		      {
			sleep(1);
		      }
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
	 * my_endpoint_listening_addr - adress for listening from everybody
	 */
	struct sockaddr_in  my_endpoint_listening_addr;

	/*
	 * socket for sending and reciving data from specified address
	 */
	int socket;
		
	if (argc!=3)
		usage(argv[0]);
	if (chdir(argv[2]) == -1)
		ERR("chdir");
	sethandler(SIG_IGN, SIGPIPE);
	sethandler(siginthandler, SIGINT);
	
	my_endpoint_listening_addr = make_address(atoi(argv[1]));
	
	socket = connect_socket(my_endpoint_listening_addr);
	
	queue = createQueue(100);
	fprintf(stderr, "Created queue \n");

	do_work(socket);
	
	return EXIT_SUCCESS;
}