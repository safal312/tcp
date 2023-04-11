#ifndef LINKED_LIST
#define LINKED_LIST

#include "packet.h"

struct node {
    int key;
    int ack;
    tcp_packet* packet;
    struct node* next;
};

typedef struct linked_list {
    struct node* head;
    struct node* tail;
} linked_list;

// extern struct node* head;
// extern struct node* tail;

void insert_seq(linked_list* list, tcp_packet* packet, int seq_num);
void insert_last(linked_list* list, tcp_packet* packet, int seq_num);
void remove_first(linked_list* list);
void print_list(linked_list* list);
struct node* get_head(linked_list* list);
void test_list(linked_list* pktbuffer);
int isEmpty(linked_list* list);
int get_length(linked_list* list);
void ack_pkt(linked_list* list, int ackno);
int slide_acked(linked_list* list);

#endif