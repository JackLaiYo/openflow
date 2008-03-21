/* Copyright (C) 2007 Board of Trustees, Leland Stanford Jr. University.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* OpenFlow: protocol between controller and datapath. */

#ifndef OPENFLOW_H
#define OPENFLOW_H 1

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

/* Maximum length of a OpenFlow packet. */
#define OFP_MAXLEN (sizeof(struct ofp_data_hello) \
        + (sizeof(struct ofp_phy_port) * OFPP_MAX) + 200)

#define OFP_VERSION   1
#define OFP_MAX_TABLE_NAME_LEN 32
#define OFP_MAX_PORT_NAME_LEN  16

#define OFP_TCP_PORT  975
#define OFP_SSL_PORT  976

#define OFP_ETH_ALEN 6          /* Bytes in an Ethernet address. */

/* Port numbering.  Physical ports are numbered starting from 0. */
enum ofp_port {
    /* Maximum number of physical switch ports. */
    OFPP_MAX = 0x100,

    /* Fake output "ports". */
    OFPP_NORMAL     = 0xfffa,  /* Process with normal L2/L3 switching */
    OFPP_FLOOD      = 0xfffb,  /* All physical ports except input port and 
                                  those disabled by STP. */
    OFPP_ALL        = 0xfffc,  /* All physical ports except input port. */
    OFPP_CONTROLLER = 0xfffd,  /* Send to controller. */ 
    OFPP_LOCAL      = 0xfffe,  /* Local openflow "port". */ /* xxx Want?! */
    OFPP_NONE       = 0xffff   /* Not associated with a physical port. */
};

enum ofp_type {
    OFPT_CONTROL_HELLO,      /* 0  Controller/switch message */
    OFPT_DATA_HELLO,         /* 1  Controller/switch message */
    OFPT_PACKET_IN,          /* 2  Async message */
    OFPT_PACKET_OUT,         /* 3  Controller/switch message */
    OFPT_FLOW_MOD,           /* 4  Controller/switch message */
    OFPT_FLOW_EXPIRED,       /* 5  Async message */
    OFPT_TABLE,              /* 6  Controller/switch message */
    OFPT_PORT_MOD,           /* 7  Controller/switch message */
    OFPT_PORT_STATUS,        /* 8  Async message */
    OFPT_FLOW_STAT_REQUEST,  /* 9  Controller/switch message */
    OFPT_FLOW_STAT_REPLY,    /* 10 Controller/switch message */
    OFPT_TABLE_STAT_REQUEST, /* 11 Controller/switch message */
    OFPT_TABLE_STAT_REPLY,   /* 12 Controller/switch message */
    OFPT_PORT_STAT_REQUEST,  /* 13 Controller/switch message */
    OFPT_PORT_STAT_REPLY     /* 14 Controller/switch message */
};

/* Header on all OpenFlow packets. */
struct ofp_header {
    uint8_t version;    /* Always 1. */
    uint8_t type;       /* One of the OFPT_ constants. */
    uint16_t length;    /* Length including this ofp_header. */
    uint32_t xid;       /* Transactin id associated with this packet.
                           Replies use the same id as was in the request
                           to facilitate pairing. */
};

#define OFP_DEFAULT_MISS_SEND_LEN   128
#define OFP_MISS_SEND_LEN_UNCHANGED 0xffff

/* Flag to indicate that datapath should notify the controller of
 * expired flow entries.
 */
#define OFP_CHELLO_SEND_FLOW_EXP 0x0001

/* Controller hello (controller -> datapath). */
struct ofp_control_hello {
    struct ofp_header header;
    uint32_t version;         /* Max supported protocol version (?) */
    uint16_t flags;           
    uint16_t miss_send_len;   /* Max bytes of new flow that datapath should 
                                 send to the controller.  A value of 
                                 OFP_MISS_SEND_LEN_UNCHANGED leaves the 
                                 currently configured value unchanged. */
};

