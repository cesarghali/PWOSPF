/**********************************************************************
 * file:  sr_router.c 
 * date:  Mon Feb 18 12:50:42 PST 2002  
 * Contact: casado@stanford.edu 
 *
 * Description:
 * 
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <stdio.h>
#include <assert.h>

#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"
#include "pwospf_protocol.h"
#include "sr_pwospf.h"

#include "queue.h"
#include "cache.h"


struct queue_item* packet_queue[3];
struct cache_item* arp_cache;
pthread_t cache_thread;
pthread_t arp_thread[3];
int stop_arp_thread[3];

//uint32_t default_gateway_addr = 290068652;

uint8_t sr_multicast_mac[ETHER_ADDR_LEN];

/*--------------------------------------------------------------------- 
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 * 
 *---------------------------------------------------------------------*/

void sr_init(struct sr_instance* sr) 
{
    /* REQUIRES */
    assert(sr);

    pwospf_init(sr);

    /* Add initialization code here! */
    sr_multicast_mac[0] = 0x01;
    sr_multicast_mac[1] = 0x00;
    sr_multicast_mac[2] = 0x5e;
    sr_multicast_mac[3] = 0x00;
    sr_multicast_mac[4] = 0x00;
    sr_multicast_mac[5] = 0x05;

    packet_queue[0] = queue_create_item(NULL, 0, NULL);
    packet_queue[1] = queue_create_item(NULL, 0, NULL);
    packet_queue[2] = queue_create_item(NULL, 0, NULL);

    unsigned char empty_mac[ETHER_ADDR_LEN] = {0};
    arp_cache = cache_create_item(0, empty_mac, 0);

    pthread_create(&cache_thread, NULL, check_cache, NULL);
} /* -- sr_init -- */


/*--------------------------------------------------------------------- 
 * Method: check_cache
 *
 *---------------------------------------------------------------------*/

void* check_cache(void* args)
{
    while(1)
    {
        usleep(1000000);
        check_cache(arp_cache);
    }
}/* end check_cache */


/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/

void sr_handlepacket(struct sr_instance* sr, 
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */)
{
    /* REQUIRES */
    assert(sr);
    assert(packet);
    assert(interface);

    struct sr_if* rx_if = sr_get_interface(sr, interface);
    struct sr_ethernet_hdr* rx_e_hdr = (struct sr_ethernet_hdr*)packet;


    if (chk_ether_addr(rx_e_hdr, rx_if) == 0)
    {
        return;
    }

    switch (htons(rx_e_hdr->ether_type))
    {
        case ETHERTYPE_ARP:
            handle_arp_packet(sr, packet, len, rx_if, rx_e_hdr);
            break;

        case ETHERTYPE_IP:
            handle_ip_packet(sr, packet, len, rx_if, rx_e_hdr);
            break;

        default:
            Debug("\nReceived Unknow Packet, length = %d\n", len);
            break;
    }
}/* end sr_handlepacket */


/*--------------------------------------------------------------------- 
 * Method: handle_ARP_packet
 *
 *---------------------------------------------------------------------*/

