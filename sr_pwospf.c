/*-----------------------------------------------------------------------------
 * file: sr_pwospf.c
 * date: Tue Nov 23 23:24:18 PST 2004 
 * Author: Martin Casado
 *
 * Description:
 *
 *---------------------------------------------------------------------------*/

#include "sr_pwospf.h"
#include "sr_router.h"

#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <malloc.h>

#include <string.h>
#include <time.h>
#include <stdlib.h>

#include "sr_protocol.h"
#include "pwospf_protocol.h"
#include "sr_rt.h"
#include "pwospf_neighbors.h"
#include "pwospf_topology.h"
#include "dijkstra_stack.h"
//#include "dijkstra_heap.h"


//pthread_t hello_thread;
pthread_t hello_packet_thread;
pthread_t all_lsu_thread;
pthread_t lsu_thread;
pthread_t neighbors_thread;
pthread_t topology_entries_thread;
pthread_t rx_lsu_thread;
pthread_t dijkstra_thread;

pthread_mutex_t dijkstra_mutex = PTHREAD_MUTEX_INITIALIZER;

in_addr router_id;
uint8_t ospf_multicast_mac[ETHER_ADDR_LEN];
struct ospfv2_neighbor* first_neighbor;
struct ospfv2_topology_entry* first_topology_entry;
uint16_t sequence_num;

struct dijkstra_item* dijkstra_stack;
struct dijkstra_item* dijkstra_heap;
//int dijkstra_heap_empty_item_index;

uint8_t fault_count;
uint8_t int_down;


/* -- declaration of main thread function for pwospf subsystem --- */
static void* pwospf_run_thread(void* arg);

/*---------------------------------------------------------------------
 * Method: pwospf_init(..)
 *
 * Sets up the internal data structures for the pwospf subsystem 
 *
 * You may assume that the interfaces have been created and initialized
 * by this point.
 *---------------------------------------------------------------------*/

int pwospf_init(struct sr_instance* sr)
{
    assert(sr);

    sr->ospf_subsys = (struct pwospf_subsys*)malloc(sizeof(struct
                                                      pwospf_subsys));

    assert(sr->ospf_subsys);
    pthread_mutex_init(&(sr->ospf_subsys->lock), 0);


    /* -- handle subsystem initialization here! -- */
    router_id.s_addr = 0;

    ospf_multicast_mac[0] = 0x01;
    ospf_multicast_mac[1] = 0x00;
    ospf_multicast_mac[2] = 0x5e;
    ospf_multicast_mac[3] = 0x00;
    ospf_multicast_mac[4] = 0x00;
    ospf_multicast_mac[5] = 0x05;

    first_neighbor = NULL;

    sequence_num = 0;


    struct in_addr zero;
    zero.s_addr = 0;
    first_neighbor = create_ospfv2_neighbor(zero);
    first_topology_entry = create_ospfv2_topology_entry(zero, zero, zero, zero, zero, 0);

    fault_count = 0;
    int_down = 0;

    /*dijkstra_heap_init(dijkstra_heap);
    dijkstra_heap_empty_item_index = 0;*/

    /*struct sr_if* int_temp = sr->if_list;
    while(int_temp != NULL)
    {
        int_temp->helloint = OSPF_DEFAULT_HELLOINT;

        if (int_temp->ip > router_id)
        {
            router_id = int_temp->ip;
        }

        int_temp = int_temp->next;
    }*/

    /* -- start thread subsystem -- */
    if( pthread_create(&sr->ospf_subsys->thread, 0, pwospf_run_thread, sr)) { 
        perror("pthread_create");
        assert(0);
    }

    return 0; /* success */
} /* -- pwospf_init -- */


/*---------------------------------------------------------------------
 * Method: pwospf_lock
 *
 * Lock mutex associated with pwospf_subsys
 *
 *---------------------------------------------------------------------*/

void pwospf_lock(struct pwospf_subsys* subsys)
{
    if ( pthread_mutex_lock(&subsys->lock) )
    { assert(0); }
} /* -- pwospf_subsys -- */

/*---------------------------------------------------------------------
 * Method: pwospf_unlock
 *
 * Unlock mutex associated with pwospf subsystem
 *
 *---------------------------------------------------------------------*/

void pwospf_unlock(struct pwospf_subsys* subsys)
{
    if ( pthread_mutex_unlock(&subsys->lock) )
    { assert(0); }
} /* -- pwospf_subsys -- */

/*---------------------------------------------------------------------
 * Method: pwospf_run_thread
 *
 * Main thread of pwospf subsystem. 
 *
 *---------------------------------------------------------------------*/

static
void* pwospf_run_thread(void* arg)
{
    sleep(5);

    struct sr_instance* sr = (struct sr_instance*)arg;

    while(router_id.s_addr == 0)
    {
        struct sr_if* int_temp = sr->if_list;
        while(int_temp != NULL)
        {
            if (int_temp->ip > router_id.s_addr)
            {
                router_id.s_addr = int_temp->ip;
            }

            int_temp = int_temp->next;
        }
    }
    Debug("\n\nPWOSPF: Selecting the highest IP address on a router as the router ID [according to Cisco]\n");
    Debug("-> PWOSPF: The router ID is [%s]\n", inet_ntoa(router_id));


    Debug("\nPWOSPF: Detecting the router interfaces and adding their networks to the routing table\n");
    struct sr_if* int_temp = sr->if_list;
    while(int_temp != NULL)
    {
        struct in_addr ip;
        ip.s_addr = int_temp->ip;
        struct in_addr gw;
        gw.s_addr = 0x00000000;   //inet_aton("0.0.0.0", &gw);
        struct in_addr mask;
        mask.s_addr = htonl(0xfffffffe); //int_temp->mask;
        //inet_aton("255.255.255.254", &mask);
        struct in_addr network;
        network.s_addr = ip.s_addr & mask.s_addr;
       

        if (check_route(sr, network) == 0)
        {
            Debug("-> PWOSPF: Adding the directly connected network [%s, ", inet_ntoa(network));
            Debug("%s] to the routing table\n", inet_ntoa(mask));
            sr_add_rt_entry(sr, network, gw, mask, int_temp->name, 1);
        }
        int_temp = int_temp->next;
    }
    
    Debug("\n-> PWOSPF: Printing the forwarding table\n");
    print_routing_table(sr);


    pthread_create(&hello_packet_thread, NULL, send_hellos, sr);
    pthread_create(&all_lsu_thread, NULL, send_all_lsu, sr);
    pthread_create(&neighbors_thread, NULL, check_neighbors_life, NULL);
    pthread_create(&topology_entries_thread, NULL, check_topology_entries_age, sr);

    return NULL;
} /* -- run_ospf_thread -- */


/*---------------------------------------------------------------------
 * Method: send_hello_packet
 *
 * Constructing and Sending HELLO packets. 
 *
 *---------------------------------------------------------------------*/

