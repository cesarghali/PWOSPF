#include <stdio.h>
#include <string.h>

#include "queue.h"

void queue_push(struct queue_item* pFirstItem, struct queue_item* pNewItem)
{
    if (pFirstItem->next_item != NULL)
    {
        struct queue_item* ptr = pFirstItem->next_item;

        while(ptr->next_item != NULL)
        {
            ptr = ptr->next_item;
        }

        ptr->next_item = pNewItem;
    }
    else
    {
    	pFirstItem->next_item = pNewItem;
    }
}

struct queue_item* queue_pop(struct queue_item* pFirstItem)
{
    if (pFirstItem->next_item == NULL)
    {
        return NULL;
    }
    else
    {
        struct queue_item* pResult = pFirstItem->next_item;

        pFirstItem->next_item = pFirstItem->next_item->next_item;

        return pResult;
    }
}

struct queue_item* queue_create_item(uint8_t* packet, unsigned int length, char* interface)
{
    struct queue_item* queue_new_item = ((queue_item*)(malloc(sizeof(queue_item))));

    memcpy(queue_new_item->packet, packet, length);
    queue_new_item->length = length;
    queue_new_item->interface = interface;
    queue_new_item->next_item = NULL;
    return queue_new_item;
}

uint8_t queue_is_empty(struct queue_item* pFirstItem)
{
    if (pFirstItem->next_item == NULL)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}
