#ifndef LINKED_LIST
#define LINKED_LIST

#include "packet.h"

struct node {
    int key;
    tcp_packet* packet;
    struct node* next;
};

extern struct node* head;
extern struct node* tail;

void insert_last(tcp_packet* packet, int seq_num);
void remove_first();
void print_list();

#endif