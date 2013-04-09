#include <stdio.h>
#include <time.h>

#include "cache.h"


void cache_push(struct cache_item* pFirstItem, struct cache_item* pNewItem)
{
    if (pFirstItem->next_item != NULL)
    {
        pNewItem->next_item = pFirstItem->next_item;
        pFirstItem->next_item = pNewItem;
    }

    pFirstItem->next_item = pNewItem;
}

struct cache_item* cache_pop(struct cache_item* pFirstItem)
{
    if (pFirstItem->next_item == NULL)
    {
        return NULL;
    }
    else
    {
        struct cache_item* pResult = pFirstItem->next_item;

        pFirstItem->next_item = pFirstItem->next_item->next_item;

        return pResult;
    }
}

struct cache_item* cache_search(struct cache_item* pFirstItem, uint32_t IP)
{
    struct cache_item* ptr = pFirstItem->next_item;

    while(ptr != NULL)
    {
        if (ptr->ip == IP)
        {
            return ptr;
        }
        ptr = ptr->next_item;
    }

    return NULL;
}

struct cache_item* cache_create_item(uint32_t ip, unsigned char mac[ETHER_ADDR_LEN], int time_stamp)
{
    struct cache_item* cache_new_item = ((cache_item*)(malloc(sizeof(cache_item))));
    cache_new_item->ip = ip;
    for (int i = 0; i < ETHER_ADDR_LEN; i++)
    {
        cache_new_item->mac[i] = mac[i];
    }
    cache_new_item->time_stamp = time_stamp;
    cache_new_item->next_item = NULL;
    return cache_new_item;
}

void check_cache(struct cache_item* pFirstItem)
{
    time_t time_stamp;
    time(&time_stamp);
    int current_time_stamp = ((int)(time_stamp));

    struct cache_item* ptr = pFirstItem;

    while(ptr != NULL)
    {
        if (ptr->next_item == NULL)
        {
            break;
        }

        if (current_time_stamp - ptr->next_item->time_stamp >= 15)
        {
            in_addr ip_addr;
            ip_addr.s_addr = ptr->next_item->ip;
            Debug("\n\n**** Removing cache entry, [%s, ", inet_ntoa(ip_addr));
            DebugMAC(ptr->next_item->mac);
            Debug("] *****\n\n");

            remove_cache_item(ptr);

        }

        ptr = ptr->next_item;
    }
}

void remove_cache_item(struct cache_item* previous_item)
{
    if (previous_item->next_item->next_item != NULL)
    {
        previous_item->next_item = previous_item->next_item->next_item;
    }
    else
    {
        previous_item->next_item = NULL;
    }

    free(previous_item->next_item);
}