void* send_hellos(void* arg)
{
    struct sr_instance* sr = (struct sr_instance*)arg;

    while(1)
    {
        /* -- PWOSPF subsystem functionality should start  here! -- */

        /*pwospf_lock(sr->ospf_subsys);
        printf(" pwospf subsystem sleeping \n");
        pwospf_unlock(sr->ospf_subsys);
        sleep(2);
        printf(" pwospf subsystem awake \n");*/


        usleep(1000000);
        pwospf_lock(sr->ospf_subsys);


        /* Checking all the interfaces for sending HELLO packet */
        struct sr_if* int_temp = sr->if_list;
        while(int_temp != NULL)
        {
            if (int_down == 1)
            {
                if (strcmp(int_temp->name, sr->f_interface) == 0)
                {
                    int_temp = int_temp->next;
                    continue;
                }
            }
            
            if (int_temp->helloint > 0)
            {
                int_temp->helloint--;
            }
            else
            {
                struct powspf_hello_lsu_param* hello_param = ((powspf_hello_lsu_param*)(malloc(sizeof(powspf_hello_lsu_param))));
                hello_param->sr = sr;
                hello_param->interface = int_temp;
                pthread_create(&hello_packet_thread, NULL, send_hello_packet, hello_param);

                int_temp->helloint = OSPF_DEFAULT_HELLOINT;
            }

            int_temp = int_temp->next;
        }

        pwospf_unlock(sr->ospf_subsys);
    };

    return NULL;
} /* -- send_hellos -- */


/*---------------------------------------------------------------------
 * Method: send_hello_packet
 *
 * Constructing and Sending HELLO packets. 
 *
 *---------------------------------------------------------------------*/

void* send_hello_packet(void* arg)
{
    struct powspf_hello_lsu_param* hello_param = ((struct powspf_hello_lsu_param*)(arg));

    Debug("\n\nPWOSPF: Constructing HELLO packet for interface %s: \n", hello_param->interface->name);
    struct sr_ethernet_hdr* tx_e_hdr = ((sr_ethernet_hdr*)(malloc(sizeof(sr_ethernet_hdr))));
    struct ip* tx_ip_hdr = ((ip*)(malloc(sizeof(ip))));
    struct ospfv2_hdr* tx_ospf_hdr = ((ospfv2_hdr*)(malloc(sizeof(ospfv2_hdr))));
    struct ospfv2_hello_hdr* tx_ospf_hello_hdr = ((ospfv2_hello_hdr*)(malloc(sizeof(ospfv2_hello_hdr))));

    int packet_len = sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(ospfv2_hdr) + sizeof(ospfv2_hello_hdr);
    uint8_t* tx_packet;


    /* Destination address */
    for (int i = 0; i < ETHER_ADDR_LEN; i++)
    {
        tx_e_hdr->ether_dhost[i] = ospf_multicast_mac[i];
    }  

    /* Source address */
    for (int i = 0; i < ETHER_ADDR_LEN; i++)
    {
        tx_e_hdr->ether_shost[i] = ((uint8_t)(hello_param->interface->addr[i]));
    }         

    /* Type */
    tx_e_hdr->ether_type = htons(ETHERTYPE_IP);


    /* Version + Header length */
    tx_ip_hdr->ip_v = 4;
    tx_ip_hdr->ip_hl = 5;

    /* DS */
    tx_ip_hdr->ip_tos = 0;

    /* Total length */
    tx_ip_hdr->ip_len = htons((sizeof(ip)) + sizeof(ospfv2_hdr) + sizeof(ospfv2_hello_hdr));

    /* Identification */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand(tv.tv_sec * tv.tv_usec);
    tx_ip_hdr->ip_id = rand();

    /* Fragment */
    tx_ip_hdr->ip_off = htons(IP_NO_FRAGMENT);

    /* TTL */
    tx_ip_hdr->ip_ttl = 64;

    /* Protocol */
    tx_ip_hdr->ip_p = IP_PROTO_OSPFv2;  // which is 89 = OSPFv2

    /* Checksum */
    tx_ip_hdr->ip_sum = 0;

    /* Source IP address */
    tx_ip_hdr->ip_src.s_addr = hello_param->interface->ip;

    /* Destination IP address */
    tx_ip_hdr->ip_dst.s_addr = htonl(OSPF_AllSPFRouters);

    /* Re-Calculate checksum of the IP header */
    tx_ip_hdr->ip_sum = calc_cksum(((uint8_t*)(tx_ip_hdr)), sizeof(ip));


    /* OSPFv2 Version */
    tx_ospf_hdr->version = OSPF_V2;

    /* OSPFv2 Type */
    tx_ospf_hdr->type = OSPF_TYPE_HELLO;

    /* Packet Length */
    tx_ospf_hdr->len = htons(sizeof(ospfv2_hdr) + sizeof(ospfv2_hello_hdr));

    /* Router ID */
    tx_ospf_hdr->rid = router_id.s_addr;    //It is the highest IP address on a router [according to Cisco]

    /* Area ID */
    tx_ospf_hdr->aid = htonl(171); //((uint8_t)(hello_param->interface->ip));    //Since we only have one Area which is Area0

    /* Checksum */
    tx_ospf_hdr->csum = 0;

    /* Authentication Type */
    tx_ospf_hdr->autype = 0;

    /* Authentication Data */
    tx_ospf_hdr->audata = 0;


    /* Network Mask */
    tx_ospf_hello_hdr->nmask = htonl(0xfffffffe); //Suppose to get it from the interface.

    /* Hello Interval */
    tx_ospf_hello_hdr->helloint = htons(OSPF_DEFAULT_HELLOINT);

    /* Padding */
    tx_ospf_hello_hdr->padding = 0;


    /***** Creating the transmitted packet *****/
    tx_packet = ((uint8_t*)(malloc(packet_len)));

    memcpy(tx_packet, tx_e_hdr, sizeof(sr_ethernet_hdr));
    memcpy(tx_packet + sizeof(sr_ethernet_hdr), tx_ip_hdr, sizeof(ip));
    memcpy(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip), tx_ospf_hdr, sizeof(ospfv2_hdr));
    memcpy(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(ospfv2_hdr), tx_ospf_hello_hdr, sizeof(ospfv2_hello_hdr));

    /* Re-Calculate checksum of the OSPFv2 header */
    /* Updating the new checksum in tx_packet */
    ((ospfv2_hdr*)(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip)))->csum =
        calc_cksum(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip), sizeof(ospfv2_hdr) + sizeof(ospfv2_hello_hdr));

    Debug("-> PWOSPF: Sending HELLO Packet of length = %d, out of the interface: %s\n", packet_len, hello_param->interface->name);
    sr_send_packet(hello_param->sr, ((uint8_t*)(tx_packet)), packet_len, hello_param->interface->name);


    return NULL;
} /* -- send_hello_packet -- */


/*---------------------------------------------------------------------
 * Method: handling_ospfv2_packets
 *
 * Handling OSPFv2 packets
 *
 *---------------------------------------------------------------------*/