void handle_arp_packet(struct sr_instance* sr, uint8_t* packet, unsigned int len, struct sr_if* rx_if, struct sr_ethernet_hdr* rx_e_hdr)
{
    struct sr_ethernet_hdr* tx_e_hdr = ((sr_ethernet_hdr*)(malloc(sizeof(sr_ethernet_hdr))));
    uint8_t* tx_packet;
    int queue_index;

    /***** Getting the ARP header *****/
    struct sr_arphdr* rx_arp_hdr = ((sr_arphdr*)(packet + sizeof(sr_ethernet_hdr)));
    struct sr_arphdr* tx_arp_hdr = ((sr_arphdr*)(malloc(sizeof(sr_arphdr))));


    switch (htons(rx_arp_hdr->ar_op))
    {
        case ARP_REQUEST:
            Debug("\nReceived ARP REQUEST Packet, length = %d\n", len);

            Debug("-> Constructing ARP REPLY Packet\n");
            /* Destination address */
            for (int i = 0; i < ETHER_ADDR_LEN; i++)
            {
                tx_e_hdr->ether_dhost[i] = rx_e_hdr->ether_shost[i];
            }

            /* Source address */
            for (int i = 0; i < ETHER_ADDR_LEN; i++)
            {
                tx_e_hdr->ether_shost[i] = ((uint8_t)(rx_if->addr[i]));
            }
	    
            /* Type */
            tx_e_hdr->ether_type = rx_e_hdr->ether_type;


            /* Hardware type */
            tx_arp_hdr->ar_hrd = rx_arp_hdr->ar_hrd;

            /* Protocol type */
            tx_arp_hdr->ar_pro = rx_arp_hdr->ar_pro;

            /* Hardware address length */
            tx_arp_hdr->ar_hln = rx_arp_hdr->ar_hln;

	    /* Protocol address length */
	    tx_arp_hdr->ar_pln = rx_arp_hdr->ar_pln;

	    /* Operation code */
	    tx_arp_hdr->ar_op = htons(ARP_REPLY);

	    /* Source hardware address */
            for (int i = 0; i < ETHER_ADDR_LEN; i++)
            {
                tx_arp_hdr->ar_sha[i] = ((uint8_t)(rx_if->addr[i]));
            }

	    /* Source protocol address */
	    tx_arp_hdr->ar_sip = rx_arp_hdr->ar_tip;

	    /* Target hardware address */
            for (int i = 0; i < ETHER_ADDR_LEN; i++)
            {
                tx_arp_hdr->ar_tha[i] = rx_arp_hdr->ar_sha[i];
            }

	    /* Target protocol address */
	    tx_arp_hdr->ar_tip = rx_arp_hdr->ar_sip;


	    /***** Creating the transmitted packet *****/
	    tx_packet = ((uint8_t*)(malloc(sizeof(sr_ethernet_hdr) + sizeof(sr_arphdr))));
	    memcpy(tx_packet, tx_e_hdr, sizeof(sr_ethernet_hdr));
	    memcpy(tx_packet + sizeof(sr_ethernet_hdr), tx_arp_hdr, sizeof(sr_arphdr));


            Debug("-> Sending ARP REPLY Packet, length = %d\n", sizeof(sr_ethernet_hdr) + sizeof(sr_arphdr));
            sr_send_packet(sr, ((uint8_t*)(tx_packet)), sizeof(sr_ethernet_hdr) + sizeof(sr_arphdr), rx_if->name);


            free(tx_packet);
            free(tx_arp_hdr);
            free(tx_e_hdr);
            break;

        case ARP_REPLY:
            Debug("\nReceived ARP REPLY Packet, length = %d\n", len);

            /* Checking the ARP cache */
            struct in_addr ip_address;

            ip_address.s_addr = rx_arp_hdr->ar_sip;
            if (cache_search(arp_cache, rx_arp_hdr->ar_sip) == NULL)
            {
                Debug("-> Updating the ARP Cache, [%s, ", inet_ntoa(ip_address));
                DebugMAC(rx_arp_hdr->ar_sha);
                Debug("]\n");

                if (arp_cache == NULL)
                {
                    time_t time_stamp;
                    time(&time_stamp);
                    arp_cache = cache_create_item(rx_arp_hdr->ar_sip, rx_arp_hdr->ar_sha, ((int)(time_stamp)));
                }
                else
                {
                    time_t time_stamp;
                    time(&time_stamp);
                    cache_push(arp_cache, cache_create_item(rx_arp_hdr->ar_sip, rx_arp_hdr->ar_sha, ((int)(time_stamp))));
                }
            }
            else
            {
                Debug("-> Entry already exists in the ARP Cache, [%s, ", inet_ntoa(ip_address));
                DebugMAC(rx_arp_hdr->ar_sha);
                Debug("]\n");
            }

            queue_index = ((int)(rx_if->name[strlen(rx_if->name) - 1])) - 48;

            /***** Stop the ARP_THREAD *****/
            if (stop_arp_thread[queue_index] == 0)
            {
                Debug("-> Stopping the ARP REQUESTs thread\n");
                stop_arp_thread[queue_index] = 1;
                //pthread_cancel(arp_thread[queue_index]);
            }

            if (queue_is_empty(packet_queue[queue_index]) == 0)
            {
                struct queue_item* item = queue_pop(packet_queue[queue_index]);
                Debug("-> Popping a packet from the queue, length = %d\n", item->length);

                Debug("-> Updating the popped packet\n");    
                for (int i = 0; i < ETHER_ADDR_LEN; i++)
                {
                    item->packet[i] = rx_arp_hdr->ar_sha[i];
                }

                Debug("-> Sending the popped packet, length = %d\n", item->length);
                sr_send_packet(sr, item->packet, item->length, item->interface);

                free(item);
            }
            break;
    }
}/* end handle_ARP_packet */


/*--------------------------------------------------------------------- 
 * Method: handle_IP_packet
 *
 *---------------------------------------------------------------------*/

