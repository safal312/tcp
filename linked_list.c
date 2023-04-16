#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "packet.h"
#include "linked_list.h"

void insert_seq(linked_list* list, tcp_packet* packet, int seq_num) {
    struct node* new_node = (struct node*) malloc(sizeof(struct node));
    new_node->packet = packet;
    new_node->key = seq_num;
    new_node->ack = 0;
    new_node->next = NULL;

    struct node* current = list->head;

    if (current == NULL) {
        list->head = list->tail = new_node;
        return;
    } 

    // iterate over each node
    // if next node's key value is greater than packet's seq num
    // current node's next should point to the new packet
    // new packet should point to the next node
    
    struct node* prev = current;
    // struct node* current = current->next;
    while (current != NULL) {
        if (current->key == seq_num) return;
        if (current->key > seq_num) {
            new_node->next = current;
            if (current == list->head) {
                list->head = new_node;
                return;
            }
            prev->next = new_node;
            return;
        }
        current = current->next;
        if (current != list->head->next) prev = prev->next;
    }
    
    // if we reach end of the list
    list->tail->next = new_node;
    list->tail = new_node;
}

void insert_last(linked_list* list, tcp_packet* packet, int seq_num) {
    struct node* new_node = (struct node*) malloc(sizeof(struct node));
    new_node->packet = packet;
    new_node->key = seq_num;
    new_node->ack = 0;
    new_node->next = NULL;

    if (list->head == NULL) {
        list->head = list->tail = new_node;
        return;
    }

    list->tail->next = new_node;
    list->tail = new_node;
}

void remove_first(linked_list* list) {
    struct node* head = list->head;

    if (head == NULL) return;
    struct node* temp = head;

    if (head->next==NULL) {
        list->tail = NULL;
    }
    
    list->head = list->head->next;
    free(temp);
}

void print_list(linked_list* list) {
    struct node* head = list->head;
    struct node* current = head;

    if (head == NULL) {
        printf("Empty List\n");
        return;
    }

    while (current != NULL) {
        printf("%d->", current->packet->hdr.seqno);
        current = current->next;
    }
    printf("NULL\n");
}

struct node* get_head(linked_list* list) {
    return list->head;
}

int isEmpty(linked_list* list) {
    return list->head == NULL ? 1 : 0;
}

void ack_pkt(linked_list* list, int ackno) {
    struct node* current = list->head;

    while (current != NULL && current->key != ackno) {
        current->ack = 1;           // 1 means acked. Cumulatively acked
        // if (current->key == ackno) break;
        current = current->next;
    }
}

int slide_acked(linked_list* list) {
    struct node* current = list->head;

    int flag = 0;
    while (current != NULL) {
        if (current->ack != 1) break;
        flag = 1;

        struct node* next_node = current->next;
        free(current);
        
        list->head = next_node;
        current = next_node;
    }
    return flag;
}

int get_length(linked_list* list) {
    struct node* current = list->head;
    int len = 0;
    while (current != NULL) {
        len += 1;
        current = current->next;
    }
    return len;
}

void test_list(linked_list* pktbuffer) {
    insert_seq(pktbuffer, NULL, 0);
    insert_seq(pktbuffer, NULL, -1);
    insert_seq(pktbuffer, NULL, 100);
    insert_seq(pktbuffer, NULL, 200);
    print_list(pktbuffer);
    insert_seq(pktbuffer, NULL, 50);
    print_list(pktbuffer);
    insert_seq(pktbuffer, NULL, -1);
    print_list(pktbuffer);
    insert_seq(pktbuffer, NULL, 300);
    print_list(pktbuffer);
    insert_seq(pktbuffer, NULL, 150);
    print_list(pktbuffer);
    remove_first(pktbuffer);
    remove_first(pktbuffer);
    print_list(pktbuffer);
    remove_first(pktbuffer);
    remove_first(pktbuffer);
    print_list(pktbuffer);
}