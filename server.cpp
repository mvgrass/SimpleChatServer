#include <iostream>
#include <unordered_map>
#include <set>
#include <string>
#include <cstring>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

int set_nonblock(int fd){
	int flags;
#if defined(O_NONBLOCK)
	if(-1==(flags = fcntl(fd, F_GETFL,0)))
		flags = 0;
	return fcntl(fd, F_SETFL, flags| O_NONBLOCK);
#else
	flags = 1;
	return ioctl(fd, FIOBIO, &flags);
#endif
}

const int MAXEVENTS = 32;

int main(){
	
	int MasterSocket = socket(AF_INET, SOCK_STREAM, 0);

	if(MasterSocket == -1){
		std::cerr<<"Can't create MasterSocket\n";
		return 0;
	}

	std::unordered_map<int, char*>SlaveSockets;
	std::set<std::string>Users;
	

	int port;
	std::cout<<"Print free port to connect: ";
	std::cin>>port;

	struct sockaddr_in SockAddr;
	SockAddr.sin_family = AF_INET;
	SockAddr.sin_port = htons(port);
	SockAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(-1==bind(MasterSocket,
		(struct sockaddr*)&SockAddr,
		sizeof(SockAddr))){
		std::cerr<<"Can't bind socket\n";
		return 0;
	}

	set_nonblock(MasterSocket);
	
	if(-1==listen(MasterSocket,SOMAXCONN)){
		std::cerr<<"Bad listen\n";
		return 0;
	}

	std::cout<<"Server started. Port: "<<port<<"\n";

	int Epoll = epoll_create1(0);
	struct epoll_event Event;
	Event.data.fd = MasterSocket;
	Event.events = EPOLLIN;

	epoll_ctl(Epoll,EPOLL_CTL_ADD, MasterSocket, &Event);

	while(true){
		struct epoll_event Events[MAXEVENTS];

		int N = epoll_wait(Epoll, Events, MAXEVENTS, -1);

		for(unsigned int i = 0;i<N;i++){
			if(Events[i].data.fd != MasterSocket){
				if((Events[i].events&EPOLLERR)
					||Events[i].events&EPOLLHUP){
					shutdown(Events[i].data.fd,SHUT_RDWR);
					close(Events[i].data.fd);
					auto It = SlaveSockets.find(Events[i].data.fd);

					char Alert[1024];
					Alert[0] = '\r';
					Alert[1] = '\0';
					strcat(Alert, It->second);
					strcat(Alert,"\n");
					
					Users.erase(It->second);
					delete It->second;
					It->second = nullptr;
					SlaveSockets.erase(It);
					for(auto Iter= SlaveSockets.begin();
						Iter!=SlaveSockets.end();
						Iter++){
						send(Iter->first, 
							Alert, 
							strlen(Alert), 
							MSG_NOSIGNAL);
					}

				}else{
					static char Buffer[1024];
					int Recv_size = recv(Events[i].data.fd, Buffer, 1024,
										MSG_NOSIGNAL);
					
					if(Recv_size == 0){
						shutdown(Events[i].data.fd,SHUT_RDWR);
						close(Events[i].data.fd);
						auto It = SlaveSockets.find(Events[i].data.fd);
			
						char Alert[1024];
						Alert[0] = '\r';
						Alert[1] = '\0';
						strcat(Alert, It->second);
						strcat(Alert,"\n");
						
						for(auto Iter= SlaveSockets.begin();
							Iter!=SlaveSockets.end();
							Iter++){
							
							send(Iter->first, 
								Alert, 
								strlen(Alert),
								MSG_NOSIGNAL);
						}
					
						Users.erase(It->second);
						delete It->second;
						It->second = nullptr;
						SlaveSockets.erase(It);
					}else{
						Buffer[Recv_size++] = '\0';	
						static char Name[1024];
						strcpy(Name, 
							 SlaveSockets.find(Events[i].data.fd)->second);
						strcat(Name,": ");
						for(auto Iter = SlaveSockets.begin();
							Iter!=SlaveSockets.end();
							Iter++){
							static char Message[2048];
							strcpy(Message, Name);
							strcat(Message, Buffer);
							send(Iter->first, Message, strlen(Message),
												MSG_NOSIGNAL);
						
						}
					}
				}
			}else{
				int SlaveSocket = accept(MasterSocket, 0, 0);
				static char Buffer_name[1024];
				int Recv_size = recv(SlaveSocket, Buffer_name, 1024,
										MSG_NOSIGNAL);
				Buffer_name[Recv_size++] = '\0';
				if(Users.find(Buffer_name)==Users.end()){
					send(SlaveSocket, "y", 2, MSG_NOSIGNAL);
					Users.insert(Buffer_name);
				}
				else{
					send(SlaveSocket, "n", 2, MSG_NOSIGNAL);
					continue;
				}
			
				char ans[3];

				recv(SlaveSocket, ans, 3, MSG_NOSIGNAL);
		
				char* Name = (char*)malloc(Recv_size*sizeof(char));
				strcpy(Name, Buffer_name);
					
				for(auto Iter = Users.begin();
					Iter!=Users.end();
					Iter++){
					send(SlaveSocket, (*Iter).c_str(), 
						strlen((*Iter).c_str()),
						MSG_NOSIGNAL);
					recv(SlaveSocket,ans,3, MSG_NOSIGNAL);
				}

				send(SlaveSocket, "\n", 2, MSG_NOSIGNAL);
				
			
				set_nonblock(SlaveSocket);
	
				char Alert[1024];
				Alert[0] = '\n';
				Alert[1] = '\0';
				strcat(Alert, Name);
				strcat(Alert, "\n");				

				for(auto Iter= SlaveSockets.begin();
					Iter!=SlaveSockets.end();
					Iter++){
					send(Iter->first, Alert, strlen(Alert), MSG_NOSIGNAL);
				}

				SlaveSockets[SlaveSocket] = Name;

				struct epoll_event tmp_Event;
				tmp_Event.data.fd = SlaveSocket;
				tmp_Event.events = EPOLLIN;

				epoll_ctl(Epoll, EPOLL_CTL_ADD, SlaveSocket, &tmp_Event);
			}
		}
	}
	
	return 0;
}