void handle_ip_packet(struct sr_instance* sr, uint8_t* packet, unsigned int len, struct sr_if* rx_if, struct sr_ethernet_hdr* rx_e_hdr)
{
    Debug("\nReceived IP Packet, length = %d\n", len);


    struct sr_ethernet_hdr* tx_e_hdr = ((sr_ethernet_hdr*)(malloc(sizeof(sr_ethernet_hdr))));
    struct sr_icmphdr* rx_icmp_hdr;
    struct sr_icmphdr* tx_icmp_hdr;
    uint8_t* tx_packet;
    int queue_index;

    /***** Getting the IP header *****/
    ip* rx_ip_hdr = ((ip*)(packet + sizeof(sr_ethernet_hdr)));
    ip* tx_ip_hdr = ((ip*)(malloc(sizeof(ip))));

    /***** Checking the received Checksum *****/
    int rx_sum_temp = rx_ip_hdr->ip_sum;
    rx_ip_hdr->ip_sum = 0;
    uint16_t rx_sum = calc_cksum(((uint8_t*)(rx_ip_hdr)), sizeof(ip));
    rx_ip_hdr->ip_sum = rx_sum_temp;
    if (rx_sum != rx_sum_temp)
    {
        Debug("-> Packet dropped: invalid checksum\n");
        return;
    }


    if (chk_ip_addr(rx_ip_hdr, sr) == 0)
    {
        /***** Checking the received TTL *****/
        if (rx_ip_hdr->ip_ttl <= 1)
        {
            Debug("-> Packet dropped: invalid TTL\n");
            send_icmp_error(sr, packet, len, rx_if, ICMP_TIME_EXCEEDED_TYPE, ICMP_TIME_EXCEEDED_CODE);
        }
        else
        {
            Debug("-> Forwarding packet, length = %d\n", len);
            forward_packet(sr, packet, len);
        }

        return;
    }

    /***** Checking the received TTL *****/
    if (rx_ip_hdr->ip_ttl <= 1)
    {
        Debug("-> Packet dropped: invalid TTL\n");
        send_icmp_error(sr, packet, len, rx_if, ICMP_DESTINATION_UNREACHABLE_TYPE, ICMP_PORT_UNREACHABLE_CODE);

        return;
    }

    switch (rx_ip_hdr->ip_p)
    {
         case IP_PROTO_ICMP:
            /***** Getting the ICMP header *****/
	    rx_icmp_hdr = ((sr_icmphdr*)(packet + sizeof(sr_ethernet_hdr) + sizeof(ip)));
            tx_icmp_hdr = ((sr_icmphdr*)(malloc(sizeof(sr_icmphdr))));

            if ((rx_icmp_hdr->type == ICMP_ECHO_REQUEST_TYPE) & (rx_icmp_hdr->code == ICMP_ECHO_REQUEST_CODE))
            {
                Debug("-> The IP Packet is ICMP ECHO REQUEST\n");

                Debug("-> Constructing ICMP ECHO REPLY Packet\n");
                /* Destination address */
                for (int i = 0; i < ETHER_ADDR_LEN; i++)
                {
                    tx_e_hdr->ether_dhost[i] = 255;
                }

                /* Source address */   
                for (int i = 0; i < ETHER_ADDR_LEN; i++)
                {
                    tx_e_hdr->ether_shost[i] = ((uint8_t)(rx_if->addr[i]));
                }         
                
                /* Type */
                tx_e_hdr->ether_type = rx_e_hdr->ether_type;


                /* Version + Header length */
                tx_ip_hdr->ip_v = rx_ip_hdr->ip_v;
                tx_ip_hdr->ip_hl = rx_ip_hdr->ip_hl;

                /* DS */
                tx_ip_hdr->ip_tos = rx_ip_hdr->ip_tos;

                /* Total length */
                tx_ip_hdr->ip_len = rx_ip_hdr->ip_len;

                /* Identification */
                tx_ip_hdr->ip_id = rx_ip_hdr->ip_id;

                /* Fragment */
                tx_ip_hdr->ip_off = rx_ip_hdr->ip_off;

                /* TTL */
                tx_ip_hdr->ip_ttl = 64;

                /* Protocol */
                tx_ip_hdr->ip_p = rx_ip_hdr->ip_p;  // which is 1 = ICMP

                /* Checksum */
                tx_ip_hdr->ip_sum = 0;

                /* Source IP address */
                tx_ip_hdr->ip_src = rx_ip_hdr->ip_dst;

                /* Destination IP address */
                tx_ip_hdr->ip_dst = rx_ip_hdr->ip_src;

                /* Re-Calculate checksum of the IP header */
                tx_ip_hdr->ip_sum = calc_cksum(((uint8_t*)(tx_ip_hdr)), sizeof(ip));


                /* Type */
                tx_icmp_hdr->type = ICMP_ECHO_REPLY_TYPE;

                /* Code */
                tx_icmp_hdr->code = ICMP_ECHO_REPLY_CODE;

                /* Checksum */
                tx_icmp_hdr->cksum = 0;

                /* Identification */
                tx_icmp_hdr->id = rx_icmp_hdr->id;

                /* Sequence Number */
                tx_icmp_hdr->seq_n = rx_icmp_hdr->seq_n;


                /***** Creating the transmitted packet *****/
                tx_packet = ((uint8_t*)(malloc(sizeof(uint8_t) * len)));

                memcpy(tx_packet, tx_e_hdr, sizeof(sr_ethernet_hdr));
                memcpy(tx_packet + sizeof(sr_ethernet_hdr), tx_ip_hdr, sizeof(ip));
                memcpy(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip), tx_icmp_hdr, sizeof(sr_icmphdr));
                /* Copy the Data part */
                for (unsigned int i = sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(sr_icmphdr); i < len; i++)
                {
                    tx_packet[i] = packet[i];
                }


                /* Re-Calculate checksum of the ICMP header */
                /* Updating the new checksum in tx_packet */
                ((sr_icmphdr*)(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip)))->cksum =
                    calc_cksum(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip), len - (sizeof(sr_ethernet_hdr) + sizeof(ip)));


                /* Checking the ARP cache */
                struct in_addr ip_address;
                ip_address.s_addr = get_nex_hop_ip(sr, rx_if->name);
                Debug("-> Searching the ARP Cache for [%s]\n", inet_ntoa(ip_address));
                cache_item* item = cache_search(arp_cache, get_nex_hop_ip(sr, rx_if->name));
                if (item == NULL)
                {
                    /* Push the packet in the queue */
                    Debug("-> ARP Cache entry NOT found\n");

                    Debug("-> Pushing the ICMP ECHO REPLY Packet in the queue, length = %d\n", len);
                    queue_index = ((int)(rx_if->name[strlen(rx_if->name) - 1])) - 48;
                    if (packet_queue[queue_index] == NULL)
                    {
                        packet_queue[queue_index] = queue_create_item(tx_packet, len, rx_if->name);
                    }
                    else
                    {
                        queue_push(packet_queue[queue_index], queue_create_item(tx_packet, len, rx_if->name));
                    }
                    send_arp_request(sr, rx_if, get_nex_hop_ip(sr, rx_if->name));
                }
                else
                {
                    ip_address.s_addr = item->ip;
                    Debug("-> ARP Cache entry found, [%s, ", inet_ntoa(ip_address));
                    DebugMAC(item->mac);
                    Debug("]\n");

                    Debug("-> Updating the ICMP ECHO REPLY Packet\n");    
                    for (int i = 0; i < ETHER_ADDR_LEN; i++)
                    {
                        tx_packet[i] = item->mac[i];
                    }

                    Debug("-> Sending the ICMP ECHO REPLY Packet, length = %d\n", len);
                    sr_send_packet(sr, tx_packet, len, rx_if->name);


                    free(tx_packet);
                }

                
                free(tx_icmp_hdr);
                free(tx_ip_hdr);
                free(tx_e_hdr);
            }
            else if ((rx_icmp_hdr->type == ICMP_ECHO_REPLY_TYPE) & (rx_icmp_hdr->code == ICMP_ECHO_REPLY_CODE))
            {
                Debug("-> The IP Packet is ICMP ECHO REPLY\n");
            }
            break;

        case IP_PROTO_TCP:
        case IP_PROTO_UDP:
            if (rx_ip_hdr->ip_p == IP_PROTO_TCP)
            {
                Debug("-> The IP Packet is TCP Packet\n");
            }
            else
            {
                Debug("-> The IP Packet is UDP Packet\n");
            }

            /***** Seding an ICMP PORT UNREACHABLE packet *****/
            send_icmp_error(sr, packet, len, rx_if, ICMP_DESTINATION_UNREACHABLE_TYPE, ICMP_PORT_UNREACHABLE_CODE);
            break;

        case IP_PROTO_OSPFv2:
            Debug("-> The IP Packet is PWOSPF Packet\n");
            handling_ospfv2_packets(sr, packet, len, rx_if);
            break;
    }
}/* end handle_ip_packet */


