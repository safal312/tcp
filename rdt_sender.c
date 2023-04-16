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

#include"packet.h"
#include"common.h"
#include"linked_list.h"

#define STDIN_FD    0
#define RETRY  120 //millisecond

int next_seqno=0;
int send_base=0;
int MAX_WINDOW = 10;       // changed from 1 to 10

int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer; 
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;       

linked_list* pktbuffer;


// struct node* head = NULL;
// struct node* tail = NULL;

void resend_packets(int sig)
{
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
    fp = fopen(argv[3], "r");
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

    //Stop and wait protocol

    init_timer(RETRY, resend_packets);
    next_seqno = 0;
    int start_byte = next_seqno;

    while (1)
    {   
        int pktbufferlength = get_length(pktbuffer);
        int free_space = MAX_WINDOW - pktbufferlength;
        // printf("Next seq no: %d\n", next_seqno);
        for (int i = 0; i < free_space; i++) {

            len = fread(buffer, 1, DATA_SIZE, fp);
            // printf("LEngth: %d\n", len);
            if ( len <= 0)
            {
                VLOG(INFO, "End Of File has been reached");
                sndpkt = make_packet(0);
                sndpkt->hdr.seqno = next_seqno;         // this is the last packet
                
                if (!isEmpty(pktbuffer)){
                    insert_last(pktbuffer, sndpkt, start_byte);         // insert the packet into the linked list
                }
                else {
                    for (int i = 0; i < 5; i++) {
                        sendto(sockfd, sndpkt, TCP_HDR_SIZE,  0,
                            (const struct sockaddr *)&serveraddr, serverlen);
                    }
                    return 0;
                }
                
                break;
            }
            start_byte = next_seqno;
            next_seqno = start_byte + len;
            
            sndpkt = make_packet(len);
            memcpy(sndpkt->data, buffer, len);
            sndpkt->hdr.seqno = start_byte;
            // printf("New pkt: %d, Next Seq", start_byte);
            insert_last(pktbuffer, sndpkt, start_byte);         // insert the packet into the linked list
        }

        //Wait for ACK
        // do {
            struct node* head = get_head(pktbuffer);
            struct node* current = head;

            start_timer();
            // print_list(pktbuffer);

            // skip already sent pkts
            for (int i = 0; i < pktbufferlength; i++) {
                current = current->next;
            }

            while (current != NULL) {
                tcp_packet* cur_pkt = current->packet;
                int cur_seq = cur_pkt->hdr.seqno;
                VLOG(DEBUG, "Sending packet %d to %s", 
                    cur_seq, inet_ntoa(serveraddr.sin_addr));
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
                current = current->next;
            }

            //ssize_t recvfrom(int sockfd, void *buf, size_t len, int flags,
            //struct sockaddr *src_addr, socklen_t *addrlen);
            
            int move_window = 0;

            // recvpkt = (tcp_packet*) malloc(sizeof(tcp_packet));

            do
            {
                if(recvfrom(sockfd, buffer, MSS_SIZE, 0,
                            (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0)
                {
                    error("recvfrom");
                }

                recvpkt = (tcp_packet *)buffer;
                // printf("%d \n", get_data_size(recvpkt));
                printf("Acknowledgement Number: %d\n", recvpkt->hdr.ackno);
                
                assert(get_data_size(recvpkt) <= DATA_SIZE);

                int ackno = recvpkt->hdr.ackno;
                if (ackno > send_base) {
                    send_base = ackno;
                    ack_pkt(pktbuffer, ackno);
                    move_window = slide_acked(pktbuffer);       // slide window if possible

                    // pktbuffer->head != NULL: this case is for when the whole buffer gets acked
                    if (pktbuffer->head != NULL && pktbuffer->head->ack != 1) start_timer();    
                }
            }while(!move_window && recvpkt->hdr.ackno > send_base);    //ignore duplicate ACKs
            // code might be stuck here

            stop_timer();
            
            
            // if (move_window) break;
            /*resend pack if don't recv ACK */
        // } while(recvpkt->hdr.ackno != next_seqno);      

        // free(sndpkt);
    }

    return 0;

}