void handling_ospfv2_packets(struct sr_instance* sr, uint8_t* packet, unsigned int length, struct sr_if* rx_if)
{
    struct ospfv2_hdr* rx_ospfv2_hdr = ((struct ospfv2_hdr*)(packet + sizeof(sr_ethernet_hdr) + sizeof(ip)));

    switch(rx_ospfv2_hdr->type)
    {
        case OSPF_TYPE_HELLO:
            handling_ospfv2_hello_packets(sr, packet, length, rx_if);
            break;

        case OSPF_TYPE_LSU:
            struct powspf_rx_lsu_param* rx_lsu_param = ((powspf_rx_lsu_param*)(malloc(sizeof(powspf_rx_lsu_param))));
            rx_lsu_param->sr = sr;
            for (unsigned int i = 0; i < length; i++)
            {
                rx_lsu_param->packet[i] = packet[i];
            }
            rx_lsu_param->length = length;
            rx_lsu_param->rx_if = rx_if;
            pthread_create(&rx_lsu_thread, NULL, handling_ospfv2_lsu_packets, rx_lsu_param);
            break;
    }
} /* -- handling_ospfv2_packets -- */


/*---------------------------------------------------------------------
 * Method: handling_ospfv2_hello_packets
 *
 * Handling the received HELLO packets 
 *
 *---------------------------------------------------------------------*/

void handling_ospfv2_hello_packets(struct sr_instance* sr, uint8_t* packet, unsigned int length, struct sr_if* rx_if)
{
    struct ip* rx_ip_hdr = ((ip*)(packet + sizeof(sr_ethernet_hdr)));
    struct ospfv2_hdr* rx_ospfv2_hdr = ((struct ospfv2_hdr*)(packet + sizeof(sr_ethernet_hdr) + sizeof(ip)));
    struct ospfv2_hello_hdr* rx_ospfv2_hello_hdr = ((struct ospfv2_hello_hdr*)(packet + sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(ospfv2_hdr)));

    struct in_addr neighbor_id;
    neighbor_id.s_addr = rx_ospfv2_hdr->rid;
    struct in_addr net_mask;
    net_mask.s_addr = rx_ospfv2_hello_hdr->nmask;
    Debug("-> PWOSPF: Detecting PWOSPF HELLO Packet from:\n");
    Debug("      [Neighbor ID = %s]\n", inet_ntoa(neighbor_id));
    Debug("      [Neighbor IP = %s]\n", inet_ntoa(rx_ip_hdr->ip_src));
    Debug("      [Network Mask = %s]\n", inet_ntoa(net_mask));

    /* Checking checksum */
    uint16_t rx_checksum = rx_ospfv2_hdr->csum;
    rx_ospfv2_hdr->csum = 0;
    uint16_t calc_checksum = calc_cksum(packet + sizeof(sr_ethernet_hdr) + sizeof(ip), sizeof(ospfv2_hdr) + sizeof(ospfv2_hello_hdr));
    if (calc_checksum != rx_checksum)
    {
        Debug("-> PWOSPF: HELLO Packet dropped, invalid checksum\n");
        return;
    }
    rx_ospfv2_hdr->csum = rx_checksum;

    if (rx_ospfv2_hello_hdr->nmask != rx_if->mask)
    {
        Debug("-> PWOSPF: HELLO Packet dropped, invalid hello network mask\n");
        return;
    }

    if (rx_ospfv2_hello_hdr->helloint != htons(OSPF_DEFAULT_HELLOINT))
    {
        Debug("-> PWOSPF: HELLO Packet dropped, invalid hello interval\n");
        return;
    }

    /* Set the neighbor id and ip of the interface at which the HELLO packet is received */
    int new_neighbor = 0;
    if (rx_if->neighbor_id != rx_ospfv2_hdr->rid)
    {
        rx_if->neighbor_id = rx_ospfv2_hdr->rid;
        new_neighbor = 1;
    }
    rx_if->neighbor_ip = rx_ip_hdr->ip_src.s_addr;

    refresh_neighbors_alive(first_neighbor, neighbor_id);

    if (new_neighbor == 1)
    {
        struct powspf_hello_lsu_param* lsu_param = ((powspf_hello_lsu_param*)(malloc(sizeof(powspf_hello_lsu_param))));
        lsu_param->sr = sr;
        lsu_param->interface = rx_if;
        pthread_create(&lsu_thread, NULL, send_lsu, lsu_param);
    }
} /* -- handling_ospfv2_hello_packets -- */


/*---------------------------------------------------------------------
 * Method: handling_ospfv2_lsu_packets
 *
 * Handling the received LSU packets 
 *
 *---------------------------------------------------------------------*/