/*--------------------------------------------------------------------- 
 * Method: send_icmp_error
 *
 *---------------------------------------------------------------------*/
void send_icmp_error(struct sr_instance* sr, uint8_t* packet, unsigned int len, struct sr_if* rx_if, uint8_t type, uint8_t code)
{
    struct sr_ethernet_hdr* rx_e_hdr = (struct sr_ethernet_hdr*)packet;
    struct sr_ethernet_hdr* tx_e_hdr = ((sr_ethernet_hdr*)(malloc(sizeof(sr_ethernet_hdr))));
    struct sr_icmphdr* rx_icmp_hdr;
    struct sr_icmphdr* tx_icmp_hdr;
    uint8_t* tx_packet;
    int queue_index;

    /***** Getting the IP header *****/
    ip* rx_ip_hdr = ((ip*)(packet + sizeof(sr_ethernet_hdr)));
    ip* tx_ip_hdr = ((ip*)(malloc(sizeof(ip))));

    if (type == ICMP_DESTINATION_UNREACHABLE_TYPE)
    {
        if (code == ICMP_HOST_UNREACHABLE_CODE)
        {
            Debug("-> Constructing ICMP HOST UNREACHABLE Packet\n");
        }
        else if (code == ICMP_PORT_UNREACHABLE_CODE)
        {
            Debug("-> Constructing ICMP PORT UNREACHABLE Packet\n");
        }
    }
    else if ((type == ICMP_TIME_EXCEEDED_TYPE) & (code == ICMP_TIME_EXCEEDED_CODE))
    {
        Debug("-> Constructing ICMP TIME EXCEEDED Packet\n");
    }

    /***** Getting the ICMP header *****/
    rx_icmp_hdr = ((sr_icmphdr*)(packet + sizeof(sr_ethernet_hdr) + sizeof(ip)));
    tx_icmp_hdr = ((sr_icmphdr*)(malloc(sizeof(sr_icmphdr))));

    /* Destination address */
    for (int i = 0; i < ETHER_ADDR_LEN; i++)
    {
        tx_e_hdr->ether_dhost[i] = 255; //rx_e_hdr->ether_shost[i];
    }

    /* Source address */
    for (int i = 0; i < ETHER_ADDR_LEN; i++)
    {
        tx_e_hdr->ether_shost[i] = ((uint8_t)(rx_if->addr[i]));
    }         

    /* Type */
    tx_e_hdr->ether_type = rx_e_hdr->ether_type;


    /* Version + Header length */
    tx_ip_hdr->ip_v = rx_ip_hdr->ip_v;
    tx_ip_hdr->ip_hl = rx_ip_hdr->ip_hl;

    /* DS */
    tx_ip_hdr->ip_tos = rx_ip_hdr->ip_tos;

    /* Total length */
    tx_ip_hdr->ip_len = htons((2 * sizeof(ip)) + sizeof(sr_icmphdr) + 8);

    /* Identification */
    tx_ip_hdr->ip_id = rx_ip_hdr->ip_id;

    /* Fragment */
    tx_ip_hdr->ip_off = rx_ip_hdr->ip_off;

    /* TTL */
    tx_ip_hdr->ip_ttl = 64;

    /* Protocol */
    tx_ip_hdr->ip_p = IP_PROTO_ICMP;  // which is 1 = ICMP

    /* Checksum */
    tx_ip_hdr->ip_sum = 0;

    /* Source IP address */
    if (type == 11)
    {
        tx_ip_hdr->ip_src.s_addr = rx_if->ip;
    }
    else
    {
        tx_ip_hdr->ip_src = rx_ip_hdr->ip_dst;
    }

    /* Destination IP address */
    tx_ip_hdr->ip_dst = rx_ip_hdr->ip_src;

    /* Re-Calculate checksum of the IP header */
    tx_ip_hdr->ip_sum = calc_cksum(((uint8_t*)(tx_ip_hdr)), sizeof(ip));


    /* Type */
    tx_icmp_hdr->type = type;

    /* Code */
    tx_icmp_hdr->code = code;

    /* Checksum */
    tx_icmp_hdr->cksum = 0;

    /* Identification */
    tx_icmp_hdr->id = 0;

    /* Sequence Number */
    tx_icmp_hdr->seq_n = 0;

    /***** Creating the transmitted packet *****/
    tx_packet = ((uint8_t*)(malloc(sizeof(sr_ethernet_hdr) + (2 * sizeof(ip)) + sizeof(sr_icmphdr) + 8)));

    memcpy(tx_packet, tx_e_hdr, sizeof(sr_ethernet_hdr));
    memcpy(tx_packet + sizeof(sr_ethernet_hdr), tx_ip_hdr, sizeof(ip));
    memcpy(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip), tx_icmp_hdr, sizeof(sr_icmphdr));
    /* Copy the IP daragram that trrigered the error */
    memcpy(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip) + sizeof(sr_icmphdr), rx_ip_hdr, sizeof(ip));
    for (unsigned int i = 0; i < 8; i++)
    {
        tx_packet[sizeof(sr_ethernet_hdr) + (2 * sizeof(ip)) + sizeof(sr_icmphdr) + i] = packet[sizeof(sr_ethernet_hdr) + sizeof(ip) + i];
    }

    /* Re-Calculate checksum of the ICMP header */
    /* Updating the new checksum in tx_packet */
    ((sr_icmphdr*)(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip)))->cksum =
        calc_cksum(tx_packet + sizeof(sr_ethernet_hdr) + sizeof(ip), sizeof(sr_icmphdr) + sizeof(ip) + 8);


    /* Checking the ARP cache */
    struct in_addr ip_address;
    ip_address.s_addr = get_nex_hop_ip(sr, rx_if->name);
    Debug("-> Searching the ARP Cache for [%s]\n", inet_ntoa(ip_address));
    cache_item* item = cache_search(arp_cache, get_nex_hop_ip(sr, rx_if->name));
    if (item == NULL)
    {
        /* Push the packet in the queue */
        Debug("-> ARP Cache entry NOT found\n");

        /* Push the packet in the queue */
        Debug("-> Pushing the ICMP ERROR MESSAGE Packet in the queue, length = %d\n",
            sizeof(uint8_t) * (sizeof(sr_ethernet_hdr) + (2 * sizeof(ip)) + sizeof(sr_icmphdr) + 8));
        queue_index = ((int)(rx_if->name[strlen(rx_if->name) - 1])) - 48;
        if (packet_queue[queue_index] == NULL)
        {
            packet_queue[queue_index] = queue_create_item(tx_packet, sizeof(sr_ethernet_hdr) + (2 * sizeof(ip)) + sizeof(sr_icmphdr) + 8, rx_if->name);
        }
        else
        {
            queue_push(packet_queue[queue_index], queue_create_item(tx_packet, sizeof(sr_ethernet_hdr) + (2 * sizeof(ip)) + sizeof(sr_icmphdr) + 8,
                rx_if->name));
        }

        send_arp_request(sr, rx_if, get_nex_hop_ip(sr, rx_if->name));
    }
    else
    {
        ip_address.s_addr = item->ip;
        Debug("-> ARP Cache entry found, [%s, ", inet_ntoa(ip_address));
        DebugMAC(item->mac);
        Debug("]\n");

        if (type == ICMP_DESTINATION_UNREACHABLE_TYPE)
        {
            if (code == ICMP_HOST_UNREACHABLE_CODE)
            {
                Debug("-> Updating the ICMP HOST UNREACHABLE Packet\n");
            }
            else if (code == ICMP_PORT_UNREACHABLE_CODE)
            {
                Debug("-> Updating the ICMP PORT UNREACHABLE Packet\n");
            }
        }
        else if ((type == ICMP_TIME_EXCEEDED_TYPE) & (code == ICMP_TIME_EXCEEDED_CODE))
        {
            Debug("-> Updating the ICMP TIME EXCEEDED Packet\n");
        }
        for (int i = 0; i < ETHER_ADDR_LEN; i++)
        {
            tx_packet[i] = item->mac[i];
        }

        if (type == ICMP_DESTINATION_UNREACHABLE_TYPE)
        {
            if (code == ICMP_HOST_UNREACHABLE_CODE)
            {
                Debug("-> Sending the ICMP HOST UNREACHABLE Packet, length = %d\n", len);
            }
            else if (code == ICMP_PORT_UNREACHABLE_CODE)
            {
                Debug("-> Sending the ICMP PORT UNREACHABLE Packet, length = %d\n", len);
            }
        }
        else if ((type == ICMP_TIME_EXCEEDED_TYPE) & (code == ICMP_TIME_EXCEEDED_CODE))
        {
            Debug("-> Sending the ICMP TIME EXCEEDED Packet, length = %d\n", len);
        }
        sr_send_packet(sr, tx_packet, sizeof(sr_ethernet_hdr) + (2 * sizeof(ip)) + sizeof(sr_icmphdr) + 8, rx_if->name);


        free(tx_packet);
    }

    
    free(tx_icmp_hdr);
    free(tx_ip_hdr);
    free(tx_e_hdr);
}/* end send_icmp_error */


