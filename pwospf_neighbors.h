#ifndef PWOSPF_NEIGHBORS
#define PWOSPF_NEIGHBORS

#ifdef _DARWIN_
#include <sys/types.h>
#endif

#include <netinet/in.h>
#include <stdlib.h>

#include "sr_router.h"


/* ----------------------------------------------------------------------------
 * struct ospfv2_neighbor
 *
 * Maintains a table of the alive neighbors
 *
 * -------------------------------------------------------------------------- */

struct ospfv2_neighbor
{
    struct in_addr neighbor_id; /* -- the neighbor id -- */
    uint8_t alive;        /* -- alive since [in seconds] -- */
    struct ospfv2_neighbor* next;
}__attribute__ ((packed));


void add_neighbor(struct ospfv2_neighbor*, struct ospfv2_neighbor*);
void delete_neighbor(struct ospfv2_neighbor*);
void check_neighbors_alive(ospfv2_neighbor*);
void refresh_neighbors_alive(ospfv2_neighbor*, in_addr);
struct ospfv2_neighbor* create_ospfv2_neighbor(in_addr);


#endif  /* --  PWOSPF_NEIGHBORS -- */
