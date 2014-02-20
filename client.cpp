//make send thread exit as sooon as rcv dies and vice versa using bools
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

using namespace std;

#define SERVERPORT "8888"  // the port users will be connecting to
#define CLIENTPORT "6666"	// port on which client listens for other peers' connections
#define PEERPORT "7777"		//port which a client uses to connect with other clients' listening port

#define BACKLOG 10	 // how many pending connections queue will hold

#define MAXTHREADS 3

#define TTL 5

#define MAXDATASIZE 100

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_RESET   "\x1b[0m"


pthread_t threads[MAXTHREADS]; //thread[0] for ping, thread[1] for peer send, thread[2] for peer rcv
mutex mtx;
FILE *f = fopen("chat.txt","a");
bool pingAlive, sendAlive, rcvAlive;	//to check running status of the 3 threads.
//consider if mutx req. for above or not.

//for recv timeout in ACK from server
struct timeval tv;

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


void *sendPing(void *fd)
{
	int rv;
	int sockfd=*((int *)fd);
	char buf[5];
	while(1)
	{
		pingAlive=true;
		if(send(sockfd, "PING", 5, 0) <0)
		{
			fprintf(stderr,"Error in sending PING to server\n");
		}

		if((rv = recv(sockfd, buf, 5, 0))>0)
		{
			if(strcmp(buf, "ACK"))
				cout<<"Ping ACKed\n";
		}
		else 
		{
			cout<<"Server Down! Application will exit after current chat is over...\n";
			close(sockfd);
			pingAlive=false;
			pthread_exit(NULL);
		}
		usleep(10000000); //10 sec
	}
}

bool getOnlineClients(int sockfd)
{
	int rv;
	if(send(sockfd, "LIST", 5, 0) <0)
	{
		fprintf(stderr,"Error in sending LIST to server\n");
	}

	char buf[MAXDATASIZE];
	if((rv = recv(sockfd, buf, MAXDATASIZE-1, 0))>0)
	{
		cout<<string(buf);
		cout<<"Enter the IP address you want to connect to: ";
	}
	else
	{
		cout<<"Server Down! Application will exit after current chat is over...\n";
		close(sockfd);
		return false;
	}
	return true;

}


void *chatSend(void *fd)
{
	int socket=*((int *)fd);
	int msgNo=1;
	while(1)
	{
		sendAlive=true;
		char msg[MAXDATASIZE];
		string buf;

		memset(msg, '\0', sizeof(msg));
		cout<<"#"<<msgNo++<<":";
		fgets(msg, MAXDATASIZE-3, stdin);
		
		if(msg[0]=='\0')	//client wishes to close connection
		{
			cout<<"Connection to peer closed."<<endl;
			close(socket);		//consider this. this should not be done.
			mtx.lock();
			fclose(f);
			//also remove file
			mtx.unlock();
			sendAlive=false;
			pthread_exit(NULL);
		}

		buf="#"+to_string(msgNo-1)+":";
		buf+=string(msg);

		if(send(socket, (char *)(buf.c_str()), size_t(buf.size()), 0) <0)
		{
			fprintf(stderr,"Error in sending msg to peer\n");
		}

		//writting to chat file
		mtx.lock();
		fprintf(f, "%s", (char *)(buf.c_str()));
		mtx.unlock();

	}
}

void *chatRcv(void *fd)
{
	int socket=*((int *)fd), rv;
	while(1)
	{
		rcvAlive=true;
		char msg[MAXDATASIZE];
		string buf;
		if((rv=recv(socket, msg, MAXDATASIZE, 0))>0)
		{
			string sMsg= string(msg);
			if(sMsg.substr(0,3)=="ACK")		//ACK for msg received
			{
				buf+="-------------------------------------------------\n";
				buf+="MSG:"+sMsg.substr(3,sMsg.size()-1)+"seen.\n";
				buf+="-------------------------------------------------\n";
			}
			/*
			MSG received. Need to send ACK.
			message format - #13:hello
			*/
			else
			{
				string msgNo="ACK";	//to extract msgNo. from message and build the ACK response
				int i=1;	//skipping the starting '#'
				while(sMsg[i]!=':')
				{
					i++;
					msgNo+=sMsg[i];		//extracting msgNo. from the message.
				}
				buf=sMsg+"\n";

				//sending ACK for the above message.

				if(send(socket, (char *)(msgNo.c_str()), size_t(sMsg.size()), 0) <0)
				{
					fprintf(stderr,"Error in sending ACK to peer\n");
				}

			}
		}
		else if (rv<0)
		{
			perror("rcv");
			continue;
		}
		else //rv==0
		{
			cout<<"Connection closed by peer. Chat will exit.";
			close(socket);		//think about this. you can't close socket.
			mtx.lock();
			fclose(f);
			//also remove file
			mtx.unlock();
			rcvAlive=false;
			pthread_exit(NULL);
		}

		//writting to chat file.
		mtx.lock();
		fprintf(f, "%s", (char *)(buf.c_str()));
		mtx.unlock();
	}

}


