//`g++ -std=c++11 client.cpp -o client` use that to compile this file
//`./client localhost 526 500 2` to run it
//tanyas: ArrivalTime(time for the next packet) is equal to RTT
//
//
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
#include <ctime>
#include <iostream>
#include <fstream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>  
#include <cstdlib>
#include <mutex>

#include <chrono>
#include <thread>

#define MAXBUFLEN 2048 //Ok?
#define ITERATIONS 500000

std::mutex mtx;

using namespace std;

//Variable Declarations
const char* SERVERPORT= "49050";
const double ageAlpha = 0.2;
const int array_size = 50000;
const int age_size = 50000;
const int local_size = 1000;
const int small_size = 100;

struct timespec start_time;
struct timespec *tx,*rx;
time_t *systemTime;
time_t RTT = 0.0;

static uint32_t transmission_seq;
static uint32_t receiver_seq;
int lastSeqNum = 0;
int outOfOrderPackets = 0;
double interRate = 0.5; //this is lambda

static double age_estimate[age_size] = {0};
static double lambda[age_size] = {0};

static time_t controlPacketDelay[age_size] = {0};
static time_t controlDepartureTime[age_size] = {0};
time_t currentControlTime = 0;
static int controlIndex = 0;

static time_t controlTime = 0;
static time_t depTime = 0;
static time_t depTime_control= 0;
static time_t prevReceiveTime = 0;
static double prevAverageBacklog = 0;
static double currentAverageBacklog = 0;
static double calcLambda = 0;
static double n=0;

static double globalBacklog[age_size] = {0};
static time_t backlogTime[age_size] = {0};
static int backlogIndex = 0;
static double desiredChangeinBacklog = 0;
static double prev_age_estimate = 0;
static double recent_age_estimate = 0;

static time_t received_delay[array_size];
int update_index = 1;
int lambda_index = 1;

bool firstpacket = false;
bool firstcontrol = true;
bool controlTaken = false;

static int current_action = 0;
static double alpha = 0.2;

time_t controlPacketDelay_local[local_size]={0};
time_t controlDepartureTime_local[local_size]={0};
time_t backlogTime_local[local_size] = {0};
double globalBacklog_local[local_size] = {0};


//Log files opening these files in overwite(out) append state is app
ofstream fileOOOLog("Log/Out_of_Order_log.txt", ios::out);
ofstream fileTxLog("Log/Tx_log.txt", ios::out);
ofstream fileTxServerLog("Log/TxServer_log.txt", ios::out);
ofstream fileTxLog2("Log/Tx_log2.txt", ios::out);
ofstream fileRxLog("Log/Rx_log.txt", ios::out);
ofstream fileArrLog("Log/Arrival_log.txt", ios::out);
ofstream fileYLog("Log/Y_log.txt", ios::out);
ofstream fileAgeEst("Log/Age_Est.txt", ios::out);
ofstream fileintArrLog("Log/intArr_log.txt", ios::out);
ofstream fileServerScheduleLog("Log/ServerSchedule_log.txt", ios::out);
ofstream filecontrol("Log/control_log.txt", ios::out);
ofstream filebacklogArr("Log/backlog_arrival.txt", ios::out);
ofstream filelambdaLog("Log/lambdaLog.txt", ios::out);
ofstream fileAvgbacklog("Log/Avg_backlog.txt", ios::out);
ofstream fileDebugAge("Log/debug_age.txt", ios::out);
ofstream fileRttLog("Log/RTT_Log.txt", ios::out);
ofstream fileDepTimeLog("Log/DepTime_Log.txt", ios::out);
ofstream fileDesiredChangeLog("Log/Desired_backlog.txt", ios::out);
ofstream fileCopyTimes("Log/CopyTimes.txt", ios::out);

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

time_t getDoubleTimeDiff(struct timespec *start, struct timespec *stop){
  struct timespec result_spec;
  time_t result;

  timespec_diff(start,stop,&result_spec);

  result = result_spec.tv_sec * 1e9 + ((result_spec.tv_nsec));
  return result;
}

time_t getDoubleTimeNow(){
  //Double as in the data type
  struct timespec temp_time, result_time;
  time_t result;
  if(clock_gettime(CLOCK_REALTIME,&temp_time)==-1){
    printf("getDoubleTimeNow ERROR\n");
    exit(1);
  }
  timespec_diff(&start_time, &temp_time, &result_time);

  result = result_time.tv_sec * 1e9 + ((result_time.tv_nsec));
  return result;
}


