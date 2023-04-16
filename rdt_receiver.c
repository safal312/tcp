#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>

#include "common.h"
#include "packet.h"
#include "linked_list.h"

tcp_packet *recvpkt;
tcp_packet *sndpkt;

linked_list* pktbuffer;

int expected_seqno = 0;     // do we need to randomize? (task 2)
int eof = 0;            // eof indicator

int main(int argc, char **argv) {
    linked_list* pktbuffer = (linked_list*) malloc(sizeof(linked_list));
    int sockfd; /* socket */
    int portno; /* port to listen on */
    int clientlen; /* byte size of client's address */
    struct sockaddr_in serveraddr; /* server's addr */
    struct sockaddr_in clientaddr; /* client addr */
    int optval; /* flag value for setsockopt */
    FILE *fp;
    char buffer[MSS_SIZE];
    struct timeval tp;

    /* 
     * check command line arguments 
     */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    fp  = fopen(argv[2], "wb");
    if (fp == NULL) {
        error(argv[2]);
    }

    /* 
     * socket: create the parent socket 
     */
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    /* setsockopt: Handy debugging trick that lets 
     * us rerun the server immediately after we kill it; 
     * otherwise we have to wait about 20 secs. 
     * Eliminates "ERROR on binding: Address already in use" error. 
     */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, 
            (const void *)&optval , sizeof(int));

    /*
     * build the server's Internet address
     */
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    /* 
     * bind: associate the parent socket with a port 
     */
    if (bind(sockfd, (struct sockaddr *) &serveraddr, 
                sizeof(serveraddr)) < 0) 
        error("ERROR on binding");

    /* 
     * main loop: wait for a datagram, then echo it
     */
    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    clientlen = sizeof(clientaddr);
    while (1) {
        /*
         * recvfrom: receive a UDP datagram from a client
         */
        //VLOG(DEBUG, "waiting from server \n");
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0,
                (struct sockaddr *) &clientaddr, (socklen_t *)&clientlen) < 0) {
            error("ERROR in recvfrom");
        }
        recvpkt = (tcp_packet*) malloc(sizeof(buffer));
        memcpy(recvpkt, buffer, sizeof(buffer));
        
        assert(get_data_size(recvpkt) <= DATA_SIZE);

        /* 
         * sendto: ACK back to the client 
         */

        gettimeofday(&tp, NULL);
        VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);
        
        // only insert when header seqno is >= expected
        // this makes sure old, duplicate packets are not inserted
        if (!eof && recvpkt->hdr.seqno >= expected_seqno) {
            insert_seq(pktbuffer, recvpkt, recvpkt->hdr.seqno);
            // print_list(pktbuffer);

            struct node* current = pktbuffer->head;

            while (current != NULL) {
                struct node* next_node = current->next;
                int next_seq = current->key + current->packet->hdr.data_size;

                if (current->key == expected_seqno) {
                    if (current->packet->hdr.data_size == 0) {
                        eof = 1;
                        VLOG(INFO, "End Of File has been reached");
                        fclose(fp);
                        break;
                    }
                    // changed from recvpkt to current
                    fseek(fp, current->packet->hdr.seqno, SEEK_SET);
                    fwrite(current->packet->data, 1, current->packet->hdr.data_size, fp);
                    remove_first(pktbuffer);
                } else {
                    break;
                }
                
                expected_seqno = next_seq;
                current = next_node;
            }
        }

        // printf("exp seq: %d\n",expected_seqno);

        sndpkt = make_packet(0);
        
        sndpkt->hdr.ackno = expected_seqno;
        sndpkt->hdr.ctr_flags = ACK;
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, 
                (struct sockaddr *) &clientaddr, clientlen) < 0) {
            error("ERROR in sendto");
        }
    }

    return 0;
}