/* Capabilities supported by the datapath. */
enum ofp_capabilities {
    OFPC_FLOW_STATS   = 1 << 0, /* Flow statistics. */
    OFPC_TABLE_STATS  = 1 << 1, /* Table statistics. */
    OFPC_PORT_STATS   = 1 << 2, /* Port statistics. */
    OFPC_STP          = 1 << 3, /* 802.11d spanning tree. */
    OFPC_MULTI_PHY_TX = 1 << 4  /* Supports transmitting through multiple
                                   physical interfaces */
};

/* Flags to indicate behavior of the physical port */
enum ofp_port_flags {
    OFPPFL_NO_FLOOD  = 1 << 0, /* Do not include this port when flooding */
};

/* Features of physical ports available in a datapath. */
enum ofp_port_features {
    OFPPF_10MB_HD    = 1 << 0, /* 10 Mb half-duplex rate support. */
    OFPPF_10MB_FD    = 1 << 1, /* 10 Mb full-duplex rate support. */
    OFPPF_100MB_HD   = 1 << 2, /* 100 Mb half-duplex rate support. */
    OFPPF_100MB_FD   = 1 << 3, /* 100 Mb full-duplex rate support. */
    OFPPF_1GB_HD     = 1 << 4, /* 1 Gb half-duplex rate support. */
    OFPPF_1GB_FD     = 1 << 5, /* 1 Gb full-duplex rate support. */
    OFPPF_10GB_FD    = 1 << 6, /* 10 Gb full-duplex rate support. */
};


/* Description of a physical port */
struct ofp_phy_port {
    uint16_t port_no;
    uint8_t hw_addr[OFP_ETH_ALEN];
    uint8_t name[OFP_MAX_PORT_NAME_LEN]; /* Null-terminated */
    uint32_t flags;         /* Bitmap of "ofp_port_flags". */
    uint32_t speed;         /* Current speed in Mbps */
    uint32_t features;      /* Bitmap of supported "ofp_port_features"s. */
};

/* Datapath hello (datapath -> controller). */
struct ofp_data_hello {
    struct ofp_header header;
    uint64_t datapath_id;   /* Datapath unique ID */

    /* Table info. */
    uint32_t n_exact;       /* Max exact-match table entries. */
    uint32_t n_mac_only;    /* Max mac-only table entries. */
    uint32_t n_compression; /* Max entries compressed on service port.  */
    uint32_t n_general;     /* Max entries of arbitrary form. */

    /* Buffer limits.  A datapath that cannot buffer reports 0.*/
    uint32_t buffer_mb;     /* Space for buffering packets, in MB. */
    uint32_t n_buffers;     /* Max packets buffered at once. */

    /* Features. */
    uint32_t capabilities;  /* Bitmap of support "ofp_capabilities". */
    uint32_t actions;       /* Bitmap of supported "ofp_action_type"s. */

    /* Miscellany */
    uint16_t miss_send_len; /* Currently configured value for max bytes 
                               of new flow that datapath will send to the 
                               controller. */
    uint8_t pad[2];         /* Align to 32-bits */

    /* Port info.*/
    struct ofp_phy_port ports[0];   /* Port definitions.  The number of ports
                                      is inferred from the length field in 
                                      the header. */
};

/* What changed about the phsyical port */
enum ofp_port_reason {
    OFPPR_ADD,              /* The port was added */
    OFPPR_DELETE,           /* The port was removed */
    OFPPR_MOD               /* Some attribute of the port has changed */
};

/* A physical port has changed in the datapath */
struct ofp_port_status {
    struct ofp_header header;
    uint8_t reason;          /* One of OFPPR_* */
    uint8_t pad[3];          /* Align to 32-bits */
    struct ofp_phy_port desc;
};

/* Modify behavior of the physical port */
struct ofp_port_mod {
    struct ofp_header header;
    struct ofp_phy_port desc;
};

/* Why is this packet being sent to the controller? */
enum ofp_reason {
    OFPR_NO_MATCH,          /* No matching flow. */
    OFPR_ACTION             /* Action explicitly output to controller. */
};

