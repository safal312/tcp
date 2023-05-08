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
#include <math.h>

#include"packet.h"
#include"common.h"
#include"linked_list.h"

#define STDIN_FD    0
#define RETRY  3000 // min rto in millisecond
#define ALPHA 0.125 // for estimatedRTT
#define BETA 0.25 // for devRTT

// state variable for SLOW_START and CONGESTION_AVOIDANCE
int SLOW_START = 1;         
int CONGESTION_AVOIDANCE = 0;

int next_seqno = 0;
int send_base = 0;

// cwnd starts at 1
float cwnd = 1;
int ssthresh = 64;              // initial max ssthresh
int shift_after_ack = 0;      // keep track of number of packets acked from the old congestion window

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;       

linked_list* pktbuffer;             // buffer to store packets in buffer
struct timespec tp;                 // to get current time
double rto = RETRY;                 // rto time initialised with RETRY value
double max_rto = 240 * 1000.0;      // upperbound for rto
double sampleRTT = 0.0; 
double estimatedRTT = 0.0; 
double devRTT = 0.0;
int count_timeouts = 0;             // variable to count timeouts for exp backoff

FILE* csv;                          // exporting csv file

// end of file indicator
int eof = -1;

// hoisting
void init_timer(int delay, void (*sig_handler)(int));
void resend_packets(int sig);
void start_timer();
void stop_timer();
double get_timestamp(struct timespec tp);

// implementing max function for cwnd
int get_max_cwnd(int cwnd_val) {
    return cwnd_val < 2 ? 2 : cwnd_val;
}

// helper function to initialize timer
void reinitialize_timer(double rto) {
    init_timer((int) rto, resend_packets);
    stop_timer();
    start_timer();
}

void resend_packets(int sig)
{
    count_timeouts += 1;
    ssthresh = get_max_cwnd(cwnd / 2);
    cwnd = 1;
    SLOW_START = 1;
    CONGESTION_AVOIDANCE = 0;
    // print after every ack when which is when cwnd changes
    clock_gettime(CLOCK_MONOTONIC, &tp);
    fprintf(csv, "%f,%f,%d\n", get_timestamp(tp) / 1000.0, cwnd, ssthresh);

    // exponential backoff to double rto after two successive timeouts
    if (count_timeouts >= 2) {
        rto = fmin(rto*2, max_rto);
        reinitialize_timer(rto);
    }
    
    if (sig == SIGALRM)
    {
        //Resend all packets range between 
        //sendBase and nextSeqNum
        VLOG(INFO, "Timeout happend");
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
        return 0;
    } else {
        return 1;
    }
}

// get current timestamp in milliseconds
double get_timestamp(struct timespec tp) {
    double timestamp = tp.tv_sec * 1000000000 + tp.tv_nsec;
    return timestamp / 1000000.0;
}

