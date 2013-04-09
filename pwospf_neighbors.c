#include "pwospf_neighbors.h"
#include "pwospf_protocol.h"

void add_neighbor(ospfv2_neighbor* first_neighbor, ospfv2_neighbor* new_neighbor)
{
    if (first_neighbor->next != NULL)
    {
        new_neighbor->next = first_neighbor->next;
        first_neighbor->next = new_neighbor;
    }
    else
    {
        first_neighbor->next = new_neighbor;
    }
}

void delete_neighbor(ospfv2_neighbor* previous_neighbor)
{
    ospfv2_neighbor* temp = previous_neighbor->next;

    if (previous_neighbor->next->next != NULL)
    {
        previous_neighbor->next = previous_neighbor->next->next;
    }
    else
    {
        previous_neighbor->next = NULL;
    }

    free(temp);
}

void check_neighbors_alive(ospfv2_neighbor* first_neighbor)
{
    struct ospfv2_neighbor* ptr = first_neighbor;

    while(ptr != NULL)
    {
        if (ptr->next == NULL)
        {
            break;
        }

        if (ptr->next->alive == 0)
        {
            Debug("\n\n**** PWOSPF: Removing the neighbor, [ID = %s] from the alive neighbors table\n\n", inet_ntoa(ptr->next->neighbor_id));

            delete_neighbor(ptr);
        }
        else
        {
            ptr->next->alive--;
        }

        ptr = ptr->next;
    }
}

void refresh_neighbors_alive(ospfv2_neighbor* first_neighbor, in_addr neighbor_id)
{
    struct ospfv2_neighbor* ptr = first_neighbor;
    while(ptr != NULL)
    {
        if (ptr->neighbor_id.s_addr == neighbor_id.s_addr)
        {
            Debug("-> PWOSPF: Refreshing the neighbor, [ID = %s] in the alive neighbors table\n", inet_ntoa(neighbor_id));
            ptr->alive = OSPF_NEIGHBOR_TIMEOUT;
            return;
        }

        ptr = ptr->next;
    }

    Debug("-> PWOSPF: Adding the neighbor, [ID = %s] to the alive neighbors table\n", inet_ntoa(neighbor_id));
    add_neighbor(first_neighbor, create_ospfv2_neighbor(neighbor_id));
}

struct ospfv2_neighbor* create_ospfv2_neighbor(in_addr neighbor_id)
{
    struct ospfv2_neighbor* new_neighbor = ((ospfv2_neighbor*)(malloc(sizeof(ospfv2_neighbor))));

    new_neighbor->neighbor_id.s_addr = neighbor_id.s_addr;
    new_neighbor->alive = OSPF_NEIGHBOR_TIMEOUT;
    new_neighbor->next = NULL;

    return new_neighbor;
}
