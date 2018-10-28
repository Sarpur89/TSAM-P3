// Author: V_GROUP_10
//
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <vector>
#include <iostream>
#include <sstream>
#include <thread>
#include <map>
#include <assert.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#define BACKLOG  5          // Allowed length of queue of waiting connections

class Node
{
  public:
    int sock;              // socket of client connection

    std::string name;      // Limit length of name of client's user

    std::string host_ip;
    unsigned short port;

    Node(int socket) :

    sock(socket){}

    ~Node(){}            // Virtual destructor defined for base class
};

std::map<int, Node*> connected_servers;

int mapSize = 0; //Global variable that stores the number of servers in the connected_servers map

std::string myName; // Global variable for the groups name

// Open socket for specified port.
//
// Returns -1 if unable to create the socket for any reason.
void listenOtherServer(int socket)
{
    int nread;             // Bytes read from socket
    char buffer[1025];     // Buffer for reading input

    while(true)
    {
       memset(buffer, 0, sizeof(buffer));
       nread = read(socket, buffer, sizeof(buffer));

       if(nread < 0)        // Server has dropped us
       {
          printf("Over and Out\n");
          exit(0);
       }
       else if(nread > 0)
       {
          printf("%s\n", buffer);
       }
    }
}

// A function to set a socket in non-blocking mode.
int set_nonblocking(int sock)
{
  int opt = 1;
  ioctl(sock, FIONBIO, &opt);
  return sock;
}

int open_tcp_socket(int portno)
{
   struct sockaddr_in sk_addr;   // address settings for bind()
   int sock;                     // socket opened for this port
   int set = 1;                  // for setsockopt

   // Create socket for connection
   if((sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) < 0)
   {
      perror("Failed to open TCP socket");
      return(-1);
   }

   if(set_nonblocking(sock) < 0)
   {
      perror("Failed to make non-blocking!");
      exit(1);
   }

   // Turn on SO_REUSEADDR to allow socket to be quickly reused after
   // program exit.
   if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
   {
      perror("Failed to set SO_REUSEADDR:");
   }

   memset(&sk_addr, 0, sizeof(sk_addr));

   sk_addr.sin_family      = AF_INET;
   sk_addr.sin_addr.s_addr = INADDR_ANY;
   sk_addr.sin_port        = htons(portno);

   // Bind to socket to listen for connections from clients
   if(bind(sock, (struct sockaddr *)&sk_addr, sizeof(sk_addr)) < 0)
   {
      perror("Failed to bind to socket:");
      return(-1);
   }

   else
   {
      set_nonblocking(sock);
      return(sock);
   }
}

