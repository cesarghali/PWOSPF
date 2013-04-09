#ifndef CACHE_H
#define CACHE_H

#include <stdlib.h>

#include "sr_if.h"
#include "sr_router.h"

#include "sr_protocol.h"

struct cache_item
{
    uint32_t ip;
    unsigned char mac[ETHER_ADDR_LEN];
    int time_stamp;
    struct cache_item* next_item;
} __attribute__ ((packed)) ;

void cache_push(struct cache_item*, struct cache_item*);
struct cache_item* cache_pop(struct cache_item*);
struct cache_item* cache_search(struct cache_item*, uint32_t);struct cache_item* cache_create_item(uint32_t ip, unsigned char mac[ETHER_ADDR_LEN], int time_stamp);
void check_cache(struct cache_item*);
void remove_cache_item(struct cache_item*);
#endif	//CACHE_H
