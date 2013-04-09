#ifndef DIJKSTRA_STACK_H
#define DIJKSTRA_STACK_H

#include <stdlib.h>

#include "sr_if.h"
#include "sr_router.h"

#include "sr_protocol.h"

struct dijkstra_item
{
    struct ospfv2_topology_entry* topology_entry;
    uint8_t cost;
    struct dijkstra_item* parent;
    struct dijkstra_item* next;
} __attribute__ ((packed)) ;

void dijkstra_stack_push(struct dijkstra_item*, struct dijkstra_item*);
void dijkstra_stack_reorder(struct dijkstra_item*);
struct dijkstra_item* dijkstra_stack_pop(struct dijkstra_item*);
struct dijkstra_item* create_dikjstra_item(struct ospfv2_topology_entry*, uint8_t);
#endif	//DIJKSTRA_STACK_H