void* handling_ospfv2_lsu_packets(void* arg)
{
    struct powspf_rx_lsu_param* rx_lsu_param = ((struct powspf_rx_lsu_param*)(arg));

    struct ip* rx_ip_hdr = ((struct ip*)(rx_lsu_param->packet + sizeof(sr_ethernet_hdr)));
    struct ospfv2_hdr* rx_ospfv2_hdr = ((struct ospfv2_hdr*)(rx_lsu_param->packet + sizeof(sr_ethernet_hdr) + sizeof(ip)));
    struct ospfv2_lsu_hdr* rx_ospfv2_lsu_hdr = ((struct ospfv2_lsu_hdr*)(rx_lsu_param->packet + sizeof(sr_ethernet_hdr) + sizeof(ip) +
        sizeof(ospfv2_hdr)));
    struct ospfv2_lsa* rx_ospfv2_lsa;

    struct in_addr neighbor_id;
    neighbor_id.s_addr = rx_ospfv2_hdr->rid;
    Debug("-> PWOSPF: Detecting LSU Packet from [Neighbor ID = %s]\n", inet_ntoa(neighbor_id));

    /* Check the Router ID */
    if (rx_ospfv2_hdr->rid == router_id.s_addr)
    {
        Debug("-> PWOSPF: LSU Packet dropped, originated by this router\n");
        return NULL;        
    }

    /* Checking checksum */
    uint16_t rx_checksum = rx_ospfv2_hdr->csum;
    rx_ospfv2_hdr->csum = 0;
    uint16_t calc_checksum = calc_cksum(rx_lsu_param->packet + sizeof(sr_ethernet_hdr) + sizeof(ip), htons(rx_ospfv2_hdr->len));
    if (calc_checksum != rx_checksum)
    {
        Debug("-> PWOSPF: LSU Packet dropped, invalid checksum\n");
        return NULL;
    }
    rx_ospfv2_hdr->csum = rx_checksum;


    for (unsigned int i = 0; i < htonl(rx_ospfv2_lsu_hdr->num_adv); i++)
    {
        rx_ospfv2_lsa = ((struct ospfv2_lsa*)(rx_lsu_param->packet + sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(ospfv2_hdr) +
            sizeof(ospfv2_lsu_hdr) + (sizeof(ospfv2_lsa) * i)));

        struct in_addr router_id;
        router_id.s_addr = rx_ospfv2_hdr->rid;
        struct in_addr net_num;
        net_num.s_addr = rx_ospfv2_lsa->subnet;
        struct in_addr net_mask;
        net_mask.s_addr = rx_ospfv2_lsa->mask;
        struct in_addr neighbor_id;
        neighbor_id.s_addr = rx_ospfv2_lsa->rid;
        refresh_topology_entry(first_topology_entry, router_id, net_num, net_mask, neighbor_id, rx_ip_hdr->ip_src, htons(rx_ospfv2_lsu_hdr->seq));
    }

    Debug("\n-> PWOSPF: Printing the topology table\n");
    print_topolgy_table(first_topology_entry);


    /* Running Dijkstra thread */
    Debug("\n-> PWOSPF: Running the Dijkstra algorithm\n\n");
    struct dijkstra_param* dij_param = ((dijkstra_param*)(malloc(sizeof(dijkstra_param))));
    dij_param->sr = rx_lsu_param->sr;
    dij_param->first_topology_entry = first_topology_entry;
    pthread_create(&dijkstra_thread, NULL, run_dijkstra, dij_param);


    /* Flooding the LSU packet */
    struct sr_if* temp_int = rx_lsu_param->sr->if_list;
    while (temp_int != NULL)
    {
        if ((strcmp(temp_int->name, rx_lsu_param->rx_if->name) != 0))
        {
            /* Ehternet Source address */
            for (int i = 0; i < ETHER_ADDR_LEN; i++)
            {
                ((sr_ethernet_hdr*)(rx_lsu_param->packet))->ether_shost[i] = ((uint8_t)(temp_int->addr[i]));
            }


            /* IP Identification */
            struct timeval tv;
            gettimeofday(&tv, NULL);
            srand(tv.tv_sec * tv.tv_usec);
            ((ip*)(rx_lsu_param->packet + sizeof(sr_ethernet_hdr)))->ip_id = rand();
    
            /* IP Checksum */
            ((ip*)(rx_lsu_param->packet + sizeof(sr_ethernet_hdr)))->ip_sum = 0;

            /* Source IP address */
            ((ip*)(rx_lsu_param->packet + sizeof(sr_ethernet_hdr)))->ip_src.s_addr = temp_int->ip;

            /* Re-Calculate checksum of the IP header */
            ((ip*)(rx_lsu_param->packet + sizeof(sr_ethernet_hdr)))->ip_sum = calc_cksum(((uint8_t*)(rx_lsu_param->packet + sizeof(sr_ethernet_hdr))), 
                sizeof(ip));


            /* OSPF Checksum */
            ((ospfv2_hdr*)(rx_lsu_param->packet + sizeof(sr_ethernet_hdr) + sizeof(ip)))->csum = 0;


            /* LSU TTL */
            ((ospfv2_lsu_hdr*)(rx_lsu_param->packet + sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(ospfv2_hdr)))->ttl--;

            /* Re-Calculate checksum of the LSU header */
            /* Updating the new checksum in tx_packet */
            ((ospfv2_hdr*)(rx_lsu_param->packet + sizeof(sr_ethernet_hdr) + sizeof(ip)))->csum =
                calc_cksum(rx_lsu_param->packet + sizeof(sr_ethernet_hdr) + sizeof(ip), htons(((ospfv2_hdr*)(rx_lsu_param->packet +
                sizeof(sr_ethernet_hdr) + sizeof(ip)))->len));

            Debug("-> PWOSPF: Flooding LSU Update of length = %d, out of the interface: %s\n", rx_lsu_param->length, temp_int->name);
            sr_send_packet(rx_lsu_param->sr, ((uint8_t*)(rx_lsu_param->packet)), rx_lsu_param->length, temp_int->name);
        }

        temp_int = temp_int->next;
    }

    return NULL;
} /* -- handling_ospfv2_lsu_packets -- */


/*---------------------------------------------------------------------
 * Method: send_lsu
 *
 * Constructing and Sending LSU out of a specific interface
 *
 *---------------------------------------------------------------------*/

