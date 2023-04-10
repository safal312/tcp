#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "packet.h"
#include "linked_list.h"

struct node* head = NULL;
struct node* tail = NULL;

// void init_list() {
//     struct node* head = (struct node*) malloc(sizeof(struct node));
//     head->key = NULL;
//     head->prev = NULL;

//     struct node* tail = (struct node*) malloc(sizeof(struct node));
//     tail->key = NULL;
//     tail->next = NULL;
//     tail->prev = head;
//     tail->next = NULL;
// }

void insert_last(tcp_packet* packet, int seq_num) {
    struct node* new_node = (struct node*) malloc(sizeof(struct node));
    new_node->packet = packet;
    new_node->key = seq_num;
    new_node->next = NULL;

    if (head == NULL) {
        head = tail = new_node;
        return;
    }

    tail->next = new_node;
    tail = new_node;
}

void remove_first() {
    if (head == NULL) return;
    struct node* temp = head;

    if (head->next==NULL) {
        tail = NULL;
    }
    
    head = head->next;
    free(temp);
}

void print_list() {
    struct node* current = head;

    if (head == NULL) {
        printf("Empty List");
        return;
    }

    while (current != NULL) {
        printf("%d->", current->key);
        current = current->next;
    }
    printf("NULL\n");
}
