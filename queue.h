#ifndef QUEUE_H
#define QUEUE_H

#include <stdlib.h>

#include "sr_protocol.h"

struct queue_item
{
    uint8_t packet[1500];
    unsigned int length;
    char* interface;
    struct queue_item* next_item;
} __attribute__ ((packed)) ;


void queue_push(struct queue_item*, struct queue_item*);
struct queue_item* queue_pop(struct queue_item*);
struct queue_item* queue_create_item(uint8_t*, unsigned int, char*);
uint8_t queue_is_empty(struct queue_item*);
#endif	//QUEUE_H