void* send_lsu(void* arg)
{
    struct powspf_hello_lsu_param* lsu_param = ((struct powspf_hello_lsu_param*)(arg));

    if (lsu_param->interface->neighbor_ip == 0)
    {
        return NULL;
    }

    /* Constructing LSU */
    Debug("\n\nPWOSPF: Constructing LSU packet\n");
    struct sr_ethernet_hdr* tx_e_hdr = ((sr_ethernet_hdr*)(malloc(sizeof(sr_ethernet_hdr))));
    struct ip* tx_ip_hdr = ((ip*)(malloc(sizeof(ip))));
    struct ospfv2_hdr* tx_ospf_hdr = ((ospfv2_hdr*)(malloc(sizeof(ospfv2_hdr))));
    struct ospfv2_lsu_hdr* tx_ospf_lsu_hdr = ((ospfv2_lsu_hdr*)(malloc(sizeof(ospfv2_lsu_hdr))));
    struct ospfv2_lsa* tx_ospf_lsa = ((ospfv2_lsa*)(malloc(sizeof(ospfv2_lsa))));

    int routes_num = count_routes(lsu_param->sr, 0);
    int packet_len = sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(ospfv2_hdr) + sizeof(ospfv2_lsu_hdr) + (sizeof(ospfv2_lsa) * routes_num);
    uint8_t* tx_packet;


    /* Destination address */
    for (int i = 0; i < ETHER_ADDR_LEN; i++)
    {
        tx_e_hdr->ether_dhost[i] = ospf_multicast_mac[i];
    }  

    /* Source address */
    for (int i = 0; i < ETHER_ADDR_LEN; i++)
    {
        tx_e_hdr->ether_shost[i] = ((uint8_t)(lsu_param->interface->addr[i]));
    }         

    /* Type */
    tx_e_hdr->ether_type = htons(ETHERTYPE_IP);


    /* Version + Header length */
    tx_ip_hdr->ip_v = 4;
    tx_ip_hdr->ip_hl = 5;

    /* DS */
    tx_ip_hdr->ip_tos = 0;

    /* Total length */
    tx_ip_hdr->ip_len = htons(sizeof(ip) + sizeof(ospfv2_hdr) + sizeof(ospfv2_lsu_hdr) + (sizeof(ospfv2_lsa) * routes_num));

    /* Identification */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand(tv.tv_sec * tv.tv_usec);
    tx_ip_hdr->ip_id = rand();

    /* Fragment */
    tx_ip_hdr->ip_off = htons(IP_NO_FRAGMENT);

    /* TTL */
    tx_ip_hdr->ip_ttl = 64;

    /* Protocol */
    tx_ip_hdr->ip_p = IP_PROTO_OSPFv2;  // which is 89 = OSPFv2

    /* Checksum */
    tx_ip_hdr->ip_sum = 0;

    /* Source IP address */
    tx_ip_hdr->ip_src.s_addr = lsu_param->interface->ip;

    /* Destination IP address */
    tx_ip_hdr->ip_dst.s_addr = htonl(OSPF_AllSPFRouters);

    /* Re-Calculate checksum of the IP header */
    tx_ip_hdr->ip_sum = calc_cksum(((uint8_t*)(tx_ip_hdr)), sizeof(ip));


    /* OSPFv2 Version */
    tx_ospf_hdr->version = OSPF_V2;

    /* OSPFv2 Type */
    tx_ospf_hdr->type = OSPF_TYPE_LSU;

    /* Packet Length */
    tx_ospf_hdr->len = htons(sizeof(ospfv2_hdr) + sizeof(ospfv2_lsu_hdr) + (sizeof(ospfv2_lsa) * routes_num));

    /* Router ID */
    tx_ospf_hdr->rid = router_id.s_addr;    //It is the highest IP address on a router [according to Cisco]

    /* Area ID */
    tx_ospf_hdr->aid = htonl(171); //((uint8_t)(lsu_param->interface->ip));    //Since we only have one Area which is Area0

    /* Checksum */
    tx_ospf_hdr->csum = 0;

    /* Authentication Type */
    tx_ospf_hdr->autype = 0;

    /* Authentication Data */
    tx_ospf_hdr->audata = 0;


    /* Sequence */
    sequence_num++;
    tx_ospf_lsu_hdr->seq = htons(sequence_num);

    /* Unused */
    tx_ospf_lsu_hdr->unused = 0;

    /* TTL */
    tx_ospf_lsu_hdr->ttl = 64;

    /* Number of advertisememts */
    tx_ospf_lsu_hdr->num_adv = htonl(routes_num);


    /***** Creating the transmitted packet *****/
    tx_packet = ((uint8_t*)(malloc(packet_len)));

    memcpy(tx_packet, tx_e_hdr, sizeof(sr_ethernet_hdr));
    memcpy(tx_packet + sizeof(sr_ethernet_hdr), tx_ip_hdr, sizeof(ip));
    memcpy(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip), tx_ospf_hdr, sizeof(ospfv2_hdr));
    memcpy(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(ospfv2_hdr), tx_ospf_lsu_hdr, sizeof(ospfv2_lsu_hdr));

    int i = 0;
    struct sr_rt* entry = lsu_param->sr->routing_table;
    while (entry != NULL)
    {
        if (entry->admin_dst <= 1)
        {
            /* Subnet */
            tx_ospf_lsa->subnet = entry->dest.s_addr;

            /* Mask */
            tx_ospf_lsa->mask = entry->mask.s_addr;

            /* Router ID */
            tx_ospf_lsa->rid = sr_get_interface(lsu_param->sr, entry->interface)->neighbor_id; //lsu_param->interface->neighbor_id;

            memcpy(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(ospfv2_hdr) + sizeof(ospfv2_lsu_hdr) + (sizeof(ospfv2_lsa) * i),
                tx_ospf_lsa, sizeof(ospfv2_lsa));

            i++;
        }

        entry = entry->next;
    }

    /* Re-Calculate checksum of the LSU header */
    /* Updating the new checksum in tx_packet */
    ((ospfv2_hdr*)(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip)))->csum =
        calc_cksum(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip), sizeof(ospfv2_hdr) + sizeof(ospfv2_lsu_hdr) + (sizeof(ospfv2_lsa) * routes_num));


    Debug("-> PWOSPF: Sending LSU Packet of length = %d, out of the interface: %s\n", packet_len, lsu_param->interface->name);
    sr_send_packet(lsu_param->sr, ((uint8_t*)(tx_packet)), packet_len, lsu_param->interface->name);

    
    free(tx_packet);


    /* Constructing LSU Update */
    Debug("\n\nPWOSPF: Constructing LSU update\n");

    /***** Creating the transmitted packet *****/
    packet_len = sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(ospfv2_hdr) + sizeof(ospfv2_lsu_hdr) + sizeof(ospfv2_lsa);
    tx_packet = ((uint8_t*)(malloc(packet_len)));
    
    /* LSA Update */
    /* Subnet */
    tx_ospf_lsa->subnet = lsu_param->interface->ip & htonl(0xfffffffe);

    /* Mask */
    tx_ospf_lsa->mask = htonl(0xfffffffe);

    /* Router ID */
    tx_ospf_lsa->rid = sr_get_interface(lsu_param->sr, lsu_param->interface->name)->neighbor_id;


    struct sr_if* temp_int = lsu_param->sr->if_list;
    while (temp_int != NULL)
    {
        if ((strcmp(temp_int->name, lsu_param->interface->name) != 0) /*&& (temp_int->neighbor_id != 0)*/)
        {
            /* Ehternet Source address */
            for (int i = 0; i < ETHER_ADDR_LEN; i++)
            {
                tx_e_hdr->ether_shost[i] = ((uint8_t)(temp_int->addr[i]));
            }

            
            /* IP Total length */
            tx_ip_hdr->ip_len = htons(sizeof(ip) + sizeof(ospfv2_hdr) + sizeof(ospfv2_lsu_hdr) + sizeof(ospfv2_lsa));

            /* IP Identification */
            struct timeval tv;
            gettimeofday(&tv, NULL);
            srand(tv.tv_sec * tv.tv_usec);
            tx_ip_hdr->ip_id = rand();
    
            /* IP Checksum */
            tx_ip_hdr->ip_sum = 0;

            /* Source IP address */
            tx_ip_hdr->ip_src.s_addr = temp_int->ip;

            /* Re-Calculate checksum of the IP header */
            tx_ip_hdr->ip_sum = calc_cksum(((uint8_t*)(tx_ip_hdr)), sizeof(ip));


            /* OSPF Packet Length */
            tx_ospf_hdr->len = htons(sizeof(ospfv2_hdr) + sizeof(ospfv2_lsu_hdr) + sizeof(ospfv2_lsa));

            /* OSPF Checksum */
            tx_ospf_hdr->csum = 0;


            /* LSU Sequence */
            sequence_num++;
            tx_ospf_lsu_hdr->seq = htons(sequence_num);

            /* LSU Number of advertisememts */
            tx_ospf_lsu_hdr->num_adv = htonl(1);


            memcpy(tx_packet, tx_e_hdr, sizeof(sr_ethernet_hdr));
            memcpy(tx_packet + sizeof(sr_ethernet_hdr), tx_ip_hdr, sizeof(ip));
            memcpy(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip), tx_ospf_hdr, sizeof(ospfv2_hdr));
            memcpy(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(ospfv2_hdr), tx_ospf_lsu_hdr, sizeof(ospfv2_lsu_hdr));
            memcpy(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(ospfv2_hdr) + sizeof(ospfv2_lsu_hdr), tx_ospf_lsa, sizeof(ospfv2_lsa));


            /* Re-Calculate checksum of the LSU header */
            /* Updating the new checksum in tx_packet */
            ((ospfv2_hdr*)(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip)))->csum =
                calc_cksum(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip), sizeof(ospfv2_hdr) + sizeof(ospfv2_lsu_hdr) + sizeof(ospfv2_lsa));

            Debug("-> PWOSPF: Sending LSU Update of length = %d, out of the interface: %s\n", packet_len, temp_int->name);
            sr_send_packet(lsu_param->sr, ((uint8_t*)(tx_packet)), packet_len, temp_int->name);
        }

        temp_int = temp_int->next;
    }

    free(tx_packet);
    free(tx_ospf_lsa);
    free(tx_ospf_lsu_hdr);
    free(tx_ospf_hdr);
    free(tx_ip_hdr);
    free(tx_e_hdr);

    return NULL;
} /* -- send_lsu -- */


