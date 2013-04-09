/*-----------------------------------------------------------------------------
 * File: sr_vns_comm.c 
 * Date: Spring 2002 
 * Authors: Guido Apanzeller, Vikram Vijayaraghaven, Martin Casado
 * Contact: casado@stanford.edu
 *
 * Based on many generations of sr clients including the original c client
 * and bert.
 * 
 * 2003-Dec-03 09:00:52 AM :
 *   - bug sending packets read from client to sr_log_packet.  Packet was
 *     sent in network byte order ... expected host byte order.
 *     Reported by Matt Holliman & Sam Small. /mc
 *
 *  2004-Jan-29 07:09:28 PM  
 *   - added check to handle signal interrupts on recv (for use with
 *     alarm(..) for timeouts.  Fixes are based on patch by
 *     Abhyudaya Chodisetti <sravanth@stanford.edu> /mc
 *
 *   2004-Jan-31 01:27:54 PM 
 *    - William Chan (chanman@stanford.edu) submitted patch for UMR on
 *      sr_dump_packet(..)
 *
 *---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include "sr_dumper.h"
#include "sr_router.h"
#include "sr_if.h"
#include "sr_protocol.h"

#include "vnscommand.h"


static void sr_log_packet(struct sr_instance* , uint8_t* , int );
static int  sr_arp_req_not_for_us(struct sr_instance* sr, 
                                  uint8_t * packet /* lent */,
                                  unsigned int len,
                                  char* interface  /* lent */);

/*-----------------------------------------------------------------------------
 * Method: sr_connect_to_server()
 * Scope: Global 
 *
 * Connect to the virtual server
 * 
 * RETURN VALUES:
 *
 *  0 on success 
 *  something other than zero on error
 *
 *---------------------------------------------------------------------------*/