/*--------------------------------------------------------------------- 
 * Method: send_arp_request
 *
 *---------------------------------------------------------------------*/

void send_arp_request(struct sr_instance* sr, struct sr_if* rx_if, uint32_t target_ip)
{
    Debug("-> Constructing ARP REQUEST Packet\n");
    struct sr_ethernet_hdr* tx_e_hdr = ((sr_ethernet_hdr*)(malloc(sizeof(sr_ethernet_hdr))));
    struct sr_arphdr* tx_arp_hdr = ((sr_arphdr*)(malloc(sizeof(sr_arphdr))));


    /* Destination address */
    for (int i = 0; i < ETHER_ADDR_LEN; i++)
    {
        tx_e_hdr->ether_dhost[i] = 255;
    }

    /* Source address */
    for (int i = 0; i < ETHER_ADDR_LEN; i++)
    {
        tx_e_hdr->ether_shost[i] = ((uint8_t)(rx_if->addr[i]));
    }
    
    /* Type */
    tx_e_hdr->ether_type = htons(ETHERTYPE_ARP);


    /* Hardware type */
    tx_arp_hdr->ar_hrd = htons(ARPHDR_ETHER);

    /* Protocol type */
    tx_arp_hdr->ar_pro = htons(ETHERTYPE_IP);

    /* Hardware address length */
    tx_arp_hdr->ar_hln = ETHER_ADDR_LEN;

    /* Protocol address length */
    tx_arp_hdr->ar_pln = IP_ADDR_LEN;

    /* Operation code */
    tx_arp_hdr->ar_op = htons(ARP_REQUEST);

    /* Source hardware address */
    for (int i = 0; i < ETHER_ADDR_LEN; i++)
    {
        tx_arp_hdr->ar_sha[i] = ((uint8_t)(rx_if->addr[i]));
    }

    /* Source protocol address */
    tx_arp_hdr->ar_sip = rx_if->ip;

    /* Target hardware address */
    for (int i = 0; i < ETHER_ADDR_LEN; i++)
    {
        tx_arp_hdr->ar_tha[i] = 225;
    }

    /* Target protocol address */
    tx_arp_hdr->ar_tip = target_ip;


    /***** Creating the transmitted packet *****/
    uint8_t* tx_packet = ((uint8_t*)(malloc(sizeof(sr_ethernet_hdr) + sizeof(sr_arphdr))));
    memcpy(tx_packet, tx_e_hdr, sizeof(sr_ethernet_hdr));
    memcpy(tx_packet + sizeof(sr_ethernet_hdr), tx_arp_hdr, sizeof(sr_arphdr));


    struct sr_arp_thread_param* arp_param = ((sr_arp_thread_param*)(malloc(sizeof(sr_arp_thread_param))));
    arp_param->sr = sr;
    arp_param->counter = ARP_REQUESTS_NUM;
    for (int i = 0; i < ARP_REQUEST_PKT_LEN;i++)
    {
        arp_param->packet[i] = tx_packet[i];
    }
    for (int i = 0; i < sr_IFACE_NAMELEN; i++)
    {
        arp_param->interface[i] = rx_if->name[i];
    }
    arp_param->target_ip = target_ip;
    
    Debug("-> Running the ARP REQUESTs thread for %d attempt(s)\n", ARP_REQUESTS_NUM);
    int queue_index = ((int)(rx_if->name[strlen(rx_if->name) - 1])) - 48;
    stop_arp_thread[queue_index] = 0;
    pthread_create(&arp_thread[queue_index], NULL, sending_arp_request, arp_param);


    free(tx_packet);
    free(tx_arp_hdr);
    free(tx_e_hdr);
}/* send_arp_request */


