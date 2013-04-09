#include "pwospf_topology.h"
#include "pwospf_protocol.h"

void add_topology_entry(struct ospfv2_topology_entry* first_entry, struct ospfv2_topology_entry* new_entry)
{
    if (first_entry->next != NULL)
    {
        new_entry->next = first_entry->next;
        first_entry->next = new_entry;
    }
    else
    {
        first_entry->next = new_entry;
    }
}

void delete_topology_entry(struct ospfv2_topology_entry* previous_entry)
{
    struct ospfv2_topology_entry* temp = previous_entry->next;

    if (previous_entry->next->next != NULL)
    {
        previous_entry->next = previous_entry->next->next;
    }
    else
    {
        previous_entry->next = NULL;
    }

    free(temp);
}

uint8_t check_topology_age(struct ospfv2_topology_entry* first_entry)
{
    struct ospfv2_topology_entry* ptr = first_entry;

    uint8_t deleted = 0;
    while(ptr != NULL)
    {
        if (ptr->next == NULL)
        {
            break;
        }

        if (ptr->next->age == OSPF_TOPO_ENTRY_TIMEOUT)
        {
            Debug("\n\n**** PWOSPF: Removing a topology entry from the topology table *****\n");
            Debug("        [Network = %s]\n", inet_ntoa(ptr->next->net_num));
            Debug("        [Mask = %s]\n", inet_ntoa(ptr->next->net_mask));
            Debug("        [Neighbor ID = %s]\n", inet_ntoa(ptr->next->neighbor_id));
            Debug("        [Age = %d]\n\n", ptr->next->age);

            delete_topology_entry(ptr);

            deleted = 1;
        }
        else
        {
            ptr->next->age++;
        }

        ptr = ptr->next;
    }

    return deleted;
}

void refresh_topology_entry(struct ospfv2_topology_entry* first_entry, struct in_addr router_id, struct in_addr net_num, struct in_addr net_mask,
    struct in_addr neighbor_id, struct in_addr next_hop, uint16_t sequence_num)
{
    struct ospfv2_topology_entry* ptr = first_entry->next;
    while(ptr != NULL)
    {
        if ((ptr->net_num.s_addr == net_num.s_addr) && (ptr->net_mask.s_addr == net_mask.s_addr))
        {
            if (ptr->router_id.s_addr == router_id.s_addr)
            {
                Debug("-> PWOSPF: Refreshing a topology entry in the toplogy table\n");
                Debug("        [Network = %s]\n", inet_ntoa(ptr->net_num));
                Debug("        [Mask = %s]\n", inet_ntoa(ptr->net_mask));
                Debug("        [Neighbor ID = %s]\n", inet_ntoa(ptr->neighbor_id));

                ptr->age = 0; //OSPF_TOPO_ENTRY_TIMEOUT
                ptr->sequence_num = sequence_num;
                ptr->neighbor_id.s_addr = neighbor_id.s_addr;
                return;
            }
            /* first condition */
            else if ((ptr->neighbor_id.s_addr != 0) && ((ptr->router_id.s_addr != neighbor_id.s_addr) || (ptr->neighbor_id.s_addr != router_id.s_addr)))
            {
                Debug("-> PWOSPF: Droping a topology entry: Invalid entry neighbor\n");
                Debug("        [Network = %s]\n", inet_ntoa(net_num));
                Debug("        [Mask = %s]\n", inet_ntoa(net_mask));
                Debug("        [Neighbor ID = %s]\n", inet_ntoa(neighbor_id));
                return;
            }
            /* second condition */
            else if ((ptr->neighbor_id.s_addr == router_id.s_addr) && (ptr->net_mask.s_addr != net_mask.s_addr))
            {
                Debug("-> PWOSPF: Droping a topology entry: Invalid entry subnet mask\n");
                Debug("        [Network = %s]\n", inet_ntoa(net_num));
                Debug("        [Mask = %s]\n", inet_ntoa(net_mask));
                Debug("        [Neighbor ID = %s]\n", inet_ntoa(neighbor_id));
                return;
            }
        }

        ptr = ptr->next;
    }

