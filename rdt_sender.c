#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>
#include <limits.h>

#include"packet.h"
#include"common.h"
#include"linked_list.h"

#define STDIN_FD    0
#define RETRY  180 //millisecond
#define ALPHA 0.125 // for estimatedRTT
#define BETA 0.25 // for devRTT

int SLOW_START = 1;
int CONGESTION_AVOIDANCE = 0;

int next_seqno=0;
int send_base=0;
int MAX_WINDOW = 10;       // changed from 1 to 10
float cwnd = 1;
int ssthresh = 64;
int shift_after_ack = 0;      // keep track of number of packets acked

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;       

linked_list* pktbuffer;             // buffer to store packets in buffer
// struct timeval tp;
struct timespec tp;
unsigned long rto = RETRY; // rto time initialised with RETRY value
double sampleRTT = 0.0; 
double estimatedRTT = 0.0; 
double devRTT = 0.0; 


FILE* csv;

// end of file indicator
int eof = -1;

int get_max_cwnd(int cwnd_val) {
    return cwnd_val < 2 ? 2 : cwnd_val;
}

void resend_packets(int sig)
{
    ssthresh = get_max_cwnd(cwnd / 2);
    cwnd = 1;
    SLOW_START = 1;
    CONGESTION_AVOIDANCE = 0;
    if (sig == SIGALRM)
    {
        //Resend all packets range between 
        //sendBase and nextSeqNum
        VLOG(INFO, "Timout happend");
        tcp_packet* smallest_seq_pkt = get_head(pktbuffer)->packet;
        if(sendto(sockfd, smallest_seq_pkt, TCP_HDR_SIZE + get_data_size(smallest_seq_pkt), 0, 
                    ( const struct sockaddr *)&serveraddr, serverlen) < 0)
        {
            error("sendto");
        }
    }
}


void start_timer()
{
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}


void stop_timer()
{
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}


/*
 * init_timer: Initialize timer
 * delay: delay in milliseconds
 * sig_handler: signal handler function for re-sending unACKed packets
 */
void init_timer(int delay, void (*sig_handler)(int)) 
{
    signal(SIGALRM, sig_handler);
    timer.it_interval.tv_sec = delay / 1000;    // sets an interval of the timer
    timer.it_interval.tv_usec = (delay % 1000) * 1000;  
    timer.it_value.tv_sec = delay / 1000;       // sets an initial value
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}


int timer_running()
{
    struct itimerval current_timer;
    if (getitimer(ITIMER_REAL, &current_timer) == -1) {
        perror("getitimer");
        exit(1);
    }

    if (current_timer.it_value.tv_sec == 0 && current_timer.it_value.tv_usec == 0) {
        // printf("Timer is not running.\n");
        return 0;
    } else {
        // printf("Timer is running.\n");
        return 1;
    }
}

double get_timestamp(struct timespec tp) {
    double timestamp = tp.tv_sec * 1000000000 + tp.tv_nsec;
    return timestamp / 1000000.0;
}

//reset rto based on rtt value and reset timer
void reset_rto(double rtt_val){
    sampleRTT = rtt_val;
    estimatedRTT = ((1.0 - (double) ALPHA) * estimatedRTT + (double) ALPHA * sampleRTT);
    devRTT = ((1.0 - (double) BETA) * devRTT + (double) BETA * abs(sampleRTT - estimatedRTT));
    rto = MAX(floor(estimatedRTT + 4 * devRTT), 1);

    init_timer(rto, resend_packets);
}

