#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>

#define PORT 5555
#define MAX_CLIENT_NUM 32

int client_sock_set[MAX_CLIENT_NUM];
uint32_t client_conn[MAX_CLIENT_NUM];
uint32_t total_conn = 0;

unsigned int cur_client = 0;

int main(int argc, char ** argv) 
{ 
    int sockfd,new_fd;
    struct sockaddr_in my_addr;  
    struct sockaddr_in their_addr;  
    unsigned int sin_size, myport, lisnum; 
    char default_conf_file_name[] = "./sync_server.conf";
    unsigned int client_num = 0;
    uint32_t running_time;

    if (argc < 3) {
  	fprintf(stderr, "usage: %s client_num running_time\n", argv[0]);
	exit(EXIT_FAILURE);
    }

    client_num = atoi(argv[1]);
    running_time = atoi(argv[2]);

    myport = PORT; 

    lisnum = client_num; 

    if ((sockfd = socket(PF_INET, SOCK_STREAM, 0)) == -1) { 
        perror("socket"); 
        exit(1); 
    } 

    int reuse = 1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
		  &reuse, sizeof(reuse)) < 0) 
    {
	perror("setsockopt");
	exit(1);
    }
    
    my_addr.sin_family=PF_INET; 
    my_addr.sin_port=htons(myport); 
    my_addr.sin_addr.s_addr = INADDR_ANY; 
    bzero(&(my_addr.sin_zero), 0); 
    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1) { 
        perror("bind"); 
        exit(1); 
    } 

    if (listen(sockfd, lisnum) == -1) { 
        perror("listen"); 
        exit(1); 
    } 
    while(1) { 
        sin_size = sizeof(struct sockaddr_in); 
        if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size)) == -1) { 
            perror("accept"); 
            continue; 
        } 
        printf("server: got connection from %s\n",inet_ntoa(their_addr.sin_addr)); 
	client_sock_set[cur_client++] = new_fd;
	if (cur_client == client_num) {
	    for (unsigned int i = 0; i < client_num; i++) {
		uint32_t reg = htonl(running_time);	
		send(client_sock_set[i], &reg, sizeof(reg), 0);
	    }
	    for (unsigned int i = 0; i < client_num; i++) {
		recv(client_sock_set[i], &client_conn[i], sizeof(uint32_t), 0);
	    }

	    total_conn = 0;
	    for (unsigned int i = 0; i < client_num; i++) {
		total_conn += ntohl(client_conn[i]);
	    }

	    for (unsigned int i = 0; i < client_num; i++) {
		close(client_sock_set[i]);
	    }
	    break;
	}
    } 
    close(sockfd);
    printf("total connections: %d\tin %d(s)\n", total_conn, running_time);
    printf("throughput: %.2f(conn/sec)\n", (float)total_conn/running_time);
} 

