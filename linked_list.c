#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "packet.h"
#include "linked_list.h"

struct node* head = NULL;
struct node* tail = NULL;

void insert_last(tcp_packet* packet, int seq_num) {
    struct node* new_node = (struct node*) malloc(sizeof(struct node));
    new_node->packet = packet;
    new_node->key = seq_num;

    if (tail == NULL) {
        tail = new_node;
    } else {
        tail->next = new_node;
        new_node->prev = tail;
    }
    
    tail = new_node;
}

void remove_first() {
    // struct node* temp = head;

    if (head->next==NULL) {
        tail = NULL;
    } else {
        free(head->next->prev);
        head->next->prev = NULL;
    }
    
    head = head->next;
}

void print_backwards() {
    struct node* current = tail;

    if (tail == NULL) {
        printf("Empty List");
        return;
    }

    while (current != NULL) {
        printf("%d->", current->key);
        current = current->prev;
    }
    printf("NULL\n");
}