unsigned char *package(unsigned int seq) {
    
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

unsigned int unpack(unsigned char buffer[]) {
    unsigned int l1,l2,l3,l4;

    l1 = (unsigned int)buffer[0];
    l2 = (unsigned int)buffer[1];
    l3 = (unsigned int)buffer[2];
    l4 = (unsigned int)buffer[3];

    unsigned int sq = (l1<<24) + (l2<<16) + (l3<<8) + l4;

    return sq;
}

void *Receiver(void *sock) {
  //printf("Entering Rx Block\n");
  int sockfd = *(int *)sock;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  int numbytes;
  int isFirstPacket = 1;
  
  struct sockaddr_storage their_addr;
  unsigned char buf[MAXBUFLEN];
  socklen_t addr_len;
  char s[INET6_ADDRSTRLEN];
  struct timespec stamp;
  
  while(1){
    memset(buf,0,sizeof(buf));
    addr_len = sizeof their_addr;
    if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
      perror("recvfrom");
      exit(1);
    }
    
    receiver_seq = unpack(buf);
    //printf("receiver seq is %d\n",receiver_seq);
    //getBacklogArrival(getDoubleTimeNow());

    time_t currentReceiveTime = getDoubleTimeNow();

    //printf("GOT Back %d\n", receiver_seq);

    if(receiver_seq<lastSeqNum){
      outOfOrderPackets += 1;
    }else{
      lastSeqNum = receiver_seq;
    }
    fileOOOLog << getDoubleTimeNow() << "\t" << outOfOrderPackets << std::endl;

    if(clock_gettime(CLOCK_REALTIME,&rx[receiver_seq])==-1){
      printf("error\n");
    }
    printf("The receiver_seq is %u\n", receiver_seq);
    systemTime[receiver_seq] = getDoubleTimeDiff(&tx[receiver_seq], &rx[receiver_seq]);
    received_delay[receiver_seq] = systemTime[receiver_seq];
    
    if(receiver_seq == 1){ //change it to the first packet recved
mtx.lock();
      RTT = systemTime[receiver_seq];
mtx.unlock();
      printf("FIRST RTT IS %lu\n",RTT);
      
      // pthread_t thread_control;
      // int cn;
      // cn = pthread_create(&thread_control, NULL, controlAction,NULL);
      // if(cn==-1){
      //   perror("controlAction Thread gone\n");
      //   exit(1);
      // }
    }
    else{
      mtx.lock();
      RTT = (1.0 - ageAlpha)*RTT + ageAlpha*systemTime[receiver_seq];
      mtx.unlock();
      //printf("RTT IS  %f\n",RTT);
    }

    // printf("afterr");
    time_t departureTime;
    mtx.lock();
    if(receiver_seq == 1){
      departureTime = currentReceiveTime;
      depTime = departureTime;
    } else {
      departureTime = currentReceiveTime - prevReceiveTime;
      time_t oldDepTime = depTime;
      depTime = (1 - alpha) * oldDepTime + alpha*departureTime;
    }
    prevReceiveTime = currentReceiveTime;

//  
    mtx.unlock();		//done with mutex for control
    //printf("Exiting Rx Block\n");
    fileYLog << getDoubleTimeNow() << "\t" << RTT << "\t" << systemTime[receiver_seq]<< "\t" << depTime << "\t" << controlPacketDelay[controlIndex - 1] << "\t" << controlDepartureTime[controlIndex - 1] << "\t" << controlIndex << "\t" << currentReceiveTime << "\t" << currentControlTime << "\t" << std::endl;

    fileRxLog << getDoubleTimeNow() << "\t" << receiver_seq << "\t" << systemTime[receiver_seq] << endl;
  }
}

time_t ping_to_get_rtt(int sockfd,struct addrinfo *p){
  // getDoubleTimeNow()
  time_t rtt_total = 0;
  int seqNum;

  for(seqNum = 0; seqNum < 10; seqNum++){
    unsigned char* packet = package((unsigned)seqNum);
    time_t send_time = getDoubleTimeNow();
    if (sendto(sockfd, packet, 64, 0, p->ai_addr, p->ai_addrlen) == -1) {
      perror("talker1: sendto");
      exit(1);
    }

    struct sockaddr_storage their_addr;
    socklen_t addr_len;
    unsigned char buf[MAXBUFLEN];
    //int receiver_seq;

    memset(buf,0,sizeof(buf));
    addr_len = sizeof their_addr;
    if (recvfrom(sockfd, buf, MAXBUFLEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len) == -1) {
      perror("recvfrom");
      exit(1);
    }
    time_t recv_time = getDoubleTimeNow();

    receiver_seq = unpack(buf);
    if(receiver_seq!=seqNum){
      printf("I wanted packet %d BUT I got %d", seqNum, receiver_seq);
      exit(1);
    }
    rtt_total+=(recv_time-send_time);
  }//for;
  RTT= rtt_total/seqNum;//avg RTT
  //return RTT;
}