//Does not work, would have been used to establish a UDP connection
int open_udp_socket(int portno)
{
   struct sockaddr_in sk_addr;   // address settings for bind()
   int sock;                     // socket opened for this port
   int set = 1;                  // for setsockopt

   // Create socket for connection
   if((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
   {
      perror("Failed to open UDP socket");
      return(-1);
   }

   // Turn on SO_REUSEADDR to allow socket to be quickly reused after
   // program exit.
   if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
   {
      perror("Failed to set SO_REUSEADDR:");
   }

   memset(&sk_addr, 0, sizeof(sk_addr));

   sk_addr.sin_family      = AF_INET;
   sk_addr.sin_addr.s_addr = INADDR_ANY;
   sk_addr.sin_port        = htons(portno);

   // Bind to socket to listen for connections from clients
   if(bind(sock, (struct sockaddr *)&sk_addr, sizeof(sk_addr)) < 0)
   {
      perror("Failed to bind to socket:");
      return(-1);
   }
   else
   {
      return(sock);
   }
}

//We were trying to use this function in collaboration with KEEPALIVE
void send_message_to_all()
{
    std::string msg;
    msg = "I'm still here!";
    while(true)
    {
        for(auto const& pair : connected_servers)
        {
            send(pair.second->sock, msg.c_str(), msg.length(),0);
        }
        sleep(60);
    }
}

// Close a client's connection, remove it from the client list, and
// tidy up select sockets afterwards.
void closeClient(int clientSocket, fd_set *openSockets, int *maxfds)
{
     // Remove client from the connected_servers list
     connected_servers.erase(clientSocket);
     mapSize--;

     // If this client's socket is maxfds then the next lowest
     // one has to be determined. Socket fd's can be reused by the Kernel,
     // so there aren't any nice ways to do this.

     if(*maxfds == clientSocket)
     {
        for(auto const& p : connected_servers)
        {
            *maxfds = std::max(*maxfds, p.second->sock);
        }
     }

     // And remove from the list of open sockets.
     FD_CLR(clientSocket, openSockets);
}

// Process command from client on the server
int inputCommand(int clientSocket, fd_set *openSockets, int *maxfds,
                  char *buffer)
{
  std::vector<std::string> tokens;
  std::string token;

  // Split command from client into tokens for parsing
  std::stringstream stream(buffer);

  while(stream >> token)
      tokens.push_back(token);

  if((tokens[0].compare("ID") == 0) && (tokens.size() == 1))
  {
      send(clientSocket, myName.c_str(), myName.length(), 0);
  }

  if((tokens[0].compare("CONNECT") == 0) && (tokens.size() == 2))
  {
     connected_servers[clientSocket]->name = tokens[1];
  }

  //A command used for establishing a connection between the server connected
  //to the client and other servers.
  else if((tokens[0].compare("CONNECT_OTHER") == 0) && (tokens.size() == 3))
  {
      int sock, set = 1, count = 1;
      struct addrinfo sk_addr, *svr;


      /* Create the socket. */
      sk_addr.ai_family   = AF_INET;            // IPv4 only addresses
      sk_addr.ai_socktype = SOCK_STREAM;
      sk_addr.ai_flags    = AI_PASSIVE;

      memset(&sk_addr,   0, sizeof(sk_addr));

      if(getaddrinfo(tokens[1].c_str(), tokens[2].c_str(), &sk_addr, &svr) != 0)
      {
          perror("getaddrinfo failed: ");
          exit(0);
      }

      sock = socket(svr->ai_family, svr->ai_socktype, svr->ai_protocol);

      // Connect to the other server.
      if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
      {
          printf("Failed to set SO_REUSEADDR for port %s\n", tokens[2].c_str());
          perror("setsockopt failed: ");
      }

      if(connect(sock, svr->ai_addr, svr->ai_addrlen )< 0)
      {
          printf("Failed to open socket to server: %s\n", tokens[1].c_str());
          perror("Connect failed: ");
          exit(0);
      }

      else
      {
          // Extracting ip address and port number from sk_addr
          u_short portNo;
          struct sockaddr_in *sin = (struct sockaddr_in *) svr->ai_addr;
          char ipAddress[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, &(sin->sin_addr), ipAddress, INET_ADDRSTRLEN);
          std::string ipString(ipAddress);
          portNo = htons(sin->sin_port);

          //Adding a new node and taking the info from the server being connected
          Node *myNode = new Node(sock);
          myNode->host_ip = ipString;
          myNode->port = portNo;
          mapSize++;
          connected_servers.insert(std::pair<int, Node*>(mapSize, myNode));

          return(sock);
      }
  }

  //Does not work, we wanted to print out a the info about the servers that we
  //wanted to store in a Map. This would then send the list to the server.
  else if(tokens[0].compare("LISTSERVERS") == 0)
  {
      std::cout << "The connected servers are: " << std::endl;
      std::string msg;
      //Checks if the Map is empty
      if(connected_servers.empty())
      {
          printf("List is empty");
          exit(0);
      }
      else
      {
        //A loop used to construct the ID of any connected servers
        for(auto const& names : connected_servers) 
        {
           msg += names.second->name + ",";
           msg += names.second->host_ip + ",";
           msg += names.second->port + ",";
           msg += ";";
        }
        std::cout << msg << std::endl;
        send(clientSocket, msg.c_str(), msg.length()-1, 0);
      }
  }

  //Does not work, we tried to make it work so that a tread would open when the command is called
  //and a function is called that sets a timer within a loop and sends a message to servers after 
  //the timer finished
  else if(tokens[0].compare("KEEPALIVE") == 0)
  {
      thread intervalThread;
      pthread_create(&intervalThread, NULL, send_message_to_all);
  }

  else if(tokens[0].compare("LEAVE") == 0)
  {
      // Close the socket, and leave the socket handling
      // code to deal with tidying up connected_servers etc. when
      // select() detects the OS has torn down the connection.

      closeClient(clientSocket, openSockets, maxfds);
  }
  else if(tokens[0].compare("WHO") == 0)
  {
     std::cout << "Who is logged on" << std::endl;
     std::string msg;

     for(auto const& names : connected_servers)
     {
        msg += names.second->name + ",";
     }
     // Reducing the msg length by 1 loses the excess "," - which
     // granted is totally cheating.
     send(clientSocket, msg.c_str(), msg.length()-1, 0);

  }
  // This is slightly fragile, since it's relying on the order
  // of evaluation of the if statement.
  else if((tokens[0].compare("MSG") == 0) && (tokens[1].compare("ALL") == 0))
  {
      std::string msg;
      for(auto i = tokens.begin()+2;i != tokens.end();i++)
      {
          msg += *i + " ";
      }

      for(auto const& pair : connected_servers)
      {
          send(pair.second->sock, msg.c_str(), msg.length(),0);
      }
  }
  else if(tokens[0].compare("MSG") == 0)
  {
      for(auto const& pair : connected_servers)
      {
          if(pair.second->name.compare(tokens[1]) == 0)
          {
              std::string msg;
              for(auto i = tokens.begin()+2;i != tokens.end();i++)
              {
                  msg += *i + " ";
              }
              send(pair.second->sock, msg.c_str(), msg.length(),0);
          }
      }
  }
  else
  {
      std::cout << "Unknown command from client:" << buffer << std::endl;
  }


}

int main(int argc, char* argv[])
{
    bool finished;
    int listenTCPSock;              // Socket for TCP connections to server
    //int listenUDPSock;              // Socket for UDP connections to server
    int clientSock;                 // Socket of connecting client
    fd_set openSockets;             // Current open sockets
    fd_set readSockets;             // Socket list for select()
    fd_set exceptSockets;           // Exception socket list
    int maxfds;                     // Passed to select() as max fd in set
    struct sockaddr_in client;
    socklen_t clientLen;
    char buffer[1025];              // buffer for reading from clients

    myName = "V_group_10";         //The name of our group/server

    if(argc != 2)
    {
        printf("Usage: chat_server <tcp port>\n");
        exit(0);
    }

    // Setup TCP socket for server to listen to

    listenTCPSock = open_tcp_socket(atoi(argv[1]));
    printf("Listening on TCP port: %s\n", argv[1]);
    //If we would have gotten so far as to implement a UDP connection we would
    //have put in another operation similar to the one above, passing in argv[2].

    if(listen(listenTCPSock, BACKLOG) < 0)
    {
        printf("Listen failed on tcp port %s\n", argv[1]);
        exit(0);
    }
    else
    // Add listen socket to socket set
    {
        FD_SET(listenTCPSock, &openSockets);
        maxfds = listenTCPSock;
    }

    //std::thread serverThread(listenOtherServer, listenTCPSock);
    //Thread used to print messages from other servers

    finished = false;

    while(!finished)
    {
        // Get modifiable copy of readSockets
        readSockets = exceptSockets = openSockets;
        memset(buffer, 0, sizeof(buffer));

        int n = select(maxfds + 1, &readSockets, NULL, &exceptSockets, NULL);

        if(n < 0)
        {
            perror("select failed - closing down\n");
            finished = true;
        }
        else
        {
            // Accept  any new connections to the server
            if(FD_ISSET(listenTCPSock, &readSockets))
            {
               clientSock = accept(listenTCPSock, (struct sockaddr *)&client,
                                   &clientLen);

               FD_SET(clientSock, &openSockets);
               maxfds = std::max(maxfds, clientSock);

               connected_servers[clientSock] = new Node(clientSock);

               n--;

               printf("Client connected on server: %d\n", clientSock);
            }
            // Now check for commands from clients
            while(n-- > 0)
            {
               for(auto const& pair : connected_servers)
               {
                  Node *client = pair.second;

                  if(FD_ISSET(client->sock, &readSockets))
                  {
                      if(recv(client->sock, buffer, sizeof(buffer), MSG_DONTWAIT) == 0)
                      {
                          printf("Client closed connection: %d", client->sock);
                          close(client->sock);

                          closeClient(client->sock, &openSockets, &maxfds);

                      }
                      else
                      {
                          std::cout << buffer << std::endl;
                          inputCommand(client->sock, &openSockets, &maxfds,
                                        buffer);
                      }
                  }
               }
            }
        }
    }
}