/*--------------------------------------------------------------------- 
 * Method: sending_arp_request
 *
 *---------------------------------------------------------------------*/

void* sending_arp_request(void* args)
{
    struct sr_arp_thread_param* arp_param = ((sr_arp_thread_param*)(args));
    int queue_index = ((int)(arp_param->interface[strlen(arp_param->interface) - 1])) - 48;

    while((arp_param->counter > 0) & (stop_arp_thread[queue_index] == 0))
    {
        struct in_addr target_addr;
        target_addr.s_addr = arp_param->target_ip;
        Debug("\n\n**** Sending ARP REQUEST Packet to [%s], length = %d [Attempt %d] ****\n\n", inet_ntoa(target_addr), sizeof(sr_ethernet_hdr) +
            sizeof(sr_arphdr), ARP_REQUESTS_NUM - arp_param->counter + 1);
        sr_send_packet(arp_param->sr, ((uint8_t*)(arp_param->packet)), ARP_REQUEST_PKT_LEN, arp_param->interface);

        arp_param->counter--;

        usleep(5000000);
    }

    if (queue_is_empty(packet_queue[queue_index]) == 0)
    {
        struct queue_item* q_item = queue_pop(packet_queue[queue_index]);
        send_icmp_error(arp_param->sr, q_item->packet, q_item->length, sr_get_interface(arp_param->sr, q_item->interface),
            ICMP_DESTINATION_UNREACHABLE_TYPE, ICMP_HOST_UNREACHABLE_CODE);
    }

    return NULL;
}/* end sending_arp_request */


