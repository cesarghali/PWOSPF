/*-----------------------------------------------------------------------------
 * file:  sr_pwospf.h
 * date:  Tue Nov 23 23:21:22 PST 2004 
 * Author: Martin Casado
 *
 * Description:
 *
 *---------------------------------------------------------------------------*/

#ifndef SR_PWOSPF_H
#define SR_PWOSPF_H

#include <pthread.h>
#include "sr_protocol.h"


/* forward declare */
struct sr_instance;

struct pwospf_subsys
{
    /* -- pwospf subsystem state variables here -- */


    /* -- thread and single lock for pwospf subsystem -- */
    pthread_t thread;
    pthread_mutex_t lock;
};

struct powspf_hello_lsu_param
{
    struct sr_instance* sr;
    struct sr_if* interface;
}__attribute__ ((packed));

struct powspf_rx_lsu_param
{
    struct sr_instance* sr;
    uint8_t packet[1500];
    unsigned int length;
    struct sr_if* rx_if;
}__attribute__ ((packed));

struct dijkstra_param
{
    struct sr_instance* sr;
    struct ospfv2_topology_entry* first_topology_entry;
}__attribute__ ((packed));

int pwospf_init(struct sr_instance* sr);


void* send_hellos(void*);
void* send_hello_packet(void*);
void handling_ospfv2_packets(struct sr_instance*, uint8_t*, unsigned int, struct sr_if*);
void handling_ospfv2_hello_packets(struct sr_instance*, uint8_t*, unsigned int, struct sr_if*);
void* handling_ospfv2_lsu_packets(void*);
void* send_lsu(void*);
void* send_all_lsu(void*);
void* run_dijkstra(void*);
void* check_neighbors_life(void*);
void* check_topology_entries_age(void*);
void print_routing_table(struct sr_instance*);


#endif /* SR_PWOSPF_H */