int main (int argc, char **argv)
{
    // linked list to store packets
    pktbuffer = (linked_list*) malloc(sizeof(linked_list));

    int portno, len;
    int next_seqno;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;

    /* check command line arguments */
    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "rb");
    if (fp == NULL) {
        error(argv[3]);
    }

    /* socket: create the socket */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");


    /* initialize server server details */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    /* covert host into network byte order */
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    /* build the server's Internet address */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    csv = fopen("CWND.csv", "w");
    if (csv == NULL) {
		printf("Error opening csv\n");
		return 1;
    }

    //Stop and wait protocol

    init_timer(rto, resend_packets);
    next_seqno = 0;
    int start_byte = next_seqno;

    while (1)
    {   
        int pktbufferlength = get_length(pktbuffer);
        int free_space = cwnd - pktbufferlength;
        // int free_space = cwnd - acked_packets;

        printf("CWND VALUE: %f SSTHRESH: %d\n", cwnd, ssthresh);
        printf("SLOW_START %d, CONGESTION AVOIDANCE %d\n", SLOW_START, CONGESTION_AVOIDANCE);
        // print out initial value
        clock_gettime(CLOCK_MONOTONIC, &tp);
        fprintf(csv, "%f,%f,%d\n", get_timestamp(tp), cwnd, ssthresh);
        
        // new packets are only added when cwnd is greater than packets in the buffer
        for (int i = 0; i < free_space; i++) {

            len = fread(buffer, 1, DATA_SIZE, fp);
            // gettimeofday(&tp, NULL);
            clock_gettime(CLOCK_MONOTONIC, &tp);

            if ( len <= 0)
            {
                if (eof != -1) break;           // to avoid adding the last byte multiple times in subsequent loops
                VLOG(INFO, "End Of File has been reached");
                sndpkt = make_packet(0);
                sndpkt->hdr.seqno = next_seqno;         // this is the last packet
                
                eof = next_seqno;

                for (int i = 0; i < 5; i++) {
                    insert_last(pktbuffer, sndpkt, start_byte, get_timestamp(tp));         // insert the packet into the linked list
                }
                
                break;
            }
            start_byte = next_seqno;
            next_seqno = start_byte + len;
            
            sndpkt = make_packet(len);
            memcpy(sndpkt->data, buffer, len);
            sndpkt->hdr.seqno = start_byte;
            
            insert_last(pktbuffer, sndpkt, start_byte, get_timestamp(tp));         // insert the packet into the linked list
        }

        // printf("CWND VALUE: %f SSTHRESH: %d\n", cwnd, ssthresh);
        // printf("SLOW_START %d, CONGESTION AVOIDANCE %d\n", SLOW_START, CONGESTION_AVOIDANCE);
        print_list(pktbuffer);
        //Wait for ACK
        
        struct node* head = get_head(pktbuffer);
        struct node* current = head;

        // skip already sent pkts in normal scenario
        // on fast retransmit or timeout, cwnd will go down to one

        
        for (int i = 0; i < shift_after_ack; i++) {
            current = current->next;            // causing segmentation fault
            if (current == NULL) break;
        }

        // counter to check that we're only sending cwnd packets
        int counter = shift_after_ack;

        while (current != NULL) {
            if (counter >= cwnd) break;             // break after sending cwnd packets
            tcp_packet* cur_pkt = current->packet;
            int cur_seq = cur_pkt->hdr.seqno;
            VLOG(DEBUG, "Sending packet %d to %s", cur_seq, inet_ntoa(serveraddr.sin_addr));
                
            /*
            * If the sendto is called for the first time, the system will
            * will assign a random port number so that server can send its
            * response to the src port.
            */
                
            // don't send pkts that are already sent
            if(sendto(sockfd, cur_pkt, TCP_HDR_SIZE + get_data_size(cur_pkt), 0, 
                        ( const struct sockaddr *)&serveraddr, serverlen) < 0)
            {
                error("sendto");
            }
                
            if (!timer_running()) start_timer();

            current = current->next;
            counter += 1;
        }
            
        // variable to indicate if buffer window was moved or not
        int move_window = 0;
        int DUP_ACKS = 0;

        // print_list(pktbuffer);
        // this loop waits for the lowest packet to be acked before moving forward
        // this can cause delays in sending packets in delay situations
        // when a packet is acked out of order, we'll have to wait for the lowest pkt to be received although cwnd is increased
        // we can't send another packet unless we get out of the loop
        
        shift_after_ack = 0;
        int old_cwnd = cwnd;
        int acked_pkts = 0;

        do
        {
            // printf("CWND VALUE: %f SSTHRESH: %d\n", cwnd, ssthresh);
            // printf("SLOW_START %d, CONGESTION AVOIDANCE %d\n", SLOW_START, CONGESTION_AVOIDANCE);
            if (DUP_ACKS == 3) {
                DUP_ACKS = 0;
                printf("FAST RETRANSMIT\n");
                resend_packets(SIGALRM);
            }
            if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                        (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
            {
                error("recvfrom");
            }
            
            if (SLOW_START) {
                cwnd += 1;
                if (cwnd >= ssthresh) {
                    SLOW_START = 0;
                    CONGESTION_AVOIDANCE = 1;
                }
            } else if (CONGESTION_AVOIDANCE) {
                cwnd += 1.0 / (int) cwnd;
            }
            // print after every ack when which is when cwnd changes
            clock_gettime(CLOCK_MONOTONIC, &tp);
            fprintf(csv, "%f,%f,%d\n", get_timestamp(tp), cwnd, ssthresh);

            recvpkt = (tcp_packet *)buffer;
            // printf("%d \n", get_data_size(recvpkt));
            printf("Acknowledgement Number: %d, %d\n", recvpkt->hdr.ackno, recvpkt->hdr.seqno);
                
            assert(get_data_size(recvpkt) <= DATA_SIZE);

            int ackno = recvpkt->hdr.ackno;
            if (ackno == eof) return 0;

            if (ackno > send_base) {
                clock_gettime(CLOCK_MONOTONIC, &tp);
                
                send_base = ackno;
                acked_pkts = ack_pkt(pktbuffer, ackno);
                double rtt_val = get_rtt(pktbuffer, recvpkt->hdr.seqno, get_timestamp(tp));
                // rtt_val is zero if rtt was (not?) calculated previously, so that check is required.
                // only print rtt if not zero
                if (rtt_val) {
                    printf("RTT: %f\n", rtt_val);       // use this in formula
                    reset_rto(rtt_val);
                }
                
                

                move_window = slide_acked(pktbuffer);       // slide window if possible

                // pktbuffer->head != NULL: this case is for when the whole buffer gets acked
                if (pktbuffer->head != NULL) start_timer();
            }
            DUP_ACKS += 1;
        }while(!move_window);    //ignore duplicate ACKs

        shift_after_ack = old_cwnd - acked_pkts;
        shift_after_ack = shift_after_ack < 0 ? 0 : shift_after_ack;
        if (shift_after_ack > cwnd) shift_after_ack = 0;        // in scenarios where old cwnd is larger than current cwnd
                                                        // its possible that lot of pkts in the old window were acked
                                                        // so shift_after_ack will be larger than cwnd
                                                        // in such instance, we won't shift any pkt from new window, so value is 0
    }

    return 0;

}