int main(int argc, char const *argv[])
{
	int sockfd, numbytes, yes=1, rc;
	char buf[MAXDATASIZE];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];
	string peerIP;
	struct sigaction sa;
	socklen_t sin_size;
	
	//recv timeout for ACK from server
	tv.tv_sec = 3;  /* 3 Secs Timeout */
	tv.tv_usec = 0;  // Not init'ing this can cause strange errors

	if (argc != 2) {
		fprintf(stderr,"usage: client hostname\n");
		exit(1);
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(argv[1], SERVERPORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

		// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) 
	{
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
			p->ai_protocol)) == -1) 
		{
			perror("client: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		//for recv timeout from server
		if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval)))
		{
			perror("setsockopt");
			exit(1);
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("client: connect");
			continue;
		}

		break;
	}
	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
		s, sizeof s);
	printf("client: connecting to server at %s\n", s);

	freeaddrinfo(servinfo); // all done with this structure

	char serverMsg[5];
	memset(serverMsg,'\0', 5);

	//recv CONN or DISC code from server
	if((rv = recv(sockfd, serverMsg, 5, 0)) <= 0 )
	{
		cout<<"Code Receive from server failed. Application will exit..."<<endl;
		exit(1);
	}

	if(strcmp(serverMsg, "DISC")==0)	//DISC code not received. thread overflow at server
	{
		cout<<"Thread overflow at server. Application will exit..."<<endl;
		exit(1);
	}

	if(strcmp(serverMsg, "CONN")==0)	
	{
		cout<<"Successfully connected to server"<<endl;
		exit(1);
	}
	
	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}


	if((rc = pthread_create(&threads[0], NULL , sendPing, (void*)sockfd))!=0) ;
	{
		fprintf(stderr,"Error:unable to create thread, %d\n",rc);
		return 1;
	}


	/*------- creating socket for chat and binding it to port. Will be used both for connect and listen-----*/

	
	int clientSocket, new_fd;
	// char buf[MAXDATASIZE];
	struct addrinfo *clientInfo;
	struct sockaddr_storage their_addr; // connector's address information
	// char s[INET6_ADDRSTRLEN];
	// string peerIP;
	
	//recv timeout for ACK from server
	tv.tv_sec = 5;  /* 5 Secs Timeout */
	tv.tv_usec = 0;  // Not init'ing this can cause strange errors

	if (argc != 2) {
		fprintf(stderr,"usage: client hostname\n");
		exit(1);
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, CLIENTPORT, &hints, &clientInfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and connect to the first we can
	for(p = clientInfo; p != NULL; p = p->ai_next) 
	{
		if ((clientSocket = socket(p->ai_family, p->ai_socktype,
			p->ai_protocol)) == -1) 
		{
			perror("client: socket");
			continue;
		}

		if (setsockopt(clientSocket, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(clientSocket, p->ai_addr, p->ai_addrlen) == -1) {
			close(clientSocket);
			perror("server: bind");
			continue;
		}

		break;
	}
	if (p == NULL) {
		fprintf(stderr, "client: failed to connect\n");
		return 2;
	}

	freeaddrinfo(clientInfo); // all done with this structure
	
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


	//creating sets of file descriptors to be used for select command to poll them for activity
	fd_set readfds;
	FD_ZERO(&readfds);	//clears the new set

	while(1)
	{
		printf("waiting for connections...\n");
		
		FD_SET(clientSocket, &readfds);		//used for adding given fd to a set
		FD_SET(0, &readfds);

		int choice;
		cout<<"\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"<<endl;
		cout<<"Press appropriate key:"<<endl;
		cout<<"Press 1 to get Online Clients List."<<endl;
		cout<<"Press 2 to connect to a peer (need its IP address)."<<endl;
		cout<<"\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"<<endl;
		
		select(clientSocket+1, &readfds, NULL, NULL, NULL);

		//If peer gets first
		if(FD_ISSET(clientSocket, &readfds))
		{
			cout<<"peer got here first\n";
			char peerMsg[5];
			memset(peerMsg, '\0', 5);

			sin_size = sizeof their_addr;
			new_fd = accept(clientSocket, (struct sockaddr *)&their_addr, &sin_size);
			if (new_fd == -1) 
			{
				perror("accept");
				continue;
			}

			inet_ntop(their_addr.ss_family,
				get_in_addr((struct sockaddr *)&their_addr),
				s, sizeof s);
			printf("got connection from %s\n", s);
			peerIP = s;
			string ans;
			cout<<"Do you want to chat with "<<peerIP<<"?[y/n]:";
			cin>>ans;
			if(ans == "y")
			{
				send(new_fd, (char*)(ans.c_str()), 1, 0);
			}
			else
			{
				send(new_fd, (char*)(ans.c_str()), 1, 0);
				close(new_fd);
				continue;
			}

			//create threads for chat send and chat rcv.
			if(pthread_create(&threads[1], NULL , chatSend, (void*)new_fd)!=0) //for send
			{
				cout<<"Failed to create new thread for chat. Connection to peer will be closed ";
				close(new_fd);
			}
			if( pthread_create(&threads[2], NULL , chatRcv, (void*)new_fd)!=0)
			{
				cout<<"Failed to create new thread for chat. Connection to peer will be closed ";
				close(new_fd);
			}

			//wait till the above threads die.
			while(sendAlive && rcvAlive);
		}

		//if stdin gets first
		if(FD_ISSET(0, &readfds))
		{
			cout<<"stdin got here first\n";
			cin>>choice;

			int peerSocket;
			// char buf[MAXDATASIZE];
			struct addrinfo *peerinfo;
			struct sockaddr_storage their_addr; // connector's address information
			char s[INET6_ADDRSTRLEN];
			// string peerIP;
			struct sigaction sa;


			switch(choice)
			{
				case 1:		//get online client list from server
						if(getOnlineClients(sockfd)) continue;
						else exit(1);
						break;

				case 2:		//connect to peer
						cin>>peerIP;
						memset(&hints, 0, sizeof hints);
						hints.ai_family = AF_UNSPEC;
						hints.ai_socktype = SOCK_STREAM;

						// char *temp= (char *) peerIP
						if ((rv = getaddrinfo((char *)(peerIP.c_str()), PEERPORT, &hints, &peerinfo)) != 0) {
							fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
							return 1;
						}

							// loop through all the results and connect to the first we can
						for(p = peerinfo; p != NULL; p = p->ai_next) 
						{
							if ((peerSocket = socket(p->ai_family, p->ai_socktype,
								p->ai_protocol)) == -1) 
							{
								perror("client: socket");
								continue;
							}

							if (setsockopt(peerSocket, SOL_SOCKET, SO_REUSEADDR, &yes,
									sizeof(int)) == -1) {
								perror("setsockopt");
								exit(1);
							}

							if (connect(peerSocket, p->ai_addr, p->ai_addrlen) == -1) {
								close(peerSocket);
								perror("client: connect");
								continue;
							}

							break;
						}
						if (p == NULL) {
							fprintf(stderr, "client: failed to connect\n");
							return 2;
						}

						inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
							s, sizeof s);
						printf("connecting to peer at %s\n", s);

						freeaddrinfo(peerinfo); // all done with this structure

						//create threads for chat send and chat rcv.
						if(pthread_create(&threads[1], NULL , chatSend, (void*)peerSocket)!=0) //for send
						{
							cout<<"Failed to create new thread for chat. Connection to peer will be closed ";
							close(peerSocket);
						}
						if( pthread_create(&threads[2], NULL , chatRcv, (void*)peerSocket)!=0)
						{
							cout<<"Failed to create new thread for chat. Connection to peer will be closed ";
							close(peerSocket);
						}

						//wait for both threads to complete
						while(sendAlive && rcvAlive);
						
						break;	//break of switch case statement

				default:
						cout<<"Enter a valid choice..."<<endl;
			}
		}
		//check status of ping thread, and if it is dead, (and given that chat is not happening here) kill application
		if(!pingAlive)	//connection to server is lost. application should exit
		{
			cout<<"Connection to server lost. Application will now exit...";
			return 1;
		}
		
	}

	return 0;

}

