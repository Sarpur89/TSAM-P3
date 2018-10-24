//
// Simple chat server for TSAM-409
//
// Command line: ./chat_server 4000
//
// Author: Jacky Mallett (jacky@ru.is)
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

#define BACKLOG  5          // Allowed length of queue of waiting connections

// Simple class for handling connections from clients.
//
// Client(int socket) - socket to send/receive traffic from client.
class Node
{
  public:
    int sock;              // socket of client connection

    std::string name;      // Limit length of name of client's user

    // Næstu tvær línur bætti ég inn til að gefa meiri möguleika í flokkun server-a í connected_servers. Óþarft? Betra að útfæra á annan hátt?
    std::string host_ip;
    unsigned short port;

    Node(int socket) :

    sock(socket){}

    ~Node(){}            // Virtual destructor defined for base class
};

// Note: map is not necessarily the most efficient method to use here,
// especially for a server with large numbers of simulataneous connections,
// where performance is also expected to be an issue.
//
// Quite often a simple array can be used as a lookup table,
// (indexed on socket no.) sacrificing memory for speed.

// Er nauðsynlegt að hafa tvö maps? Hugmyndin er að hafa eitt fyrir client of eitt fyrir servera, en tengingarnar eru í raun alveg eins (AFAIK)...
//std::map<int, Node*> connected_clients; // Lookup table for per Node information
std::map<int, Node*> connected_servers;

std::string myName; // Global breyta fyrir nafn hópsins.

// Open socket for specified port.
//
// Returns -1 if unable to create the socket for any reason.

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

   // Turn on SO_REUSEADDR to allow socket to be quickly reused after
   // program exit.

   if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(set)) < 0)
   {
      perror("Failed to set SO_REUSEADDR:");
   }

   //Skoða þetta betur
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

// Er nauðsynlegt að hafa heilt nýtt fall fyrir UDP tengingar?
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

   //Skoða þetta betur
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

// Close a client's connection, remove it from the client list, and
// tidy up select sockets afterwards.

void closeClient(int clientSocket, fd_set *openSockets, int *maxfds)
{
     // Remove client from the connected_servers list
     connected_servers.erase(clientSocket);

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

  // ID virkar
  if((tokens[0].compare("ID") == 0) && (tokens.size() == 1))
  {
      send(clientSocket, myName.c_str(), myName.length(), 0);
  }

  if((tokens[0].compare("CONNECT") == 0) && (tokens.size() == 2))
  {
     connected_servers[clientSocket]->name = tokens[1];
  }

  else if((tokens[0].compare("CONNECT_OTHER") == 0) && (tokens.size() == 3))
  {
      //extern void init_sockaddr (struct sockaddr_in *name, const char *hostname, uint16_t port);
      std::string hostname;
      int sock, set = 1;
      struct addrinfo sk_addr, *svr;

      // hostname = tokens[1].c_str();

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
         return(sock);
      }
  }

  else if(tokens[0].compare("LISTSERVERS") == 0)
  {
      std::cout << "The connected servers are: " << std::endl;
      std::string msg;
      for(auto const& names : connected_servers) // Á að skeyta saman: Hóp_nafn,IP_tala,Port_númer; á öllum connections í connected_servers.
      {
         msg += names.second->name + ",";
         msg += names.second->host_ip + ",";
         msg += names.second->port + ",";
         msg += ";";
      }
      send(clientSocket, msg.c_str(), msg.length()-1, 0); // Mínus einn til að losna við síðustu kommuna.
  }

  // Til að sjá ef server getur tekið við skipunum.
  else if(tokens[0].compare("TEST") == 0)
  {
      printf("Test command recieved, over!\n");
  }

 /* // CMD virkar ekki. Ég er að reyna að tengja með port númeri (ég veit að það á að vera username,
    // en held að það sé betra að útfæra það síðar). Veit ekki af hverju það virkar ekki...
  else if(tokens[0].compare("CMD") == 0 && (tokens.size() == 3))
  {
      for(auto const& pair : connected_servers)
      {
          if(pair.second->port == stoi(tokens[1]))
          {
              std::string msg;
              send(pair.second->sock, msg.c_str(), msg.length(),0);
          }
          printf("Failed to send to socket: %s\n", tokens[1].c_str());
      }
  }
*/
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
    int listenUDPSock;              // Socket for UDP connections to server
    int clientSock;                 // Socket of connecting client
    fd_set openSockets;             // Current open sockets
    fd_set readSockets;             // Socket list for select()
    fd_set exceptSockets;           // Exception socket list
    int maxfds;                     // Passed to select() as max fd in set
    struct sockaddr_in client;
    socklen_t clientLen;
    char buffer[1025];              // buffer for reading from clients

    myName = "V_group_10";

    if(argc != 2)
    {
        printf("Usage: chat_server <tcp port> <udp port> <client port>\n");
        exit(0);
    }

    // Setup TCP socket for server to listen to

    listenTCPSock = open_tcp_socket(atoi(argv[1]));
    printf("Listening on TCP port: %s\n", argv[1]);
    //listenUDPSock = open_udp_socket(atoi(argv[2]));
    //printf("Listening on UDP port: %s\n", argv[2]);

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

               // Er það sem er hér fyrir neðan nauðsynlegt (næstu tvær línur)?
               connected_servers[clientSock]->host_ip = client.sin_addr.s_addr;
               connected_servers[clientSock]->port = client.sin_port;

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