/*---------------------------------------------------------------------
 * Method: send_all_lsu
 *
 * Constructing and Sending LSUs every 30 seconds 
 *
 *---------------------------------------------------------------------*/

void* send_all_lsu(void* arg)
{
    struct sr_instance* sr = (struct sr_instance*)arg;

    while(1)
    {
        usleep(OSPF_DEFAULT_LSUINT * 1000000);

        pwospf_lock(sr->ospf_subsys);


        if (strcmp(sr->f_interface, "no\0") != 0)
        {
            fault_count++;
            if (fault_count % sr->number_of_lsus == 0)
            {
                fault_count = 0;
                if (int_down == 0)
                {
                    int_down = 1;
                    Debug("\n\n**************************************\n");
                    Debug("***** Interface %s is now down *****\n", sr->f_interface);
                    Debug("**************************************\n");
                }
                else if (int_down == 1)
                {
                    int_down = 0;
                    Debug("\n\n************************************\n");
                    Debug("***** Interface %s is now up *****\n", sr->f_interface);
                    Debug("************************************\n");
                }
            }
        }


        /* Constructing LSU */
        Debug("\n\nPWOSPF: Constructing LSU packet\n");
        struct sr_ethernet_hdr* tx_e_hdr = ((sr_ethernet_hdr*)(malloc(sizeof(sr_ethernet_hdr))));
        struct ip* tx_ip_hdr = ((ip*)(malloc(sizeof(ip))));
        struct ospfv2_hdr* tx_ospf_hdr = ((ospfv2_hdr*)(malloc(sizeof(ospfv2_hdr))));
        struct ospfv2_lsu_hdr* tx_ospf_lsu_hdr = ((ospfv2_lsu_hdr*)(malloc(sizeof(ospfv2_lsu_hdr))));
        struct ospfv2_lsa* tx_ospf_lsa = ((ospfv2_lsa*)(malloc(sizeof(ospfv2_lsa))));

        int routes_num;
        routes_num = count_routes(sr, int_down);
//printf("*********************************************************************** %d\n", routes_num);
        int packet_len = sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(ospfv2_hdr) + sizeof(ospfv2_lsu_hdr) + (sizeof(ospfv2_lsa) * routes_num);
        uint8_t* tx_packet;


        /* Destination address */
        for (int i = 0; i < ETHER_ADDR_LEN; i++)
        {
            tx_e_hdr->ether_dhost[i] = ospf_multicast_mac[i];
        }  

        /* Source address */
        /* Later in this function, depending on the interface */

        /* Type */
        tx_e_hdr->ether_type = htons(ETHERTYPE_IP);


        /* Version + Header length */
        tx_ip_hdr->ip_v = 4;
        tx_ip_hdr->ip_hl = 5;

        /* DS */
        tx_ip_hdr->ip_tos = 0;

        /* Total length */
        tx_ip_hdr->ip_len = htons(sizeof(ip) + sizeof(ospfv2_hdr) + sizeof(ospfv2_lsu_hdr) + (sizeof(ospfv2_lsa) * routes_num));

        /* Identification */
        /* Later in this function */

        /* Fragment */
        tx_ip_hdr->ip_off = htons(IP_NO_FRAGMENT);

        /* TTL */
        tx_ip_hdr->ip_ttl = 64;

        /* Protocol */
        tx_ip_hdr->ip_p = IP_PROTO_OSPFv2;  // which is 89 = OSPFv2

        /* Checksum */
        /* Later in this function */

        /* Source IP address */
        /* Later in this function */;

        /* Destination IP address */
        tx_ip_hdr->ip_dst.s_addr = htonl(OSPF_AllSPFRouters);

        /* Re-Calculate checksum of the IP header */
        /* Later in this function */;


        /* OSPFv2 Version */
        tx_ospf_hdr->version = OSPF_V2;

        /* OSPFv2 Type */
        tx_ospf_hdr->type = OSPF_TYPE_LSU;

        /* Packet Length */
        tx_ospf_hdr->len = htons(sizeof(ospfv2_hdr) + sizeof(ospfv2_lsu_hdr) + (sizeof(ospfv2_lsa) * routes_num));

        /* Router ID */
        tx_ospf_hdr->rid = router_id.s_addr;    //It is the highest IP address on a router [according to Cisco]

        /* Area ID */
        tx_ospf_hdr->aid = htonl(171);    //Since we only have one Area which is Area0

        /* Checksum */
        tx_ospf_hdr->csum = 0;

        /* Authentication Type */
        tx_ospf_hdr->autype = 0;

        /* Authentication Data */
        tx_ospf_hdr->audata = 0;


        /* Sequence */
        sequence_num++;
        tx_ospf_lsu_hdr->seq = htons(sequence_num);

        /* Unused */
        tx_ospf_lsu_hdr->unused = 0;

        /* TTL */
        tx_ospf_lsu_hdr->ttl = 64;

        /* Number of advertisememts */
        tx_ospf_lsu_hdr->num_adv = htonl(routes_num);


        /***** Creating the transmitted packet *****/
        tx_packet = ((uint8_t*)(malloc(packet_len)));

        memcpy(tx_packet, tx_e_hdr, sizeof(sr_ethernet_hdr));
        memcpy(tx_packet + sizeof(sr_ethernet_hdr), tx_ip_hdr, sizeof(ip));
        memcpy(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip), tx_ospf_hdr, sizeof(ospfv2_hdr));
        memcpy(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(ospfv2_hdr), tx_ospf_lsu_hdr, sizeof(ospfv2_lsu_hdr));


        struct sr_if* f_int;
        if (int_down == 1)
        {
            f_int = sr_get_interface(sr, sr->f_interface);
        }
        int i = 0;
        struct sr_rt* entry = sr->routing_table;
        while (entry != NULL)
        {
            if (int_down == 1)
            {
                int entry_con = 0;
                if (f_int != NULL)
                {
                    if ((f_int->ip & htonl(0x0fffffffe)) == entry->dest.s_addr)
                    {
                        entry_con = 1;
                    }
                }

                if (entry_con == 1)
                {
                    entry = entry->next;
                    continue;
                }
            }

            if (entry->admin_dst <= 1)
            {
                /* Subnet */
                tx_ospf_lsa->subnet = entry->dest.s_addr;

                /* Mask */
                tx_ospf_lsa->mask = entry->mask.s_addr;

                /* Router ID */
                tx_ospf_lsa->rid = sr_get_interface(sr, entry->interface)->neighbor_id;

                memcpy(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(ospfv2_hdr) + sizeof(ospfv2_lsu_hdr) + (sizeof(ospfv2_lsa) * i),
                    tx_ospf_lsa, sizeof(ospfv2_lsa));

                i++;
            }

            entry = entry->next;
        }

        /* Re-Calculate checksum of the LSU header */
        /* Updating the new checksum in tx_packet */
        ((ospfv2_hdr*)(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip)))->csum =
            calc_cksum(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip), sizeof(ospfv2_hdr) + sizeof(ospfv2_lsu_hdr) +
            (sizeof(ospfv2_lsa) * routes_num));

        struct sr_if* temp_int = sr->if_list;
        while (temp_int != NULL)
        {
            int int_con = 0;
            if (f_int != NULL)
            {
                if ((f_int->ip & htonl(0x0fffffffe)) == temp_int->ip)
                {
                    int_con = 1;
                }
            }

            if (int_con == 1)
            {
                temp_int = temp_int->next;
                continue;
            }

            if (temp_int->neighbor_id != 0)
            {
                /* Ehternet Source address */
                for (int i = 0; i < ETHER_ADDR_LEN; i++)
                {
                    ((sr_ethernet_hdr*)(tx_packet))->ether_shost[i] = ((uint8_t)(temp_int->addr[i]));
                }
            
                /* IP Identification */
                struct timeval tv;
                gettimeofday(&tv, NULL);
                srand(tv.tv_sec * tv.tv_usec);
                ((ip*)(tx_packet + sizeof(sr_ethernet_hdr)))->ip_id = rand();
    
                /* IP Checksum */
                ((ip*)(tx_packet + sizeof(sr_ethernet_hdr)))->ip_sum = 0;

                /* Source IP address */
                ((ip*)(tx_packet + sizeof(sr_ethernet_hdr)))->ip_src.s_addr = temp_int->ip;

                /* Re-Calculate checksum of the IP header */
                ((ip*)(tx_packet + sizeof(sr_ethernet_hdr)))->ip_sum = calc_cksum(((uint8_t*)(tx_packet + sizeof(sr_ethernet_hdr))), sizeof(ip));

                Debug("-> PWOSPF: Sending LSU Update of length = %d, out of the interface: %s\n", packet_len, temp_int->name);
                sr_send_packet(sr, ((uint8_t*)(tx_packet)), packet_len, temp_int->name);
            }

            temp_int = temp_int->next;
        }

        free(tx_packet);
        free(tx_ospf_lsa);
        free(tx_ospf_lsu_hdr);
        free(tx_ospf_hdr);
        free(tx_ip_hdr);
        free(tx_e_hdr);


        pwospf_unlock(sr->ospf_subsys);
    };

    return NULL;
} /* -- send_all_lsu -- */


