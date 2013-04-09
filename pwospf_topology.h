#ifndef PWOSPF_TOPOLOGY
#define PWOSPF_TOPOLOGY

#ifdef _DARWIN_
#include <sys/types.h>
#endif

#include <netinet/in.h>
#include <stdlib.h>

#include "sr_router.h"


/* ----------------------------------------------------------------------------
 * struct ospfv2_topology_entry
 *
 *
 * -------------------------------------------------------------------------- */

struct ospfv2_topology_entry
{
    struct in_addr router_id;     /* -- router id -- */
    struct in_addr net_num;       /* -- network prefix -- */
    struct in_addr net_mask;      /* -- network mask -- */
    struct in_addr neighbor_id;   /* -- network mask -- */
    struct in_addr next_hop;      /* -- next hop -- */
    uint16_t sequence_num;        /* -- sequence number of the LSU -- */
    int age;                      /* -- LSA age -- */
    struct ospfv2_topology_entry* next;
}__attribute__ ((packed));


void add_topology_entry(struct ospfv2_topology_entry*, struct ospfv2_topology_entry*);
void delete_topology_entry(struct ospfv2_topology_entry*);
uint8_t check_topology_age(struct ospfv2_topology_entry*);
void refresh_topology_entry(struct ospfv2_topology_entry*, struct in_addr, struct in_addr, struct in_addr, struct in_addr, struct in_addr, uint16_t);
struct ospfv2_topology_entry* create_ospfv2_topology_entry(struct in_addr, struct in_addr, struct in_addr, struct in_addr, struct in_addr, uint16_t);
struct ospfv2_topology_entry* clone_ospfv2_topology_entry(struct ospfv2_topology_entry*);
void print_topolgy_table(struct ospfv2_topology_entry*);
uint8_t search_topolgy_table(struct ospfv2_topology_entry*, uint32_t);


#endif  /* --  PWOSPF_TOPOLOGY -- */