//reset rto based on rtt value and reset timer
void reset_rto(double rtt_val){
    sampleRTT = rtt_val;
    estimatedRTT = ((1.0 - (double) ALPHA) * estimatedRTT + (double) ALPHA * sampleRTT);
    devRTT = ((1.0 - (double) BETA) * devRTT + (double) BETA * abs(sampleRTT - estimatedRTT));
    rto = fmin(estimatedRTT + 4 * devRTT, max_rto);
    reinitialize_timer(rto);
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
    if (inet_aton(hostname, &serveraddr.sin_addr)  == 0) {
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

    // print initial state
    clock_gettime(CLOCK_MONOTONIC, &tp);
    fprintf(csv, "%f,%f,%d\n", get_timestamp(tp) / 1000.0, cwnd, ssthresh);

    init_timer((int) rto, resend_packets);
    next_seqno = 0;
    int start_byte = next_seqno;

    while (1)
    {   
        int pktbufferlength = get_length(pktbuffer);
        int free_space = cwnd - pktbufferlength;
        
        // new packets are only added when cwnd is greater than packets in the buffer
        // pkts won't be added if cwnd is small
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
            
            insert_last(pktbuffer, sndpkt, start_byte, 0);         // insert the packet into the linked list
        }
        
        struct node* head = get_head(pktbuffer);
        struct node* current = head;

        // skip pkts that are already sent from the old cwnd
        for (int i = 0; i < shift_after_ack; i++) {
            current = current->next;
            if (current == NULL) break;     // break if we reach to the end
        }

        // counter to check that we're only sending packets within the cwnd
        // if we don't do this, we may send pkts that are outside of cwnd
        int counter = shift_after_ack;

        while (current != NULL) {
            if (counter >= cwnd) break;             // break after sending cwnd packets
            clock_gettime(CLOCK_MONOTONIC, &tp);

            current->timestamp = get_timestamp(tp);
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
        int DUP_ACKS = 0;           // count duplicate acks
        shift_after_ack = 0;
        int old_cwnd = cwnd;
        int acked_pkts = 0;         // number of acked packets

        // this loop waits for the lowest packet to be acked before moving forward
        // this can cause delays in sending packets in delay situations
        // when a packet is acked out of order, we'll have to wait for the lowest pkt to be received although cwnd is increased
        // we can't send another packet unless we get out of the loop
        do
        {
            // fast retransmit on 3 duplicate acks
            if (DUP_ACKS == 3) {
                DUP_ACKS = 0;
                resend_packets(SIGALRM);
            }
            if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                        (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
            {
                error("recvfrom");
            }

            recvpkt = (tcp_packet *)buffer;
            printf("Acknowledgement Number: %d, %d\n", recvpkt->hdr.ackno, recvpkt->hdr.seqno);
            
            clock_gettime(CLOCK_MONOTONIC, &tp);
            double rtt_val = get_rtt(pktbuffer, recvpkt->hdr.seqno, get_timestamp(tp));

            // rtt_val is set to zero if rtt was calculated previously, so that check is required.
            if (rtt_val) reset_rto(rtt_val);

            assert(get_data_size(recvpkt) <= DATA_SIZE);

            int ackno = recvpkt->hdr.ackno;
            if (ackno == eof) return 0;     // exit if file ended

            if (ackno > send_base) {
                count_timeouts = 0;     // when ackno > send_base, it the receiver got the smallest pkt in buffer, so we reset the timeout counts
                
                send_base = ackno;
                acked_pkts = ack_pkt(pktbuffer, ackno);
                
                move_window = slide_acked(pktbuffer);       // slide window if possible

                if (pktbuffer->head != NULL) start_timer();
            }
            
            // change cwnd based on state
            if (SLOW_START) {
                cwnd += 1;
                if (cwnd >= ssthresh) {
                    SLOW_START = 0;
                    CONGESTION_AVOIDANCE = 1;
                }
            } else if (CONGESTION_AVOIDANCE) {
                // if we receive out of order pkts, we treat as 1 packet, otherwise we treat it normally with acked_pkts/cwnd
                cwnd += acked_pkts == 0 ? 1.0 / cwnd : (float) acked_pkts / (int) cwnd;
            }

            // print after every ack when which is when cwnd changes
            clock_gettime(CLOCK_MONOTONIC, &tp);
            fprintf(csv, "%f,%f,%d\n", get_timestamp(tp) / 1000.0, cwnd, ssthresh);

            DUP_ACKS += 1;
        }while(!move_window);    //ignore duplicate ACKs

        // in scenarios where old cwnd is larger than current cwnd
        // its possible that lot of pkts in the old window were acked
        // so shift_after_ack will be larger than cwnd
        // in such instance, we won't shift any pkt from new window, so value is 0
        shift_after_ack = old_cwnd - acked_pkts;
        shift_after_ack = shift_after_ack < 0 ? 0 : shift_after_ack;
        if (shift_after_ack > cwnd) shift_after_ack = 0;
    }

    return 0;

}



