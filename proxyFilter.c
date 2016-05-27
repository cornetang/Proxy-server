#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define MAXBUFLEN 100
#define MAXDATASIZE 100
#define BACKLOG 10	 // how many pending connections queue will hold
#define NUM_THREADS 4

int listen_sock; //listen on listen_sock
int proxyport=0;

//for blacklist
int blacklist; // blacklist=1 if it exist
int num_blacklist=0;
char list[100][100];
char temp[1]="0";

/**
 * 
 * Simple Stream Server is adapted from Brian "Beej Jorgensen" Hall
 * http://beej.us/guide/bgnet/output/html/multipage/clientserver.html#simpleserver
 */

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/* thread data struct */
typedef struct _thread_data_t {
  int tid;
} thread_data_t;

/* thread function */
void *thr_func(void *arg) {
	thread_data_t *data = (thread_data_t *)arg;

	//sockets
	int client_sock, host_sock;  // receive command from client_sock, fetch HTTP from host_sock
	socklen_t sin_size;
	struct sockaddr_storage their_addr; // connector's address information
	char s_client[INET6_ADDRSTRLEN];
  	int numbytes_rev_client, numbytes_send_client;
  	char buf_client[MAXBUFLEN];
  	char request_type[4], host[256], port[60], url[1024];
  	char* strptr;

  	//for connection to server
  	int numbytes_send_host, numbytes_recv_host;
	char buf_host[MAXDATASIZE];
	struct addrinfo hints_host, *servinfo_host, *p_host;
	int rv_host;
	char s_host[INET6_ADDRSTRLEN];

	//for caching
	char cachepath[255],cc[2];
	char buf_cache[100];

  	// main accept() loop
	printf("server: waiting for connections on thread %d...\n", data->tid);
	while(1) {  // main accept() loop
		sin_size = sizeof their_addr;
		client_sock = accept(listen_sock, (struct sockaddr *)&their_addr, &sin_size);
		if (client_sock == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s_client, sizeof s_client);
		//printf("server: got connection from %s\n", s_client);
        
        if ((numbytes_rev_client = recv(client_sock, buf_client, MAXDATASIZE-1, 0)) == -1) {
            perror("recv");
            exit(1);
        }
        buf_client[numbytes_rev_client] = '\0';
        
        // prining request from client
        // printf("server: received packet from %s\n", s_client);
        printf("server: rcvd packet on thread %d containing \n%s\n",data->tid,buf_client);
   
	   	// parse request type, i.e. get
	    strptr = strtok(buf_client, " ");
	    strcpy(request_type, strptr);
	    // return error if request type is not get
	    if (strcmp(request_type, "GET")!=0){
	    	if((numbytes_send_client = send(client_sock, "405 Method Not Allowed, only GET is allowed\n", strlen("405 Method Not Allowed, only GET is allowed\n"), 0)) == -1){
	    		perror("405 Method Not Allowed, only GET is allowed\n");
	    	}
	    	close(client_sock);
	    	continue;
	    }
	    // printf("request type: %s\n", request_type);

	    // parse url and host
	    strptr = strtok(NULL, " ");
	    strptr = strtok(strptr, "/");
	    strcpy(url, strptr);
	    strptr = strtok(NULL, "/");
	    strcpy(host, strptr);
	    strcat(url, "//");
	    strcat(url, strptr);
	    strptr = strtok(NULL, " :");
	    strcat(url, "/");
	    if (strptr!=NULL) strcat(url, strptr);

	    // parse port number, 80 by default
	    strptr = strtok(NULL, ":");
	    if (strptr==NULL) {
	        strcpy(port, "80");
	    }
	    else{
	        strcpy(port, strptr);
	    }
	    printf("server: port is %s on thread %d\n",port, data->tid);

	    // printing parse result
	    // printf("request type: %s\n", request_type);
	    // printf("host: %s\n", host); 
	    // printf("url: %s\n", url); 
	    // printf("port: %s\n", port);

	    // check if URI is filtered
	    if (blacklist==1){
		 	char *filter;
		 	int j, filterbreak=0;
		   	for (j=0;j<num_blacklist;j++){
		   		filter = strcasestr(url, list[j]);
		   		if (filter!=NULL){
		   			perror("403 Forbidden\n");
					numbytes_send_host  = send(client_sock, "403 Forbidden\n", strlen("403 Forbidden\n"), 0);
					filterbreak=1;
					break;
			   	}	
			}
			if (filterbreak==1){
				close(client_sock);
				close(host_sock);
			}
		}	

		// check validity of file
		cachepath[0]='\0';
		strcat(cachepath, "./cache/");
		int i;
		cc[1]='\0';
		for (i=0;i<255;i++){
			if (url[i]=='\0') break;
			if (url[i]!=47&&url[i]!=58&&url[i]!=126) {
				cc[0]=url[i];
				strcat(cachepath, cc);
			}
		}
		strcat(cachepath, ".txt");
		FILE *ptr;
		ptr = fopen(cachepath,"r"); 
		// printf("cachepath:%s\n", cachepath);

		if (ptr==NULL){ // not cached
			// printf("not cached\n");
			
			// temp file name
			char tempcache[15];
			tempcache[0]='\0';
			strcat(tempcache,"./cache/temp");
			strcat(tempcache,temp);
			strcat(tempcache,".txt");
			if (temp[0]==53){
				temp[0]='0';
			}
			else{
				temp[0]++;
			}
			// printf("\n\n tempcache%s \n", tempcache);

			// open temp file to write
			ptr = fopen(tempcache,"w"); 

		    // create request to host server
		    char command[1000];
		    strcpy(command, "GET ");
		    strcat(command, url);
		    strcat(command, " HTTP/1.1\r\nHost: ");
		    strcat(command, host);
		    strcat(command, "/\r\n\r\n\r\n");
		    //printf("%s\n", command);

		    // establish connection with host server
			memset(&hints_host, 0, sizeof hints_host);
			hints_host.ai_family = AF_UNSPEC;
			hints_host.ai_socktype = SOCK_STREAM;
			if ((rv_host = getaddrinfo(host, port, &hints_host, &servinfo_host)) != 0) {
				fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv_host));
				numbytes_send_host  = send(client_sock, "404 Not Found\n", strlen("404 Not Found\n"), 0);
				close(client_sock);
				close(host_sock);
				continue;
			}
			for(p_host = servinfo_host; p_host != NULL; p_host = p_host->ai_next) {
				if ((host_sock = socket(p_host->ai_family, p_host->ai_socktype,p_host->ai_protocol)) == -1) {
					perror("502 Bad Gateway\n");
					numbytes_send_host  = send(client_sock, "502 Bad Gateway\n", strlen("502 Bad Gateway\n"), 0);
					continue;
				}
				if (connect(host_sock, p_host->ai_addr, p_host->ai_addrlen) == -1) {
					perror("505 HTTP Version Not Supported");
					numbytes_send_host  = send(client_sock, "505 HTTP Version Not Supported\n", strlen("505 HTTP Version Not Supported\n"), 0);
					close(host_sock);
					continue;
				}
				break;
			}
			if (p_host == NULL) {
				perror("error: no host\n");
				//numbytes_send_host  = send(client_sock, "502 Bad Gateway\n", strlen("502 Bad Gateway\n"), 0);
				continue;
			}
			inet_ntop(p_host->ai_family, get_in_addr((struct sockaddr *)p_host->ai_addr), s_host, sizeof s_host);
			//printf("client: connecting to %s\n", s_host);
			freeaddrinfo(servinfo_host); // all done with this structure

		    // Send packets to the host server
		    if ((numbytes_send_host = send(host_sock, command, strlen(command), 0)) == -1)
		    {
		        perror("502 Bad Gateway, cannot send packet to host\n");
				numbytes_send_host  = send(client_sock, "502 Bad Gateway\n", strlen("502 Bad Gateway\n"), 0);
		        close(client_sock);
		        close(host_sock);
		        continue;
		    }
		    //printf("client: sent %d bytes\n", numbytes_send_host);
		    
		    // receive packets from host, print, cache, sent back to client
		    int firstpacket=1;
		    char *packetptr;
		    char packeterror[7];
		    packeterror[0]='0';
		   	do {
		   		numbytes_recv_host = recv(host_sock, buf_host, MAXDATASIZE-1, 0);
		   		// identify 404 error
		   		if (firstpacket==1){ 
		   			packetptr = strtok(buf_host, " ");
		   			packetptr = strtok(NULL, " ");
	    			strcpy(packeterror, packetptr);
	    			firstpacket++;
	    			if (strcmp(packetptr,"404") == 0){
	    				numbytes_send_host  = send(client_sock, "404 Not Found\n", strlen("404 Not Found\n"), 0);
	    				break;
	    			}
		   		}

		   		// identify 502 error
		   		if (numbytes_recv_host==-1){
		   			perror("502 Bad Gateway, cannot receive packet from host\n");
					numbytes_send_host  = send(client_sock, "502 Bad Gateway\n", strlen("502 Bad Gateway\n"), 0);
		   		}

		   		// identify EOF
		   		else if (numbytes_recv_host==0){ // EOF
		   			// printf("EOF, Connection closed\n");
		   		}

		   		// send result to client
		   		else{
		   			buf_host[numbytes_recv_host] = '\0';
		   			// print result in proxy server
		    		// printf("%s\n",buf_host); // PRINTS RESULT
		    		// send result back to client
		    		numbytes_send_host  = send(client_sock, buf_host, strlen(buf_host), 0);
		    		fprintf(ptr,"%s",buf_host);
		   		}

		   	} while (numbytes_recv_host>0);

		   	// rename temp file to URI
		   	if (numbytes_recv_host==0){
		   		int filerename;
		   		filerename = rename(tempcache, cachepath);	
		   	}
		}
		else{ // cached
			// printf("cached\n");
			
			while(fgets(buf_cache,100,ptr)!=NULL){
				numbytes_send_host =send(client_sock, buf_cache, strlen(buf_cache), 0);
			}
		}
			
	   	// close cache file
	   	fclose(ptr);

		// end connection with host server
		close(host_sock);
		// end connection with client
		close(client_sock);  
	}
}