int main(int argc, char *argv[])
{

  printf("Size of time_t %lu\n\n", sizeof(time_t));
  printf("Size of double %lu\n\n", sizeof(double));
  printf("Size of uint_64 %lu\n\n", sizeof(uint64_t));

  if(clock_gettime(CLOCK_REALTIME,&start_time)==-1){
    printf("error\n");
  }
   
  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  int numbytes;

  //array of transmission+recieving times
  tx = (struct timespec *)malloc(ITERATIONS*sizeof(struct timespec));
  rx = (struct timespec *)malloc(ITERATIONS*sizeof(struct timespec));
  systemTime = (time_t *)malloc(ITERATIONS*sizeof(time_t));

  if (argc != 6) {
    fprintf(stderr,"Usage: talker hostname message_bytes num_messages inter_arrival_time port\n");
    exit(1);
  }

  SERVERPORT = argv[5];

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;

  if ((rv = getaddrinfo(argv[1], SERVERPORT, &hints, &servinfo)) != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  // loop through all the results and make a socket
  for(p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype,
        p->ai_protocol)) == -1) {
      perror("talker: socket");
      continue;
    }
    break;
  }

  if (p == NULL) {
    fprintf(stderr, "talker: failed to create socket\n");
    return 2;
  }

  printf("Before ping\n");
  ping_to_get_rtt(sockfd,p);
  // interRate = 1e9/ping_to_get_rtt(sockfd, p);
  // printf("%f\n", 1/interRate);
  printf("After ping\n");

  pthread_t thread_rec;
  int rc;
  /************* Receiver Initialization begins *****************/
  rc = pthread_create(&thread_rec, NULL, Receiver, &sockfd);
  if(rc==-1){
    perror("Receiver Thread gone\n");
    exit(1);
  }
  /************* Receiver Initialization ends *****************/
    //static int sockfd_new = sockfd;
    static int numbytes_new = numbytes;

  //seqNum is m_packetsSent
  for(int seqNum=1; seqNum<=stoi(argv[3]); seqNum++){//message_num times
    
    // static int scheduleTime=stoi(argv[4]);//inter arrival time
    mtx.lock();
    time_t arrivalTime = RTT;
    mtx.unlock();
    //fileintArrLog << getDoubleTimeNow() << '\t' << arrivalTime << '\t' << seqNum << '\n';
    //filelambdaLog << getDoubleTimeNow() << '\t' << interRate << '\n';
    
    //this_thread::sleep_for(chrono::nanoseconds((long long int)arrivalTime*1000000000));

    cout << "Current arrival time" << "\t" << arrivalTime << endl;
    this_thread::sleep_for(chrono::nanoseconds(arrivalTime));

    unsigned char* packet = package((unsigned)seqNum);
    cout << "sending a packet seq:"<<(unsigned)seqNum<<" of length "<<sizeof(packet)/2<<"\n";


    // if ((numbytes_new = sendto(sockfd_new, packet, sizeof(packet)/2, 0, //strlen((char *)packet) does not give size
    if(stoi(argv[2])<4){
      printf("Too less bytes. I need minimum 4");
      exit(1);
    }

    if(clock_gettime(CLOCK_REALTIME,&tx[seqNum])==-1){
      printf("error\n");
    }
    cout << "send to file descriptor" << sockfd << endl;
    if ((numbytes_new = sendto(sockfd, packet, stoi(argv[2]), 0,
         p->ai_addr, p->ai_addrlen)) == -1) {
      perror("talker2: sendto");
      exit(1);
    }
  
    //printf("Entering sender Block\n");
    //PACKET SENT
    transmission_seq = seqNum;
    fileTxLog << getDoubleTimeNow() << "\t" << arrivalTime << "\t" << seqNum << '\t' << '\n'; //Logging

    //getBacklogArrival(getDoubleTimeNow());

    //printf("Exiting sender Block\n");

  }
  this_thread::sleep_for(chrono::seconds(3));//hang on for last packets to come back :P 

  freeaddrinfo(servinfo);

  // printf("talker: sent %d bytes to %s\n", numbytes, argv[1]);
  // close(sockfd);

  return 0;
}
