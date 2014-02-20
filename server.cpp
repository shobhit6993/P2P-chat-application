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
#include <sys/time.h>
#include <map>
#include <pthread.h>
#include <mutex>
#include <iostream>
#include <csignal>

using namespace std;

#define PORT "8888"  // the port users will be connecting to

#define BACKLOG 10	 // how many pending connections queue will hold

#define MAXTHREADS 100

#define TTL 30

#define MAXDATASIZE 100

struct info
{
	int sock_fd;
	string ip;
};

std::map<string, info> clientMap;
std::map<string, int> threadMap;
pthread_t threads[MAXTHREADS];
mutex mtx;

//for recv timeout
struct timeval tv;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void sigchld_handler(int s)
{
	while(waitpid(-1, NULL, WNOHANG) > 0);
}


void *threadForClient(void* s)
{
	string client_ip = (char*)s;
	char buf[5];

	// string table;
	int rv=0;
	while((rv = recv(clientMap[client_ip].sock_fd, buf, 5, 0))>0)
	{
		string table;
		if(strcmp (buf, "PING")==0)
		{
			//not even required as TTL is not the 30sec associated with recv timeout
			//remove time entirely from map.
			//if ping is recvd, do nothing.
			cout<<"PING from"<<client_ip<<endl;
			
			if(send(clientMap[client_ip].sock_fd, "ACK", 3, 0) <0)
			{
				fprintf(stderr,"Error in sending ping ACK to %s\n",clientMap[client_ip].ip.c_str());
			}
		}
		if(strcmp (buf, "LIST")==0)
		{
			mtx.lock();
			for(auto it=clientMap.begin();it!=clientMap.end();it++)
			{
				table += it->second.ip;
				table += "\n";
			}
			mtx.unlock();

			if(send(clientMap[client_ip].sock_fd, table.c_str(), MAXDATASIZE-1, 0) < 0)
			{
				fprintf(stderr,"Error in sending client table to %s\n",clientMap[client_ip].ip.c_str());
			}
		}
	}

	if(rv<=0)		//recv timedout implies client no longer alive
	{
		cout<<"Client "<<client_ip<<"unreachable. It will be disconnected!"<<endl;
		mtx.lock();
		close(clientMap[client_ip].sock_fd);
		clientMap.erase(client_ip);
		mtx.unlock();
		pthread_exit(NULL);
	}
}

int main(int argc, char const *argv[])
{
	signal(SIGPIPE, SIG_IGN);

	int sockfd, new_fd, yes=1, rv, rc, threadCnt=0;  
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;

	char s[INET6_ADDRSTRLEN];
	string client_ip;

	//for recv timeout
	tv.tv_sec = TTL;  /* 30 Secs Timeout */
	tv.tv_usec = 0;  // Not init'ing this can cause strange errors

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		//for recv timeout
		if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval)))
		{
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}


	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	while(1)
	{
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			s, sizeof s);
		printf("server: got connection from %s\n", s);

		client_ip = s;

		if(threadCnt<MAXTHREADS)
		{
			threadMap[client_ip]=threadCnt++;
			if (send(new_fd, "CONN", 5, 0) == -1)	//send CONN msg to new client
				perror("send");
		}
		else 
		{
			fprintf(stderr,"Server ran out of threads\n");
			if (send(new_fd, "DISC", 5, 0) == -1)		//send DISC msg to new client
				perror("send");
			continue;
		}
		

		mtx.lock();
				
		clientMap[client_ip].ip = client_ip;
		clientMap[client_ip].sock_fd = new_fd;

		mtx.unlock();
		
		if((rc = pthread_create(&threads[threadMap[client_ip]], NULL , threadForClient, (void*)s))!=0)
		{
			fprintf(stderr,"Error:unable to create thread, %d\n",rc);
			close(clientMap[client_ip].sock_fd);
			clientMap.erase(client_ip);
		}

	}


	return 0;
}