/* Packet received on port (datapath -> controller). */
struct ofp_packet_in {
    struct ofp_header header;
    uint32_t buffer_id;     /* ID assigned by datapath. */
    uint16_t total_len;     /* Full length of frame. */
    uint16_t in_port;       /* Port on which frame was received. */
    uint8_t reason;         /* Reason packet is being sent (one of OFPR_*) */
    uint8_t pad;
    uint8_t data[0];        /* Ethernet frame, halfway through 32-bit word,
                               so the IP header is 32-bit aligned.  The 
                               amount of data is inferred from the length
                               field in the header.  Because of padding,
                               offsetof(struct ofp_packet_in, data) ==
                               sizeof(struct ofp_packet_in) - 2. */
};

enum ofp_action_type {
    OFPAT_OUTPUT,           /* Output to switch port. */
    OFPAT_SET_DL_VLAN,      /* VLAN. */
    OFPAT_SET_DL_SRC,       /* Ethernet source address. */
    OFPAT_SET_DL_DST,       /* Ethernet destination address. */
    OFPAT_SET_NW_SRC,       /* IP source address. */
    OFPAT_SET_NW_DST,       /* IP destination address. */
    OFPAT_SET_TP_SRC,       /* TCP/UDP source port. */
    OFPAT_SET_TP_DST        /* TCP/UDP destination port. */
};

/* An output action sends packets out 'port'.  When the 'port' is the
 * OFPP_CONTROLLER, 'max_len' indicates the max number of bytes to
 * send.  A 'max_len' of zero means the entire packet should be sent. */
struct ofp_action_output {
    uint16_t max_len;
    uint16_t port;
};

/* The VLAN id is 12-bits, so we'll use the entire 16 bits to indicate
 * special conditions.  All ones is used to indicate that no VLAN id was
 * set, or if used as an action, that the VLAN header should be
 * stripped.
 */
#define OFP_VLAN_NONE      0xffff

struct ofp_action {
    uint16_t type;                       /* One of OFPAT_* */
    union {
        struct ofp_action_output output; /* OFPAT_OUTPUT: output struct. */
        uint16_t vlan_id;                /* OFPAT_SET_DL_VLAN: VLAN id. */
        uint8_t  dl_addr[OFP_ETH_ALEN];  /* OFPAT_SET_DL_SRC/DST */
        uint32_t nw_addr;                /* OFPAT_SET_NW_SRC/DST */
        uint16_t tp;                     /* OFPAT_SET_TP_SRC/DST */
    } arg;
};

/* Send packet (controller -> datapath). */
struct ofp_packet_out {
    struct ofp_header header;
    uint32_t buffer_id;     /* ID assigned by datapath (-1 if none). */
    uint16_t in_port;       /* Packet's input port (OFPP_NONE if none). */
    uint16_t out_port;      /* Output port (if buffer_id == -1). */
    union {
        struct ofp_action actions[0]; /* buffer_id != -1 */
        uint8_t data[0];              /* buffer_id == -1 */
    } u;
};

enum ofp_flow_mod_command {
    OFPFC_ADD,              /* New flow. */
    OFPFC_DELETE,           /* Delete all matching flows. */
    OFPFC_DELETE_STRICT     /* Strictly match wildcards. */
};

/* Flow wildcards. */
enum ofp_flow_wildcards {
    OFPFW_IN_PORT  = 1 << 0,  /* Switch input port. */
    OFPFW_DL_VLAN  = 1 << 1,  /* VLAN. */
    OFPFW_DL_SRC   = 1 << 2,  /* Ethernet source address. */
    OFPFW_DL_DST   = 1 << 3,  /* Ethernet destination address. */
    OFPFW_DL_TYPE  = 1 << 4,  /* Ethernet frame type. */
    OFPFW_NW_SRC   = 1 << 5,  /* IP source address. */
    OFPFW_NW_DST   = 1 << 6,  /* IP destination address. */
    OFPFW_NW_PROTO = 1 << 7,  /* IP protocol. */
    OFPFW_TP_SRC   = 1 << 8,  /* TCP/UDP source port. */
    OFPFW_TP_DST   = 1 << 9,  /* TCP/UDP destination port. */
    OFPFW_ALL      = (1 << 10) - 1
};

