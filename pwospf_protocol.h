/*-----------------------------------------------------------------------------
 * file:  pwospf_protocol.h
 * date:  Thu Mar 18 15:14:06 PST 2004 
 * Author: Martin Casado
 *
 * Description:
 *
 * Protocol headers for the PWOSPF protocol
 *
 *---------------------------------------------------------------------------*/

#ifndef PWOSPF_PROTOCOL_H
#define PWOSPF_PROTOCOL_H

static const uint8_t OSPF_V2        = 2;

static const uint32_t OSPF_AllSPFRouters = 0xe0000005; /*"224.0.0.5"*/

static const uint8_t OSPF_TYPE_HELLO = 1;
static const uint8_t OSPF_TYPE_LSU   = 4;
static const uint8_t OSPF_TYPE_LSUPDATE = 4;
static const uint8_t OSPF_NET_BROADCAST = 1;
static const uint8_t OSPF_DEFAULT_HELLOINT  =  5; /* seconds */
static const uint8_t OSPF_DEFAULT_LSUINT    = 30; /* seconds */
static const uint8_t OSPF_NEIGHBOR_TIMEOUT  = 20; /* seconds */ 

static const uint8_t OSPF_TOPO_ENTRY_TIMEOUT = 35; /* seconds */ 

static const uint8_t OSPF_DEFAULT_AUTHKEY  =  0; /* ignored */

static const uint16_t OSPF_MAX_HELLO_SIZE  = 1024; /* bytes */
static const uint16_t OSPF_MAX_LSU_SIZE    = 1024; /* bytes */
static const uint8_t  OSPF_MAX_LSU_TTL     = 255;  


struct ospfv2_hdr
{
    uint8_t version; /* ospf version number */
    uint8_t type;    /* type of ospf packet */
    uint16_t len;    /* length of packet in bytes including header */
    uint32_t rid;    /* router ID of packet source */
    uint32_t aid;    /* area packet belongs to */
    uint16_t csum;   /* checksum */ 
    uint16_t autype; /* authentication type */
    uint64_t audata; /* used by authentication scheme */
}__attribute__ ((packed));

struct ospfv2_hello_hdr
{
    uint32_t nmask;    /* netmask of source interface */
    uint16_t helloint; /* interval time for hello broadcasts */
    uint16_t padding;
}__attribute__ ((packed));

struct ospfv2_lsu_hdr
{
    uint16_t seq;
    uint8_t  unused;
    uint8_t  ttl;
    uint32_t num_adv;  /* number of advertisements */
}__attribute__ ((packed));

struct ospfv2_lsa
{
    uint32_t subnet; /* -- link subnet -- */
    uint32_t mask;   /* -- link subnet mask -- */
    uint32_t rid;    /* -- attached router id (if any) -- */
}__attribute__ ((packed));


#endif  /* PWOSPF_PROTOCOL_H */