    Debug("-> PWOSPF: Adding a topology entry in the toplogy table\n");
    Debug("        [Network = %s]\n", inet_ntoa(net_num));
    Debug("        [Mask = %s]\n", inet_ntoa(net_mask));
    Debug("        [Neighbor ID = %s]\n", inet_ntoa(neighbor_id));
    add_topology_entry(first_entry, create_ospfv2_topology_entry(router_id, net_num, net_mask, neighbor_id, next_hop, sequence_num));
}

struct ospfv2_topology_entry* create_ospfv2_topology_entry(struct in_addr router_id, struct in_addr net_num, struct in_addr net_mask,
    struct in_addr neighbor_id, struct in_addr next_hop, uint16_t sequence_num)
{
    struct ospfv2_topology_entry* new_entry = ((ospfv2_topology_entry*)(malloc(sizeof(ospfv2_topology_entry))));

    new_entry->router_id.s_addr = router_id.s_addr;
    new_entry->net_num.s_addr = net_num.s_addr;
    new_entry->net_mask.s_addr = net_mask.s_addr;
    new_entry->neighbor_id.s_addr = neighbor_id.s_addr;
    new_entry->next_hop.s_addr = next_hop.s_addr;
    new_entry->sequence_num = sequence_num;
    new_entry->age = 0;
    new_entry->next = NULL;

    return new_entry;
}

struct ospfv2_topology_entry* clone_ospfv2_topology_entry(struct ospfv2_topology_entry* entry)
{
    struct ospfv2_topology_entry* copy_entry = ((ospfv2_topology_entry*)(malloc(sizeof(ospfv2_topology_entry))));

    copy_entry->router_id.s_addr = entry->router_id.s_addr;
    copy_entry->net_num.s_addr = entry->net_num.s_addr;
    copy_entry->net_mask.s_addr = entry->net_mask.s_addr;
    copy_entry->neighbor_id.s_addr = entry->neighbor_id.s_addr;
    copy_entry->next_hop.s_addr = entry->next_hop.s_addr;
    copy_entry->sequence_num = entry->sequence_num;
    copy_entry->age = entry->age;
    copy_entry->next = entry->next;

    return copy_entry;
}

void print_topolgy_table(struct ospfv2_topology_entry* first_entry)
{
    //Debug("--------------------------------------------------------------------------------------------------------\n");
    Debug("========================================================================================================\n");
    Debug("%-18s%-18s%-18s%-18s%-18s%-11sAge\n", "Router ID", "Subnet", "Subnet Mask", "Neighbor ID", "Next Hop", "Sequence");
    Debug("%-18s%-18s%-18s%-18s%-18s%-11s---\n", "---------", "------", "-----------", "-----------", "--------", "--------");

    struct ospfv2_topology_entry* entry = first_entry->next;
    if (entry == NULL)
    {
        Debug("The topology table is empty");
    }
    else
    {
        while(entry != NULL)
        {
            Debug("%-18s",inet_ntoa(entry->router_id));
            Debug("%-18s",inet_ntoa(entry->net_num));
            Debug("%-18s",inet_ntoa(entry->net_mask));
            Debug("%-18s",inet_ntoa(entry->neighbor_id));
            Debug("%-18s",inet_ntoa(entry->next_hop));
            Debug("%-11d",entry->sequence_num);
            Debug("%d\n",entry->age);

            entry = entry->next; 
        }
    }
    Debug("========================================================================================================\n");
}

uint8_t search_topolgy_table(struct ospfv2_topology_entry* first_entry, uint32_t subnet)
{
    struct ospfv2_topology_entry* entry = first_entry->next;
    while(entry != NULL)
    {
        if (entry->net_num.s_addr == subnet)
        {
            return 1;
        }

        entry = entry->next;
    }

    return 0;
}
