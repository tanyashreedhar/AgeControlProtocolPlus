//Imports
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h> //for logging
#include <fstream> //for logging


#define MYPORT "49050"
#define MAXBUFLEN 2048 //Ok?

struct timespec currentTime,startTime;


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


//Blindly imported for time calcs for logging
void timespec_diff(struct timespec *start, struct timespec *stop, struct timespec *result){
    if ((stop->tv_nsec - start->tv_nsec) < 0) {
        result->tv_sec = stop->tv_sec - start->tv_sec - 1;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
    } else {
        result->tv_sec = stop->tv_sec - start->tv_sec;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec;
    }

    return;
}
double getDoubleTimeDiff(struct timespec *start, struct timespec *stop){
    struct timespec result_spec;
    double result;

    timespec_diff(start,stop,&result_spec);

    result = (double)result_spec.tv_sec + ((double)(result_spec.tv_nsec))*(1e-9);
    return result;
}
double getDoubleTime(struct timespec yTime){
    double result;
    result = (double)yTime.tv_sec + ((double)(yTime.tv_nsec))*(1e-9);

    return result;
}


unsigned char *package(unsigned int seq){
    unsigned char *data = (unsigned char *)malloc(1024*sizeof(unsigned char));

    unsigned int n1 = seq>>24;
    unsigned int n2 = (seq>>16) & 0xff;
    unsigned int n3 = (seq>>8) & 0xff;
    unsigned int n4 = seq & 0xff;


    data[0] = (unsigned char)n1;
    data[1] = (unsigned char)n2;
    data[2] = (unsigned char)n3;
    data[3] = (unsigned char)n4;

    return data;
}

unsigned int unpack(unsigned char buffer[]){
    unsigned int l1,l2,l3,l4;

    l1 = (unsigned int)buffer[0];
    l2 = (unsigned int)buffer[1];
    l3 = (unsigned int)buffer[2];
    l4 = (unsigned int)buffer[3];

    unsigned int sq = (l1<<24) + (l2<<16) + (l3<<8) + l4;

    return sq;
}

std::ofstream fileServerLog("Log/Server_log.txt", std::ios::out);

int main(void)
{
  //Variable Declarations
  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  int numbytes;
  struct sockaddr_storage their_addr;//sockaddr_storage pr sockaddr_in?
  // char buf[MAXBUFLEN];
  unsigned char buf[MAXBUFLEN];
  socklen_t addr_len;
  char s[INET6_ADDRSTRLEN];
  unsigned int seq = 0; //New boy

  //Specifying IP type and Data Packet type
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE; // use my IP

  if ((rv = getaddrinfo(NULL, MYPORT, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  // loop through all the results and bind to the first we can
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype,
        p->ai_protocol)) == -1) {
      perror("listener: socket");
      continue;
    }
    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("listener: bind");
      continue;
    }
    break;
  }

  if (p == NULL) {
    fprintf(stderr, "listener: failed to bind socket\n");
    return 2;
  }

  freeaddrinfo(servinfo);

  printf("listener: waiting to recvfrom...\n");

  //Assign value to startTime, print error, if CANT
  if(clock_gettime(CLOCK_REALTIME,&startTime)==-1){
    printf("error\n");
  }  
  fileServerLog << "Seq;Time;Timestamp;PacketSize\n";

  do{
    memset(buf,0,sizeof(buf));//might not be needed

    addr_len = sizeof their_addr;
    if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0,
      (struct sockaddr *)&their_addr, &addr_len)) == -1) {
      perror("recvfrom");
      exit(1);
    }

    seq = unpack(buf);//get the sequence number as int from packet

    if(clock_gettime(CLOCK_REALTIME,&currentTime)==-1){
      printf("error\n");
    }
    fileServerLog << seq << ";" << getDoubleTimeDiff(&startTime,&currentTime) << ";" << getDoubleTime(currentTime) << ";" << numbytes << "\n";

    //outputs reciving a packet
    /*printf("listener: got packet from %s\n",
      inet_ntop(their_addr.ss_family,
        get_in_addr((struct sockaddr *)&their_addr),
        s, sizeof s));
    printf("listener: packet is %d bytes long\n", numbytes);
    buf[numbytes] = '\0';
    printf("listener: packet contains \"%s\"\n", buf);*/
    printf("listener: Recieved packet containing %d bytes\n", numbytes);

    
    //Let's ACK that we recieved the packet, on same socket
    unsigned char *packet = package(seq);
    if ((numbytes = sendto(sockfd, packet, 20, 0,(struct sockaddr *)&their_addr, addr_len)) == -1) {
      perror("talker: sendto");
      exit(1);
    }
    free(packet);

    //if(getDoubleTimeDiff(&startTime,&currentTime)>=500.0)
      //break;

  }while(true);
  

  close(sockfd);
  return 0;
}
