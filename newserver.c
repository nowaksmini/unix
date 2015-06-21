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


/*
 * message send to client to inform about ability to delete file
 * generating new task id an removing file if can
 */
void generate_delete_response_message(char* message, int socket, struct sockaddr_in client_addr)
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
	    put_id_to_message(message, message_id);
	    strcpy(message + 3*sizeof(uint32_t)/sizeof(char) + FILENAME, DELETEERESPONSEERROR);
	    fprintf(stderr, "Server pushed filename %s and id %u to unsuccessful delete register response message \n", filepath, message_id);
	    send_message(socket, client_addr, message, DELETERESPONSESTRING);
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
	int fd; /*file desriptor */
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

/* TO DO */

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
	int i, j;
	struct stat sts;
	char filepath[FILENAME];
	char message[CHUNKSIZE];
	char * real_file_name;
	unsigned char md5_sum[MD5LENGTH];
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
	get_filename_from_message(messagein, filepath);
	save_massage_type_to_message(DOWNLOADRESPONSE, message);

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
	if (stat(real_file_name, &sts) == -1 && errno == ENOENT)
	{
	  /* no such file, id is 0 */
	  save_massage_type_to_message(REGISTERRESPONSE, message);
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
		  put_size_to_message((uint32_t)size, message);
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
	send_message(socket, client_addr, message, DOWNLOADRESPONSESTRING);

	sleep(1);
	/*
	 * clear message
	 */
	memset(message, 0, CHUNKSIZE);
	/*
	 * divide whole file to smaller one packages of size CHUNKSIZE - TYPE_LENGTH - TASK_ID_LENGTH - PACKAGE_NUMBER
	 */
	save_massage_type_to_message(DOWNLOAD, message);
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
	      save_massage_type_to_message(DOWNLOAD, message);
	      fprintf(stderr, "BEFORE \n");
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
	      send_message(socket, client_addr, message, DOWNLOADSTRING);
	      sleep(1);
	  }
	  memset(message, 0, CHUNKSIZE);
	  save_massage_type_to_message(DOWNLOAD, message);
	  put_size_to_message((uint32_t)i, message);
	  put_id_to_message(message,tmp_id);
	  fprintf(stderr, "Sending md5 sum for task id %d \n", tmp_id);
	  strcpy(message + 3*sizeof(uint32_t)/sizeof(char), (char*)md5_sum);
	  send_message(socket, client_addr, message, DOWNLOADSTRING);
	  sleep(1);
	}
	free (file_contents);
	free (package);
	free(real_file_name);
}


void *server_send_download_response_function(void *arg)
{
	return server_send_response_function(arg, "DOWNLOAD", DOWNLOAD, readfile);
}


void *server_send_delete_response_function(void *arg)
{
	return server_send_response_function(arg, "DELETE", DELETE, generate_delete_response_message);
}

/* TO DO */
void *server_send_upload_response_function(void *arg)
{
	int clientfd;
	struct sockaddr_in client_addr;
	thread_arg targ;
	char* message;
	memcpy(&targ, arg, sizeof(targ));
	task_type task = NONE;
	int my_id;
	char* error_file_path;
	error_file_path = (char *)calloc(FILENAME, sizeof(char));
	message = calloc(CHUNKSIZE, sizeof(char));
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
		if(task != UPLOAD)
		{
		  /* push to the end of queue */
		  fprintf(stderr, "Task was not UPLOAD \n");
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
	fprintf(stderr, "Destroing UPLOAD thread\n");
	
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
	fprintf(stderr, "My addres %u \n", htons(port));
	/*
	* receving from anybody
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
 * message send to client to inform about server address
 */
void generate_register_response_message(char* message)
{
	memset(message, 0, CHUNKSIZE);
	save_massage_type_to_message(REGISTERRESPONSE, message);
	strcpy(message + 3*sizeof(uint32_t)/sizeof(char), REGISTERRESPONSESUCCESS);
}

/* TO DO */

void *server_send_register_response_function(void *arg)
{
	int clientfd;
	struct sockaddr_in client_addr;
	thread_arg targ;
	char* message;
	memcpy(&targ, arg, sizeof(targ));
	task_type task = NONE;
	char* error_file_path;
	error_file_path = (char *)calloc(FILENAME, sizeof(char));
	message = calloc(CHUNKSIZE, sizeof(char));
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
		if(task != REGISTER)
		{
		  /* push to the end of queue */
		  fprintf(stderr, "Task was not REGISTER \n");
		  push(queue, message);
		  sleep(1);
		  continue;
		}
		else
		{
		  clientfd = *targ.socket;
		  client_addr = *targ.server_addr;
		  generate_register_response_message(message);
		  if(send_message(clientfd, client_addr, message, REGISTERRESPONSESTRING) < 0)
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