int main(int argc, char **argv)
{
	//for coneection with client
	struct addrinfo hints_client, *servinfo_client, *p_client;
	struct sigaction sa;
	int yes=1;
	int rv_client;

    char ch, file_name[25];
    FILE *fp;

	// check number of arguments
	if (argc>3){
		printf("invalid number of arguments\n");
		return 1;
	}

	// fetch port number
	int i;
	for (i=0;i<strlen(argv[1]);i++){
		if (argv[1][i]<48||argv[1][i]>57){
			printf("Invalid port number!\n");
			return;
		}
	}
	for (i=0;i<strlen(argv[1]);i++){
		proxyport=proxyport*10+argv[1][i]-48;
	}
	if (proxyport>65535){
		printf("Invalid port number!\n");
		return;
	}

	// determine existence of blacklist
	if (argc==3){
		blacklist=1;
	}
	else{
		blacklist=0;
	}

	// fetch blacklist
	if (blacklist==1){
		// get file name from parameter
	    strcpy(file_name, argv[2]);

	    // open file
	    int num_letter=0;
	 	fp = fopen(file_name,"r"); // read mode
	 	if (fp==NULL){
	 		printf("Invalid file for blacklist\n");
	 		return;
	 	}

	 	// read file
	   	while( ( ch = fgetc(fp) ) != EOF ){
	   		// printf("ch=%c\n", ch);
	   		// printf("num_blacklist= %i\n", num_blacklist);
	   		// printf("num_letter%i\n", num_letter);
	   		if ( ch != '\n' ){
	   			list[num_blacklist][num_letter]=ch;
	   			num_letter++;
	   		}
	   		else{
	   			list[num_blacklist][num_letter]='\0';
	   			num_blacklist++;
	   			num_letter=0;
	   		}
	   	}
	   	num_blacklist++;

	   	//print blacklist
	   	// for (int j=0;j<num_blacklist;j++){
	   	// 	printf("%s\n", list[j]);
	   	// }

	   	// close reader
	   	fclose(fp);
	}

	// connection with client
	memset(&hints_client, 0, sizeof hints_client);
	hints_client.ai_family = AF_UNSPEC;
	hints_client.ai_socktype = SOCK_STREAM; // TCP
	hints_client.ai_flags = AI_PASSIVE; // use my IP
	if ((rv_client = getaddrinfo(NULL, argv[1], &hints_client, &servinfo_client)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv_client));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p_client = servinfo_client; p_client != NULL; p_client = p_client->ai_next) {
		if ((listen_sock = socket(p_client->ai_family, p_client->ai_socktype, p_client->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}
		if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}
		if (bind(listen_sock, p_client->ai_addr, p_client->ai_addrlen) == -1) {
			close(listen_sock);
			perror("server: bind");
			continue;
		}
		break;
	}
	freeaddrinfo(servinfo_client); // all done with this structure
	if (p_client == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}
	if (listen(listen_sock, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;


	// make threads
	pthread_t thr[NUM_THREADS];
  	int thr_i, rc;
  	thread_data_t thr_data[NUM_THREADS];
 
  	/* create threads */
  	for (thr_i = 0; thr_i < NUM_THREADS; ++thr_i) {
	  	thr_data[thr_i].tid = thr_i;
	    if ((rc = pthread_create(&thr[thr_i], NULL, thr_func, &thr_data[thr_i]))) {
	      fprintf(stderr, "error: pthread_create, rc: %d\n", rc);
	      exit(1);
	    }
 	}
	
	/* block until all threads complete */
	for (thr_i = 0; thr_i < NUM_THREADS; ++thr_i) {
		pthread_join(thr[thr_i], NULL);
	}

	return 0;
}