/*---------------------------------------------------------------------
 * Method: run_dijkstra
 *
 * Run Dijkstra algorithm
 *
 *---------------------------------------------------------------------*/

void* run_dijkstra(void* arg)
{
    struct dijkstra_param* dij_param = ((dijkstra_param*)(arg));

    pthread_mutex_lock(&dijkstra_mutex);


    struct in_addr zero;
    zero.s_addr = 0;
    dijkstra_stack = create_dikjstra_item(create_ospfv2_topology_entry(zero, zero, zero, zero, zero, 0), 0);
    dijkstra_heap = create_dikjstra_item(create_ospfv2_topology_entry(zero, zero, zero, zero, zero, 0), 0);


    /* Cleaing the routing table */
//printf("1111111111111111111111111111111111111111111111111111111111111111111111111111111111\n");
/*    Debug("\n-> PWOSPF: Printing the forwarding table\n");
    print_routing_table(dij_param->sr);*/

    clear_routes(dij_param->sr);
/*    Debug("\n-> PWOSPF: Printing the forwarding table\n");
    print_routing_table(dij_param->sr);*/

//printf("2222222222222222222222222222222222222222222222222222222222222222222222222222222222\n");

    /* Run Dijkstra algorithm */
    struct ospfv2_topology_entry* topo_entry = first_topology_entry->next;
    while(topo_entry != NULL)
    {
//printf("*********************************************************\n");
        if (check_route(dij_param->sr, topo_entry->net_num) == 0)
        {
            struct sr_if* temp_int = dij_param->sr->if_list;
            while (temp_int != NULL)
            {
/*struct in_addr x;	x.s_addr = temp_int->ip & htonl(0xfffffffe);
printf("************************************************* %s\n", inet_ntoa(x));*/

                if (search_topolgy_table(first_topology_entry, (temp_int->ip & htonl(0xfffffffe))) == 0)
                {
//printf("************************************************* continue\n");
                    temp_int = temp_int->next;
                    continue;
                }

                if (temp_int->neighbor_id != 0)
                {
                    struct in_addr mask;	    mask.s_addr = htonl(0xfffffffe);
                    struct in_addr subnet;	    subnet.s_addr = temp_int->ip & mask.s_addr;
                    struct in_addr neighbor_id;	    neighbor_id.s_addr = temp_int->neighbor_id;
                    struct in_addr next_hop;	    next_hop.s_addr = temp_int->neighbor_ip;

/*Debug("Push in heap: ");
Debug("%-18s",inet_ntoa(router_id));
Debug("%-18s",inet_ntoa(subnet));
Debug("%-18s",inet_ntoa(mask));
Debug("%-18s",inet_ntoa(neighbor_id));
Debug("%-18s",inet_ntoa(next_hop));
Debug("%-11d",0);
Debug("%d\n",0);*/

                    dijkstra_stack_push(dijkstra_heap, create_dikjstra_item(create_ospfv2_topology_entry(router_id, subnet, mask, neighbor_id,
                        next_hop, 0), 1));
                }

                temp_int = temp_int->next;
            }

            struct dijkstra_item* dijkstra_popped_item;
            uint8_t stop = 0;
            while(1)
            {
                while(1)
                {
                    /* Pop from the heap */
                    dijkstra_popped_item = dijkstra_stack_pop(dijkstra_heap);

                    if (dijkstra_popped_item == NULL)
                    {
//printf("******* Stack is Empty\n");
                        stop = 1;
                        break;
                    }

/*Debug("Pop from heap: ");
Debug("%-18s",inet_ntoa(dijkstra_popped_item->topology_entry->router_id));
Debug("%-18s",inet_ntoa(dijkstra_popped_item->topology_entry->net_num));
Debug("%-18s",inet_ntoa(dijkstra_popped_item->topology_entry->net_mask));
Debug("%-18s",inet_ntoa(dijkstra_popped_item->topology_entry->neighbor_id));
Debug("%-18s",inet_ntoa(dijkstra_popped_item->topology_entry->next_hop));
Debug("%-11d",dijkstra_popped_item->topology_entry->sequence_num);
Debug("%d\n",dijkstra_popped_item->topology_entry->age);*/

                    if (dijkstra_popped_item->topology_entry->net_num.s_addr == topo_entry->net_num.s_addr)
                    {
//printf("******* Entry found\n");
                        stop = 1;
                        break;
                    }

                    if (dijkstra_popped_item->topology_entry->neighbor_id.s_addr != 0)
                    {
                        break;
                    }
                }

                /* Push popped item in stack */
                if (dijkstra_popped_item != NULL)
                {
                    dijkstra_stack_push(dijkstra_stack, dijkstra_popped_item);
                }


                /* stopping criteria here */
                if (stop == 1)
                {
                    break;
                }


                /* Relaxing the popped item */
                struct ospfv2_topology_entry* ptr = first_topology_entry->next;
                while(ptr != NULL)
                {
/*printf("Begin\n");
printf("%s, ", inet_ntoa(ptr->router_id));
printf("%s\n", inet_ntoa(dijkstra_popped_item->topology_entry->neighbor_id));

printf("%s, ", inet_ntoa(ptr->net_num));
printf("%s\n", inet_ntoa(dijkstra_popped_item->topology_entry->router_id));

printf("%s, ", inet_ntoa(ptr->neighbor_id));
printf("%s\n", inet_ntoa(dijkstra_popped_item->topology_entry->router_id));

printf("End\n");*/

                    if ((ptr->router_id.s_addr == dijkstra_popped_item->topology_entry->neighbor_id.s_addr) &&
                        (ptr->net_num.s_addr != dijkstra_popped_item->topology_entry->net_num.s_addr) &&
                        (ptr->neighbor_id.s_addr != dijkstra_popped_item->topology_entry->neighbor_id.s_addr))
                    {
                        struct ospfv2_topology_entry* clone = clone_ospfv2_topology_entry(ptr);

                        struct dijkstra_item* to_be_pushed = create_dikjstra_item(clone, dijkstra_popped_item->cost + 1);
                        to_be_pushed->parent = dijkstra_popped_item;


/*Debug("Push from heap: ");
Debug("%-18s",inet_ntoa(to_be_pushed->topology_entry->router_id));
Debug("%-18s",inet_ntoa(to_be_pushed->topology_entry->net_num));
Debug("%-18s",inet_ntoa(to_be_pushed->topology_entry->net_mask));
Debug("%-18s",inet_ntoa(to_be_pushed->topology_entry->neighbor_id));
Debug("%-18s",inet_ntoa(to_be_pushed->topology_entry->next_hop));
Debug("%-11d",to_be_pushed->topology_entry->sequence_num);
Debug("%d\n",to_be_pushed->topology_entry->age);*/

                        dijkstra_stack_push(dijkstra_heap, to_be_pushed);
                        dijkstra_stack_reorder(dijkstra_heap);
                    }

                    ptr = ptr->next;
                }
            }


//Debug("**********************************************************************************\n");
            struct dijkstra_item* final_item = dijkstra_stack_pop(dijkstra_stack);

            if (final_item != NULL)
            {
/*Debug("final_item: ");
Debug("%-18s",inet_ntoa(final_item->topology_entry->router_id));
Debug("%-18s",inet_ntoa(final_item->topology_entry->net_num));
Debug("%-18s",inet_ntoa(final_item->topology_entry->net_mask));
Debug("%-18s",inet_ntoa(final_item->topology_entry->neighbor_id));
Debug("%-18s",inet_ntoa(final_item->topology_entry->next_hop));
Debug("%-11d",final_item->topology_entry->sequence_num);
Debug("%d\n",final_item->topology_entry->age);*/

                while (final_item->parent != NULL)
                {
                    final_item = final_item->parent; //dijkstra_stack_pop(dijkstra_stack);

/*Debug("final_item: ");
Debug("%-18s",inet_ntoa(final_item->topology_entry->router_id));
Debug("%-18s",inet_ntoa(final_item->topology_entry->net_num));
Debug("%-18s",inet_ntoa(final_item->topology_entry->net_mask));
Debug("%-18s",inet_ntoa(final_item->topology_entry->neighbor_id));
Debug("%-18s",inet_ntoa(final_item->topology_entry->next_hop));
Debug("%-11d",final_item->topology_entry->sequence_num);
Debug("%d\n",final_item->topology_entry->age);*/
                }
//Debug("**********************************************************************************\n");



                struct sr_if* next_hop_int = dij_param->sr->if_list;
                while (next_hop_int != NULL)
                {
//printf("**** %d, %d\n", (next_hop_int->ip & htonl(0xfffffffe)), (final_item->topology_entry->next_hop.s_addr & htonl(0xfffffffe)));
                    if ((next_hop_int->ip & htonl(0xfffffffe)) == (final_item->topology_entry->next_hop.s_addr & htonl(0xfffffffe)))
                    {
                        break;
                    }

                    next_hop_int = next_hop_int->next;
                }

//printf("****** next_hop_int->name = %s\n", next_hop_int->name);
            

            
                sr_add_rt_entry(dij_param->sr, topo_entry->net_num, final_item->topology_entry->next_hop,
                    /*final_item->topology_entry*/topo_entry->net_mask, next_hop_int->name, 110);
            }
        }

        topo_entry = topo_entry->next;
    }

    Debug("\n-> PWOSPF: Dijkstra algorithm completed\n\n");
    Debug("\n-> PWOSPF: Printing the forwarding table\n");
    print_routing_table(dij_param->sr);



    pthread_mutex_unlock(&dijkstra_mutex);

    return NULL;
} /* -- run_dijkstra -- */


