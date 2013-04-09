/*-----------------------------------------------------------------------------
 * File: sr_router.h
 * Date: ?
 * Authors: Guido Apenzeller, Martin Casado, Virkam V.
 * Contact: casado@stanford.edu
 *
 *---------------------------------------------------------------------------*/

#ifndef SR_ROUTER_H
#define SR_ROUTER_H

#include <netinet/in.h>
#include <sys/time.h>
#include <stdio.h>

#include "sr_protocol.h"

/* we dont like this debug , but what to do for varargs ? */
#ifdef _DEBUG_
#define Debug(x, args...) printf(x, ## args)
#define DebugMAC(x) \
  do { int ivyl; for(ivyl=0; ivyl<5; ivyl++) printf("%02x:", \
  (unsigned char)(x[ivyl])); printf("%02x",(unsigned char)(x[5])); } while (0)
#else
#define Debug(x, args...) do{}while(0)
#define DebugMAC(x) do{}while(0)
#endif

#define INIT_TTL 255
#define PACKET_DUMP_SIZE 1024

/* forward declare */
struct sr_if;
struct sr_rt;

struct pwospf_subsys;

/* ----------------------------------------------------------------------------
 * struct sr_instance
 *
 * Encapsulation of the state for a single virtual router.
 *
 * -------------------------------------------------------------------------- */

struct sr_instance
{
    int  sockfd;   /* socket to server */
    char user[32]; /* user name */
    char host[32]; /* host name */
    char sr_template[30]; /* template name if any */
    unsigned short topo_id;
    struct sockaddr_in sr_addr; /* address to server */
    struct sr_if* if_list; /* list of interfaces */
    struct sr_rt* routing_table; /* routing table */
    FILE* logfile;
    volatile uint8_t  hw_init; /* bool : hardware has been initialized */

    /* -- pwospf subsystem -- */
    struct pwospf_subsys* ospf_subsys;

    char f_interface[sr_IFACE_NAMELEN];
    int number_of_lsus;
};

/* -- sr_main.c -- */
int sr_verify_routing_table(struct sr_instance* sr);

/* -- sr_vns_comm.c -- */
int sr_send_packet(struct sr_instance* , uint8_t* , unsigned int , const char*);
int sr_connect_to_server(struct sr_instance* ,unsigned short , char* );
int sr_read_from_server(struct sr_instance* );

/* -- sr_router.c -- */
void sr_init(struct sr_instance* );
void sr_handlepacket(struct sr_instance* , uint8_t * , unsigned int , char* );


void* check_cache(void*);
void handle_arp_packet(struct sr_instance*, uint8_t*, unsigned int, struct sr_if*, struct sr_ethernet_hdr*);
void handle_ip_packet(struct sr_instance*, uint8_t*, unsigned int, struct sr_if*, struct sr_ethernet_hdr*);
void send_icmp_error(struct sr_instance*, uint8_t*, unsigned int, struct sr_if*, uint8_t, uint8_t);
void send_arp_request(struct sr_instance*, struct sr_if* rx_if, uint32_t target_ip);
void* sending_arp_request(void*);
void forward_packet(struct sr_instance*, uint8_t*, unsigned int);
short chk_ether_addr(struct sr_ethernet_hdr* rx_e_hdr, struct sr_if* rx_if);
uint32_t get_nex_hop_ip(struct sr_instance*, char*);
short chk_ip_addr(struct ip*, struct sr_instance*);
uint16_t calc_cksum(uint8_t*, int);


/* -- sr_if.c -- */
void sr_add_interface(struct sr_instance* , const char* );
void sr_set_ether_ip(struct sr_instance* , uint32_t );
void sr_set_ether_addr(struct sr_instance* , const unsigned char* );
void sr_print_if_list(struct sr_instance* );

#endif /* SR_ROUTER_H */
