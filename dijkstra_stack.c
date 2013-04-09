#include <stdio.h>
#include <time.h>

#include "dijkstra_stack.h"


void dijkstra_stack_push(struct dijkstra_item* dijkstra_first_item, struct dijkstra_item* dijkstra_new_item)
{
    if (dijkstra_first_item->next != NULL)
    {
        dijkstra_new_item->next = dijkstra_first_item->next;
        dijkstra_first_item->next = dijkstra_new_item;
    }

    dijkstra_first_item->next = dijkstra_new_item;
}

void dijkstra_stack_reorder(struct dijkstra_item* dijkstra_first_item)
{
    struct dijkstra_item* ptr = dijkstra_first_item;
    while (ptr->next != NULL)
    {
        if (ptr->next->next == NULL)
        {
            break;
        }

        if (ptr->next->cost > ptr->next->next->cost)
        {
            struct dijkstra_item* temp_1 = ptr->next->next->next;
            struct dijkstra_item* temp_2 = ptr->next->next;
            ptr->next->next->next = ptr->next;
            ptr->next->next = temp_1;
            ptr->next = temp_2;
        }

        ptr = ptr->next;
    }
}

struct dijkstra_item* dijkstra_stack_pop(struct dijkstra_item* dijkstra_first_item)
{
    if (dijkstra_first_item->next == NULL)
    {
        return NULL;
    }
    else
    {
        struct dijkstra_item* pResult = dijkstra_first_item->next;

        dijkstra_first_item->next = dijkstra_first_item->next->next;

        return pResult;
    }
}

struct dijkstra_item* create_dikjstra_item(struct ospfv2_topology_entry* new_topology_entry, uint8_t cost)
{
    struct dijkstra_item* dijkstra_new_item = ((dijkstra_item*)(malloc(sizeof(dijkstra_item))));
    dijkstra_new_item->topology_entry = new_topology_entry;
    dijkstra_new_item->cost = cost;
    dijkstra_new_item->parent = NULL;
    dijkstra_new_item->next = NULL;
    return dijkstra_new_item;
}