/*--------------------------------------------------------------------- 
 * Method: forward_packet
 *
 *---------------------------------------------------------------------*/

void forward_packet(struct sr_instance* sr, uint8_t* packet, unsigned int len)
{
    /***** Reducing the TTL and Recalculating the Checksum *****/
    ((ip*)(packet + sizeof(sr_ethernet_hdr)))->ip_ttl--;
    ((ip*)(packet + sizeof(sr_ethernet_hdr)))->ip_sum = 0;
    ((ip*)(packet + sizeof(sr_ethernet_hdr)))->ip_sum =
        calc_cksum(packet + sizeof(sr_ethernet_hdr), sizeof(ip));


    ip* rx_ip_hdr = ((ip*)(packet + sizeof(sr_ethernet_hdr)));
    int queue_index;

    struct sr_rt* temp = sr->routing_table;
    struct sr_rt* default_route = NULL;
    struct sr_rt* route = NULL;
    while(temp != NULL)
    {
        if (temp->dest.s_addr == 0)
        {
            default_route = temp;
        }
        else
        {
            if (route == NULL)
            {
                if ((temp->dest.s_addr & htonl(0xfffffffe)/*temp->mask.s_addr*/) == (rx_ip_hdr->ip_dst.s_addr & htonl(0xfffffffe)/*temp->mask.s_addr*/))
                {
                    route = temp;
                }
            }
        }

        temp = temp->next;
    }

    struct sr_if* tx_interface;
    struct in_addr ip_address;
    if (route != NULL)
    {
        tx_interface = sr_get_interface(sr, route->interface);
        if (route->gw.s_addr != 0)
        {
            ip_address = route->gw;
        }
        else if (tx_interface->neighbor_ip != 0)
        {
            ip_address.s_addr = tx_interface->neighbor_ip;
        }
        else
        {
            ip_address.s_addr = ((ip*)(packet + sizeof(sr_ethernet_hdr)))->ip_dst.s_addr;
        }
    }
    else if (default_route != NULL)
    {
        tx_interface = sr_get_interface(sr, default_route->interface);
        ip_address = default_route->gw;
    }

    if (tx_interface != NULL)
    {
        for (int i = 0; i < ETHER_ADDR_LEN; i++)
        {
            packet[i + 6] = tx_interface->addr[i];
        }


        /* Checking the ARP cache */
        Debug("-> Searching the ARP Cache for [%s]\n", inet_ntoa(ip_address));
        cache_item* item = cache_search(arp_cache, ip_address.s_addr);
        if (item == NULL)
        {
            Debug("-> ARP Cache entry NOT found\n");

            /* Push the packet in the queue */
            Debug("-> Pushing forwarded packet in the queue, length = %d\n", len);
            queue_index = ((int)(tx_interface->name[strlen(tx_interface->name) - 1])) - 48;
            if (packet_queue[queue_index] == NULL)
            {
                packet_queue[queue_index] = queue_create_item(packet, len, tx_interface->name);
            }
            else
            {
                queue_push(packet_queue[queue_index], queue_create_item(packet, len, tx_interface->name));
            }
        
            //if (route != NULL)
            //{
                send_arp_request(sr, tx_interface, ip_address.s_addr/*route->gw.s_addr*/);
            //}
            //else if (default_route != NULL)
            //{
            //    send_arp_request(sr, tx_interface, default_route->gw.s_addr);
            //}
        }
        else
        {
            ip_address.s_addr = item->ip;
            Debug("-> ARP Cache entry found, [%s, ", inet_ntoa(ip_address));
            DebugMAC(item->mac);
            Debug("]\n");

            Debug("-> Updating the forworded packet\n");    
            for (int i = 0; i < ETHER_ADDR_LEN; i++)
            {
                packet[i] = item->mac[i];
            }

            Debug("-> Sending the forworded packet, length = %d\n", len);
            sr_send_packet(sr, packet, len, tx_interface->name);
        }
    }
    else
    {
        Debug("**** ERROR: no route the destenation ****\n");
    }
    
}/* forward_packet */