/*---------------------------------------------------------------------
 * Method: check_neighbors_life
 *
 * Check if the neighbors are alive 
 *
 *---------------------------------------------------------------------*/

void* check_neighbors_life(void* arg)
{
    while(1)
    {
        usleep(1000000);
        check_neighbors_alive(first_neighbor);
    };

    return NULL;
} /* -- check_neighbors_life -- */


/*---------------------------------------------------------------------
 * Method: check_topology_entries_age
 *
 * Check if the topology entries are alive 
 *
 *---------------------------------------------------------------------*/

void* check_topology_entries_age(void* arg)
{
    struct sr_instance* sr = (struct sr_instance*)arg;

    while(1)
    {
        usleep(1000000);
        if (check_topology_age(first_topology_entry) == 1)
        {
            Debug("\n-> PWOSPF: Printing the topology table\n");
            print_topolgy_table(first_topology_entry);
            Debug("\n");

            struct dijkstra_param* dij_param = ((dijkstra_param*)(malloc(sizeof(dijkstra_param))));
            dij_param->sr = sr;
            dij_param->first_topology_entry = first_topology_entry;
            pthread_create(&dijkstra_thread, NULL, run_dijkstra, dij_param);
        }
    };

    return NULL;
} /* -- check_topology_entries_age -- */


/*--------------------------------------------------------------------- 
 * Method: print_routing_table
 *
 *---------------------------------------------------------------------*/

void print_routing_table(struct sr_instance* sr)
{
    //Debug("-----------------------------------------------------------------------\n");
    Debug("=======================================================================\n");
    Debug("%-18s%-18s%-18s%-8sAdmin Dis\n", "Destination", "Gateway", "Subnet Mask", "Iface");
    Debug("%-18s%-18s%-18s%-8s---------\n", "-----------", "-------", "-----------", "-----");

    struct sr_rt* entry = sr->routing_table;
    if (entry == NULL)
    {
        Debug("The forwarding table is empty");
    }
    else
    {
        while(entry != NULL)
        {
            Debug("%-18s",inet_ntoa(entry->dest));
            Debug("%-18s",inet_ntoa(entry->gw));
            Debug("%-18s",inet_ntoa(entry->mask));
            Debug("%-8s",entry->interface);
            Debug("%d\n",entry->admin_dst);

            entry = entry->next; 
        }
    }
    Debug("=======================================================================\n");
} /* -- print_routing_table -- */