int sr_connect_to_server(struct sr_instance* sr,unsigned short port,
                         char* server)
{
    struct hostent *hp;
    c_open command;

    /* REQUIRES */
    assert(sr);
    assert(server);

    /* purify UMR be gone ! */
    memset((void*)&command,0,sizeof(c_open));

    /* zero out server address struct */
    memset(&(sr->sr_addr),0,sizeof(struct sockaddr_in));

    sr->sr_addr.sin_family = AF_INET;
    sr->sr_addr.sin_port = htons(port);

    /* grab hosts address from domain name */
    if ((hp = gethostbyname(server))==0) 
    {
        perror("gethostbyname:sr_client.c::sr_connect_to_server(..)");
        return -1;
    }

    /* set server address */
    memcpy(&(sr->sr_addr.sin_addr),hp->h_addr,hp->h_length);

    /* create socket */
    if ((sr->sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket(..):sr_client.c::sr_connect_to_server(..)");
        return -1;
    }

    /* attempt to connect to the server */
    if (connect(sr->sockfd, (struct sockaddr *)&(sr->sr_addr), 
                sizeof(sr->sr_addr)) < 0)
    {
        perror("connect(..):sr_client.c::sr_connect_to_server(..)");
        close(sr->sockfd);
        return -1;
    }

    /* send sr_OPEN message to server */ 
    command.mLen   = htonl(sizeof(c_open));
    command.mType  = htonl(VNSOPEN);
    command.topoID = htons(sr->topo_id);
    strncpy( command.mVirtualHostID, sr->host,  IDSIZE);
    strncpy( command.mUID, sr->user, IDSIZE);

    printf("Sending c_open (type=%d len=%d)\n", htonl(command.mType), htonl(command.mLen) );

    if (send(sr->sockfd, (void *)&command, sizeof(c_open), 0) != sizeof(c_open))
    {
        perror("send(..):sr_client.c::sr_connect_to_server()");
        return -1;
    }

    return 0;
} /* -- sr_connect_to_server -- */

/*-----------------------------------------------------------------------------
 * Method: sr_handle_hwinfo(..) 
 * scope: global 
 *
 *
 * Read, from the server, the hardware information for the reserved host.
 *
 *---------------------------------------------------------------------------*/

int sr_handle_hwinfo(struct sr_instance* sr, c_hwinfo* hwinfo)
{
    int num_entries;
    int i = 0;

    /* REQUIRES */
    assert(sr);
    assert(hwinfo);

    num_entries = (ntohl(hwinfo->mLen) - (2*sizeof(uint32_t)))/sizeof(c_hw_entry);
    Debug("Received Hardware Info with %d entries\n",num_entries); 

    for ( i=0; i<num_entries; i++ )
    {
        switch( ntohl(hwinfo->mHWInfo[i].mKey))
        {
            case HWFIXEDIP:
                Debug("Fixed IP: %s\n",inet_ntoa(
                            *((struct in_addr*)(hwinfo->mHWInfo[i].value))));
                break;    
            case HWINTERFACE:    
                Debug("INTERFACE: %s\n",hwinfo->mHWInfo[i].value);
                sr_add_interface(sr,hwinfo->mHWInfo[i].value);
                break;
            case HWSPEED:    
                Debug("Speed: %d\n",
                        ntohl(*((unsigned int*)hwinfo->mHWInfo[i].value)));
                break;
            case HWSUBNET:
                Debug("Subnet: %s\n",inet_ntoa(
                            *((struct in_addr*)(hwinfo->mHWInfo[i].value))));
                break;
            case HWMASK:
                Debug("Mask: %s\n",inet_ntoa(
                            *((struct in_addr*)(hwinfo->mHWInfo[i].value))));
                sr_set_ether_mask(sr,*((uint32_t*)hwinfo->mHWInfo[i].value));
                break;
            case HWETHIP:    
                Debug("Ethernet IP: %s\n",inet_ntoa(
                            *((struct in_addr*)(hwinfo->mHWInfo[i].value))));
                sr_set_ether_ip(sr,*((uint32_t*)hwinfo->mHWInfo[i].value));
                break;
            case HWETHER:
                Debug("Hardware Address: ");
                DebugMAC(hwinfo->mHWInfo[i].value);
                Debug("\n");
                sr_set_ether_addr(sr,(unsigned char*)hwinfo->mHWInfo[i].value);
                break;
            default:
                printf (" %d \n",ntohl(hwinfo->mHWInfo[i].mKey));
        } /* -- switch -- */
    } /* -- for -- */

    sr_print_if_list(sr);

    /* flag that hardware has been initialized */
    sr->hw_init = 1;

    return num_entries;
} /* -- sr_handle_hwinfo -- */

/*-----------------------------------------------------------------------------
 * Method: sr_read_from_server(..) 
 * Scope: global 
 *
 * Houses main while loop for communicating with the virtual router server.
 *
 *---------------------------------------------------------------------------*/

int sr_read_from_server(struct sr_instance* sr /* borrowed */)
{
    int command, len;
    unsigned char *buf = 0;
    c_packet_ethernet_header* sr_pkt = 0;
    int ret = 0, bytes_read = 0;

    /* REQUIRES */
    assert(sr);

    /*---------------------------------------------------------------------------
      Read a command from the server 
      -------------------------------------------------------------------------*/

    bytes_read = 0;

    /* attempt to read the size of the incoming packet */
    while( bytes_read < 4)
    {
        do
        { /* -- just in case SIGALRM breaks recv -- */
            errno = 0; /* -- hacky glibc workaround -- */
            if((ret = recv(sr->sockfd,((uint8_t*)&len) + bytes_read, 
                            4 - bytes_read, 0)) == -1)
            {
                if ( errno == EINTR )
                { continue; }

                perror("recv(..):sr_client.c::sr_read_from_server");
                return -1;
            }
            bytes_read += ret;
        } while ( errno == EINTR); /* be mindful of signals */

    }

    len = ntohl(len);

    if ( len > 10000 || len < 0 )
    {
        fprintf(stderr,"Error: command length to large %d\n",len);
        close(sr->sockfd); 
        return -1;
    }

    if((buf = ((unsigned char*)(malloc(len)))) == 0)
    {
        fprintf(stderr,"Error: out of memory (sr_read_from_server)\n");
        return -1;
    }

    /* set first field of command since we've already read it */ 
    *((int *)buf) = htonl(len);

    bytes_read = 0;

    /* read the rest of the command */
    while ( bytes_read < len - 4)
    {
        do
        {/* -- just in case SIGALRM breaks recv -- */
            errno = 0; /* -- hacky glibc workaround -- */
            if ((ret = read(sr->sockfd, buf+4+bytes_read, len - 4 - bytes_read)) ==
                    -1)
            {
                if ( errno == EINTR )
                { continue; }
                fprintf(stderr,"Error: failed reading command body %d\n",ret);
                close(sr->sockfd); 
                return -1;
            }
            bytes_read += ret;
        } while (errno == EINTR); /* be mindful of signals */
    } 

    /* My entry for most unreadable line of code - guido */
    /* ... you win - mc                                  */
    command = *(((int *)buf)+1) = ntohl(*(((int *)buf)+1));

    switch (command)
    {
        /* -------------        VNSPACKET     -------------------- */

        case VNSPACKET:
            sr_pkt = (c_packet_ethernet_header *)buf;

            /* -- check if it is an ARP to another router if so drop   -- */
            if ( sr_arp_req_not_for_us(sr, 
                    (buf+sizeof(c_packet_header)),
                    len - sizeof(c_packet_ethernet_header) +
                    sizeof(struct sr_ethernet_hdr),
                    (char*)(buf + sizeof(c_base))) )
            { break; }

            /* -- log packet -- */
            sr_log_packet(sr, buf + sizeof(c_packet_header),
                    ntohl(sr_pkt->mLen) - sizeof(c_packet_header));

            /* -- pass to router, student's code should take over here -- */
            sr_handlepacket(sr,
                    (buf+sizeof(c_packet_header)),
                    len - sizeof(c_packet_ethernet_header) +
                    sizeof(struct sr_ethernet_hdr),
                    (char*)(buf + sizeof(c_base)));

            break;

            /* -------------        VNSCLOSE      -------------------- */

        case VNSCLOSE:
            fprintf(stderr,"vns server closed session.\n");
            fprintf(stderr,"Reason: %s\n",((c_close*)buf)->mErrorMessage);
            if(buf)
            { free(buf); }
            return 0;      
            break;

            /* -------------     VNSHWINFO     -------------------- */

        case VNSHWINFO:
            sr_handle_hwinfo(sr,(c_hwinfo*)buf); 
            if(sr_verify_routing_table(sr) != 0)
            {
                /*fprintf(stderr,"Routing table not consistent with hardware\n");
                return -1;*/
            }
            break;

        default:
            Debug("unknown command: %d\n", command);
            break;

    }/* -- switch -- */

    if(buf)
    { free(buf); }
    return 1;
}/* -- sr_read_from_server -- */

/*-----------------------------------------------------------------------------
 * Method: sr_ether_addrs_match_interface(..)
 * Scope: Local
 *
 * Make sure ethernet addresses are sane so we don't muck uo the system.
 *
 *----------------------------------------------------------------------------*/

static 
int 
sr_ether_addrs_match_interface( struct sr_instance* sr, /* borrowed */
                                uint8_t* buf, /* borrowed */
                                const char* name /* borrowed */ )
{
    struct sr_ethernet_hdr* ether_hdr = 0;
    struct sr_if* iface = 0;

    /* -- REQUIRES -- */
    assert(sr);
    assert(buf);
    assert(name);

    ether_hdr = (struct sr_ethernet_hdr*)buf;
    iface = sr_get_interface(sr, name);

    if ( iface == 0 )
    {
        fprintf( stderr, "** Error, interface %s, does not exist\n", name);
        return 0;
    }

    if ( memcmp( ether_hdr->ether_shost, iface->addr, ETHER_ADDR_LEN) != 0 )
    {
        fprintf( stderr, "** Error, source address does not match interface\n");
        return 0;
    }

    /* TODO */
    /* Check destination, hardware address.  If it is private (i.e. destined
     * to a virtual interface) ensure it is going to the correct topology
     * Note: This check should really be done server side ...
     */

    return 1;

} /* -- sr_ether_addrs_match_interface -- */

/*-----------------------------------------------------------------------------
 * Method: sr_send_packet(..)
 * Scope: Global
 *
 * Send a packet (ethernet header included!) of length 'len' to the server
 * to be injected onto the wire.
 *
 *---------------------------------------------------------------------------*/

int sr_send_packet(struct sr_instance* sr /* borrowed */, 
                         uint8_t* buf /* borrowed */ ,
                         unsigned int len, 
                         const char* iface /* borrowed */)
{
    c_packet_header *sr_pkt;
    unsigned int total_len =  len + (sizeof(c_packet_header));

    /* REQUIRES */
    assert(sr);
    assert(buf);
    assert(iface);

    /* don't waste my time ... */
    if ( len < sizeof(struct sr_ethernet_hdr) )
    {
        fprintf(stderr , "** Error: packet is way to short \n");
        return -1;
    }

    /* Create packet */
    sr_pkt = (c_packet_header *)malloc(len +
            sizeof(c_packet_header));
    assert(sr_pkt);
    sr_pkt->mLen  = htonl(total_len);
    sr_pkt->mType = htonl(VNSPACKET);
    strncpy(sr_pkt->mInterfaceName,iface,16);
    memcpy(((uint8_t*)sr_pkt) + sizeof(c_packet_header),
            buf,len);

    /* -- log packet -- */
    sr_log_packet(sr,buf,len);

    if ( ! sr_ether_addrs_match_interface( sr, buf, iface) )
    {
        fprintf( stderr, "*** Error: problem with ethernet header, check log\n");
        free ( sr_pkt );
        return -1; 
    }

    if( ((uint32_t)(write(sr->sockfd, sr_pkt, total_len))) < total_len ) 
    {
        fprintf(stderr, "Error writing packet\n");
        free(sr_pkt);
        return -1;
    }

    free(sr_pkt);

    return 0;
} /* -- sr_send_packet -- */

/*-----------------------------------------------------------------------------
 * Method: sr_log_packet()
 * Scope: Local 
 *
 *---------------------------------------------------------------------------*/

void sr_log_packet(struct sr_instance* sr, uint8_t* buf, int len )
{
    struct pcap_pkthdr h;
    int size;

    /* REQUIRES */
    assert(sr);

    if(!sr->logfile)
    {return; }

    size = min(PACKET_DUMP_SIZE, len);

    gettimeofday(&h.ts, 0);
    h.caplen = size;
    h.len = (size < PACKET_DUMP_SIZE) ? size : PACKET_DUMP_SIZE;

    sr_dump(sr->logfile, &h, buf);
    fflush(sr->logfile);
} /* -- sr_log_packet -- */

/*-----------------------------------------------------------------------------
 * Method: sr_arp_req_not_for_us()
 * Scope: Local 
 *
 *---------------------------------------------------------------------------*/

int  sr_arp_req_not_for_us(struct sr_instance* sr, 
                           uint8_t * packet /* lent */,
                           unsigned int len,
                           char* interface  /* lent */)
{
    struct sr_if* iface = sr_get_interface(sr, interface);
    struct sr_ethernet_hdr* e_hdr = 0;
    struct sr_arphdr*       a_hdr = 0;

    if (len < sizeof(struct sr_ethernet_hdr) + sizeof(struct sr_arphdr) )
    { return 0; }

    assert(iface);

    e_hdr = (struct sr_ethernet_hdr*)packet; 
    a_hdr = (struct sr_arphdr*)(packet + sizeof(struct sr_ethernet_hdr));

    if ( (e_hdr->ether_type == htons(ETHERTYPE_ARP)) &&
            (a_hdr->ar_op   == htons(ARP_REQUEST))   &&
            (a_hdr->ar_tip  != iface->ip ) )
    { return 1; }

    return 0;
} /* -- sr_arp_req_not_for_us -- */