/* Values below this cutoff are 802.3 packets and the two bytes
 * following MAC addresses are used as a frame length.  Otherwise, the
 * two bytes are used as the Ethernet type.
 */
#define OFP_DL_TYPE_ETH2_CUTOFF   0x0600

/* Value of dl_type to indicate that the frame does not include an
 * Ethernet type.
 */
#define OFP_DL_TYPE_NOT_ETH_TYPE  0x05ff

/* Fields to match against flows */
struct ofp_match {
    uint16_t wildcards;        /* Wildcard fields. */
    uint16_t in_port;          /* Input switch port. */
    uint8_t dl_src[OFP_ETH_ALEN]; /* Ethernet source address. */
    uint8_t dl_dst[OFP_ETH_ALEN]; /* Ethernet destination address. */
    uint16_t dl_vlan;          /* Input VLAN. */
    uint16_t dl_type;          /* Ethernet frame type. */
    uint32_t nw_src;           /* IP source address. */
    uint32_t nw_dst;           /* IP destination address. */
    uint8_t nw_proto;          /* IP protocol. */
    uint8_t pad[3];            /* Align to 32-bits */
    uint16_t tp_src;           /* TCP/UDP source port. */
    uint16_t tp_dst;           /* TCP/UDP destination port. */
};

/* Value used in "max_idle" to indicate that the entry is permanent */
#define OFP_FLOW_PERMANENT 0

/* Flow setup and teardown (controller -> datapath). */
struct ofp_flow_mod {
    struct ofp_header header;
    struct ofp_match match;      /* Fields to match */

    /* Flow actions. */
    uint16_t command;            /* One of OFPFC_*. */
    uint16_t max_idle;           /* Idle time before discarding (seconds). */
    uint32_t buffer_id;          /* Buffered packet to apply to (or -1). */
    uint32_t group_id;           /* Flow group ID (for QoS). */
    struct ofp_action actions[0]; /* The number of actions is inferred from
                                    the length field in the header. */
};

/* Flow expiration (datapath -> controller). */
struct ofp_flow_expired {
    struct ofp_header header;
    struct ofp_match match;   /* Description of fields */

    uint32_t duration;        /* Time flow was alive in seconds. */
    uint64_t packet_count;    
    uint64_t byte_count;
};

/* Statistics about flows that match the "match" field */
struct ofp_flow_stats {
    struct ofp_match match;   /* Description of fields */
    uint32_t duration;        /* Time flow has been alive in seconds.  Only 
                                 used for non-aggregated results. */
    uint64_t packet_count;    
    uint64_t byte_count;
};

enum {
    OFPFS_INDIV,              /* Send an entry for each matching flow */
    OFPFS_AGGREGATE           /* Aggregate matching flows */
};

/* Current flow statistics request */
struct ofp_flow_stat_request {
    struct ofp_header header;
    struct ofp_match match;   /* Fields to match */
    uint8_t type;             /* One of OFPFS_ */
    uint8_t pad[3];           /* Align to 32-bits */
};

/* Current flow statistics reply */
struct ofp_flow_stat_reply {
    struct ofp_header header;

    /* If request was of type OFPFS_INDIV, this will contain an array of
     * flow statistic entries.  The number of matching flows is likely
     * much larger than can fit in a single OpenFlow message, so a
     * a response with no flows included is sent to indicate the end.
     * If it was a OFPFS_AGGREGATE request, only a single flow stats 
     * entry will be contained in the response.
     */
    struct ofp_flow_stats flows[0];  
};

/* Table attributes collected at runtime */
struct ofp_table {
    struct ofp_header header;
    char              name[OFP_MAX_TABLE_NAME_LEN];
    uint16_t          table_id;
    unsigned long int n_flows;
    unsigned long int max_flows;
};

#endif /* openflow.h */