/*--------------------------------------------------------------------- 
 * Method: chk_ether_addr
 *
 *---------------------------------------------------------------------*/
 
short chk_ether_addr(struct sr_ethernet_hdr* rx_e_hdr, struct sr_if* rx_if)
{
    for (int i = 0; i < ETHER_ADDR_LEN; i++)
    {
        if ((rx_e_hdr->ether_dhost[i] != 255) & (rx_e_hdr->ether_dhost[i] != sr_multicast_mac[i]))
        {
            if (rx_e_hdr->ether_dhost[i] != ((uint8_t)(rx_if->addr[i])))
            {
                return 0;
            }
        }
    }

    return 1;
}/* end chk_ether_addr */


/*--------------------------------------------------------------------- 
 * Method: get_nex_hop_ip
 *
 *---------------------------------------------------------------------*/

uint32_t get_nex_hop_ip(struct sr_instance* sr, char* interface)
{
    struct sr_rt* temp = sr->routing_table;
    while(temp != NULL)
    {
        if (strcmp(temp->interface, interface) == 0)
        {
            if (temp->gw.s_addr != 0)
            {
                return temp->gw.s_addr;
            }
            else
            {
                struct sr_if* temp_int = sr_get_interface(sr, interface);
                return temp_int->neighbor_ip;
            }
        }

        temp = temp->next;
    }

    return 0;
}/* get_nex_hop_ip */


/*--------------------------------------------------------------------- 
 * Method: chk_ip_addr
 *
 *---------------------------------------------------------------------*/

short chk_ip_addr(struct ip* rx_ip_hdr, struct sr_instance* sr)
{
    if (rx_ip_hdr->ip_dst.s_addr == 0xffffffff || rx_ip_hdr->ip_dst.s_addr == htonl(OSPF_AllSPFRouters))
    {
        return 1;
    }

    struct sr_if* router_if = sr->if_list;
    while(router_if != NULL)
    {
        if (rx_ip_hdr->ip_dst.s_addr == router_if->ip)
        {
            return 1;
        }

        router_if = router_if->next;
    }

    return 0;
}/* chk_ip_addr */


/*--------------------------------------------------------------------- 
 * Method: calc_cksum
 *
 *---------------------------------------------------------------------*/

uint16_t calc_cksum(uint8_t* hdr, int len)
{
    long sum = 0;

    while(len > 1)
    {
        sum += *((unsigned short*)hdr);
        hdr = hdr + 2;
        if(sum & 0x80000000)
        {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        len -= 2;
    }

    if(len)
    {
        sum += (unsigned short) *(unsigned char *)hdr;
    }
          
    while(sum>>16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return ~sum;
}/* end calc_cksum */
