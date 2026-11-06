#include "net.h"
#include "e1000.h"
#include "pci.h"
#include "string.h"
#include "vga.h"
#include "timer.h"
#include "kheap.h"

// Ethernet types (big-endian)
#define ETHERTYPE_ARP  0x0806
#define ETHERTYPE_IPV4 0x0800

// Protocols
#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

// TCP Flags
#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

struct eth_header {
    uint8_t  dest[6];
    uint8_t  src[6];
    uint16_t ethertype;
} __attribute__((packed));

struct arp_packet {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  sender_mac[6];
    uint32_t sender_ip;
    uint8_t  target_mac[6];
    uint32_t target_ip;
} __attribute__((packed));

struct ip_header {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed));

struct icmp_header {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed));

struct udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

struct tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed));

struct tcp_pseudo_header {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t  reserved;
    uint8_t  protocol;
    uint16_t tcp_len;
} __attribute__((packed));

struct dhcp_packet {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic_cookie;
    uint8_t  options[308];
} __attribute__((packed));

struct dns_header {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed));

static struct net_config current_net;
static bool arp_resolved = false;
static uint32_t arp_last_ip = 0;
static uint8_t arp_last_mac[6] = {0};

// Incoming TCP buffer for active connection
#define TCP_RX_BUF_SIZE 4096
static struct tcp_conn* active_tcp = NULL;
static uint8_t tcp_rx_buf[TCP_RX_BUF_SIZE];
static volatile uint16_t tcp_rx_len = 0;

// DNS response
static volatile uint32_t dns_resolved_ip = 0;
static volatile bool dns_got_response = false;
static uint16_t dns_last_id = 0;

// Ping response
static volatile bool ping_got_reply = false;
static volatile uint16_t ping_last_seq = 0;

static uint16_t ip_id_counter = 100;



static uint16_t checksum(const void* data, size_t len) {
    uint32_t sum = 0;
    const uint16_t* ptr = (const uint16_t*)data;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const uint8_t*)ptr;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, const struct tcp_header* th, const void* payload, size_t payload_len) {
    uint8_t ph[12];
    memcpy(ph, &src_ip, 4);
    memcpy(ph + 4, &dst_ip, 4);
    ph[8] = 0;
    ph[9] = IP_PROTO_TCP;
    uint16_t tcp_len = htons((uint16_t)(sizeof(struct tcp_header) + payload_len));
    memcpy(ph + 10, &tcp_len, 2);

    uint32_t sum = 0;
    const uint16_t* ptr = (const uint16_t*)ph;
    for (size_t i = 0; i < 6; i++) {
        sum += *ptr++;
    }

    ptr = (const uint16_t*)th;
    for (size_t i = 0; i < sizeof(struct tcp_header) / 2; i++) {
        sum += *ptr++;
    }

    if (payload && payload_len > 0) {
        ptr = (const uint16_t*)payload;
        size_t len = payload_len;
        while (len > 1) {
            sum += *ptr++;
            len -= 2;
        }
        if (len == 1) {
            sum += *(const uint8_t*)ptr;
        }
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

bool net_parse_ip(const char* str, uint32_t* out_ip) {
    uint32_t parts[4] = {0};
    int idx = 0;
    while (*str && idx < 4) {
        if (*str >= '0' && *str <= '9') {
            parts[idx] = parts[idx] * 10 + (*str - '0');
            if (parts[idx] > 255) return false;
        } else if (*str == '.') {
            idx++;
        } else {
            return false;
        }
        str++;
    }
    if (idx != 3) return false;
    *out_ip = MAKE_IP(parts[0], parts[1], parts[2], parts[3]);
    return true;
}

void net_ip_to_str(uint32_t ip, char* out_str) {
    uint8_t* p = (uint8_t*)&ip;
    char b0[8], b1[8], b2[8], b3[8];
    utoa(p[0], b0, 10);
    utoa(p[1], b1, 10);
    utoa(p[2], b2, 10);
    utoa(p[3], b3, 10);

    strcpy(out_str, b0);
    size_t len = strlen(out_str);
    out_str[len++] = '.'; out_str[len] = '\0';
    strcpy(out_str + len, b1);
    len = strlen(out_str);
    out_str[len++] = '.'; out_str[len] = '\0';
    strcpy(out_str + len, b2);
    len = strlen(out_str);
    out_str[len++] = '.'; out_str[len] = '\0';
    strcpy(out_str + len, b3);
}

static void send_raw_ethernet(const uint8_t* dst_mac, uint16_t ethertype, const void* payload, uint16_t len) {
    uint8_t frame[1518];
    if (sizeof(struct eth_header) + len > sizeof(frame)) return;

    struct eth_header* eth = (struct eth_header*)frame;
    memcpy(eth->dest, dst_mac, 6);
    memcpy(eth->src, current_net.mac, 6);
    eth->ethertype = htons(ethertype);

    memcpy(frame + sizeof(struct eth_header), payload, len);
    uint16_t total_len = sizeof(struct eth_header) + len;
    if (total_len < 60) {
        memset(frame + total_len, 0, 60 - total_len);
        total_len = 60;
    }

    e1000_send(frame, total_len);
    current_net.tx_packets++;
}

static void send_ip_packet(uint32_t dst_ip, uint8_t protocol, const void* payload, uint16_t payload_len) {
    uint8_t dst_mac[6];
    uint32_t next_hop = dst_ip;

    // Check if on same subnet
    if ((dst_ip & current_net.subnet) != (current_net.ip & current_net.subnet)) {
        next_hop = current_net.gateway;
    }

    if (dst_ip == 0xFFFFFFFF) {
        memset(dst_mac, 0xFF, 6);
    } else {
        if (!arp_resolve(next_hop, dst_mac, 1000)) {
            return;
        }
    }

    uint8_t buf[1500];
    struct ip_header* ip = (struct ip_header*)buf;
    ip->ver_ihl = 0x45;
    ip->tos = 0;
    ip->total_len = htons((uint16_t)(sizeof(struct ip_header) + payload_len));
    ip->id = htons(ip_id_counter++);
    ip->flags_frag = htons(0x4000); // DF
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    ip->src_ip = current_net.ip;
    ip->dst_ip = dst_ip;
    ip->checksum = checksum(ip, sizeof(struct ip_header));

    memcpy(buf + sizeof(struct ip_header), payload, payload_len);
    send_raw_ethernet(dst_mac, ETHERTYPE_IPV4, buf, sizeof(struct ip_header) + payload_len);
}

static void send_arp(uint16_t opcode, const uint8_t* target_mac, uint32_t target_ip) {
    struct arp_packet arp;
    arp.hw_type = htons(1); // Ethernet
    arp.proto_type = htons(ETHERTYPE_IPV4);
    arp.hw_len = 6;
    arp.proto_len = 4;
    arp.opcode = htons(opcode);
    memcpy(arp.sender_mac, current_net.mac, 6);
    arp.sender_ip = current_net.ip;

    if (opcode == 1) { // Request
        memset(arp.target_mac, 0x00, 6);
        uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        arp.target_ip = target_ip;
        send_raw_ethernet(bcast, ETHERTYPE_ARP, &arp, sizeof(arp));
    } else { // Reply
        memcpy(arp.target_mac, target_mac, 6);
        arp.target_ip = target_ip;
        send_raw_ethernet(target_mac, ETHERTYPE_ARP, &arp, sizeof(arp));
    }
}

bool arp_resolve(uint32_t ip, uint8_t* out_mac, uint32_t timeout_ms) {
    if (ip == 0xFFFFFFFF) {
        memset(out_mac, 0xFF, 6);
        return true;
    }
    if (arp_resolved && arp_last_ip == ip) {
        memcpy(out_mac, arp_last_mac, 6);
        return true;
    }

    arp_resolved = false;
    send_arp(1, NULL, ip);

    uint32_t start = timer_get_ticks();
    uint32_t max_ticks = (timeout_ms / 10) + 1;
    while (!arp_resolved && (timer_get_ticks() - start < max_ticks)) {
        net_poll();
    }

    if (arp_resolved && arp_last_ip == ip) {
        memcpy(out_mac, arp_last_mac, 6);
        return true;
    }
    return false;
}

static void handle_arp(const struct arp_packet* arp) {
    uint16_t opcode = ntohs(arp->opcode);
    if (opcode == 1) { // ARP Request
        if (arp->target_ip == current_net.ip) {
            send_arp(2, arp->sender_mac, arp->sender_ip);
        }
    } else if (opcode == 2) { // ARP Reply
        arp_last_ip = arp->sender_ip;
        memcpy(arp_last_mac, arp->sender_mac, 6);
        arp_resolved = true;
    }
}

static void handle_icmp(const struct ip_header* ip, const uint8_t* payload, uint16_t len) {
    if (len < sizeof(struct icmp_header)) return;
    const struct icmp_header* icmp = (const struct icmp_header*)payload;

    if (icmp->type == 8) { // Echo Request
        // Send Echo Reply (type 0)
        uint8_t reply_buf[512];
        if (len > sizeof(reply_buf)) len = sizeof(reply_buf);
        memcpy(reply_buf, payload, len);

        struct icmp_header* reply = (struct icmp_header*)reply_buf;
        reply->type = 0;
        reply->code = 0;
        reply->checksum = 0;
        reply->checksum = checksum(reply, len);

        send_ip_packet(ip->src_ip, IP_PROTO_ICMP, reply_buf, len);
    } else if (icmp->type == 0) { // Echo Reply
        ping_got_reply = true;
        ping_last_seq = ntohs(icmp->seq);
    }
}

static void handle_udp(const struct ip_header* ip, const uint8_t* payload, uint16_t len) {
    (void)ip;
    if (len < sizeof(struct udp_header)) return;
    const struct udp_header* udp = (const struct udp_header*)payload;
    uint16_t src_port = ntohs(udp->src_port);
    const uint8_t* data = payload + sizeof(struct udp_header);
    uint16_t data_len = len - sizeof(struct udp_header);

    if (src_port == 53 && data_len >= sizeof(struct dns_header)) { // DNS Response
        const struct dns_header* dns = (const struct dns_header*)data;
        if (ntohs(dns->id) == dns_last_id && ntohs(dns->ancount) > 0) {
            // Skip questions section
            const uint8_t* ptr = data + sizeof(struct dns_header);
            uint16_t qcount = ntohs(dns->qdcount);
            while (qcount-- && ptr < data + data_len) {
                while (*ptr && ptr < data + data_len) ptr += *ptr + 1;
                ptr += 5; // skip null byte, QTYPE (2), QCLASS (2)
            }

            // Read first Answer section
            if (ptr + 12 <= data + data_len) {
                // If name is compressed (0xC0..)
                if ((*ptr & 0xC0) == 0xC0) ptr += 2;
                else while (*ptr) ptr += *ptr + 1;

                uint16_t type = ntohs(*(uint16_t*)ptr); ptr += 2;
                uint16_t class = ntohs(*(uint16_t*)ptr); ptr += 2;
                ptr += 4; // skip TTL (4)
                uint16_t rdlen = ntohs(*(uint16_t*)ptr); ptr += 2;

                if (type == 1 && class == 1 && rdlen == 4) { // Type A IPv4
                    dns_resolved_ip = *(uint32_t*)ptr;
                    dns_got_response = true;
                }
            }
        }
    }
}

static void handle_tcp(const struct ip_header* ip, const uint8_t* payload, uint16_t len) {
    if (len < sizeof(struct tcp_header)) return;
    const struct tcp_header* tcp = (const struct tcp_header*)payload;

    if (!active_tcp) return;
    if (ip->src_ip != active_tcp->remote_ip) return;
    if (ntohs(tcp->dst_port) != active_tcp->local_port) return;

    uint32_t seq = ntohl(tcp->seq);
    uint32_t ack = ntohl(tcp->ack);
    uint8_t flags = tcp->flags;
    size_t header_len = (tcp->data_offset >> 4) * 4;
    size_t data_len = len - header_len;
    const uint8_t* data = payload + header_len;

    // Handshake: SYN+ACK received
    if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) == (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        active_tcp->remote_seq = seq + 1;
        active_tcp->local_seq = ack;
        active_tcp->connected = true;

        // Send ACK to complete handshake
        struct tcp_header reply;
        memset(&reply, 0, sizeof(reply));
        reply.src_port = htons(active_tcp->local_port);
        reply.dst_port = htons(active_tcp->remote_port);
        reply.seq = htonl(active_tcp->local_seq);
        reply.ack = htonl(active_tcp->remote_seq);
        reply.data_offset = (sizeof(struct tcp_header) / 4) << 4;
        reply.flags = TCP_FLAG_ACK;
        reply.window = htons(4096);
        reply.checksum = tcp_checksum(current_net.ip, active_tcp->remote_ip, &reply, NULL, 0);

        send_ip_packet(active_tcp->remote_ip, IP_PROTO_TCP, &reply, sizeof(reply));
        return;
    }

    // Data segment or FIN
    if (flags & TCP_FLAG_ACK) {
        if (data_len > 0) {
            // Append to buffer
            if (tcp_rx_len + data_len < TCP_RX_BUF_SIZE) {
                memcpy(tcp_rx_buf + tcp_rx_len, data, data_len);
                tcp_rx_len += data_len;
            }
            active_tcp->remote_seq += data_len;

            // Send ACK for data
            struct tcp_header reply;
            memset(&reply, 0, sizeof(reply));
            reply.src_port = htons(active_tcp->local_port);
            reply.dst_port = htons(active_tcp->remote_port);
            reply.seq = htonl(active_tcp->local_seq);
            reply.ack = htonl(active_tcp->remote_seq);
            reply.data_offset = (sizeof(struct tcp_header) / 4) << 4;
            reply.flags = TCP_FLAG_ACK;
            reply.window = htons(4096);
            reply.checksum = tcp_checksum(current_net.ip, active_tcp->remote_ip, &reply, NULL, 0);

            send_ip_packet(active_tcp->remote_ip, IP_PROTO_TCP, &reply, sizeof(reply));
        }

        if (flags & TCP_FLAG_FIN) {
            active_tcp->remote_seq++;
            active_tcp->closed = true;

            // Send ACK for FIN
            struct tcp_header reply;
            memset(&reply, 0, sizeof(reply));
            reply.src_port = htons(active_tcp->local_port);
            reply.dst_port = htons(active_tcp->remote_port);
            reply.seq = htonl(active_tcp->local_seq);
            reply.ack = htonl(active_tcp->remote_seq);
            reply.data_offset = (sizeof(struct tcp_header) / 4) << 4;
            reply.flags = TCP_FLAG_ACK;
            reply.window = htons(4096);
            reply.checksum = tcp_checksum(current_net.ip, active_tcp->remote_ip, &reply, NULL, 0);

            send_ip_packet(active_tcp->remote_ip, IP_PROTO_TCP, &reply, sizeof(reply));
        }
    }
}

void net_poll(void) {
    uint8_t buffer[1518];
    uint16_t len = e1000_receive(buffer, sizeof(buffer));
    if (len < sizeof(struct eth_header)) return;

    current_net.rx_packets++;
    const struct eth_header* eth = (const struct eth_header*)buffer;
    uint16_t ethertype = ntohs(eth->ethertype);

    if (ethertype == ETHERTYPE_ARP) {
        if (len >= sizeof(struct eth_header) + sizeof(struct arp_packet)) {
            handle_arp((const struct arp_packet*)(buffer + sizeof(struct eth_header)));
        }
    } else if (ethertype == ETHERTYPE_IPV4) {
        if (len >= sizeof(struct eth_header) + sizeof(struct ip_header)) {
            const struct ip_header* ip = (const struct ip_header*)(buffer + sizeof(struct eth_header));
            uint16_t ip_header_len = (ip->ver_ihl & 0x0F) * 4;
            uint16_t ip_total_len = ntohs(ip->total_len);
            if (ip_total_len <= len - sizeof(struct eth_header)) {
                const uint8_t* payload = buffer + sizeof(struct eth_header) + ip_header_len;
                uint16_t payload_len = ip_total_len - ip_header_len;

                if (ip->protocol == IP_PROTO_ICMP) {
                    handle_icmp(ip, payload, payload_len);
                } else if (ip->protocol == IP_PROTO_UDP) {
                    handle_udp(ip, payload, payload_len);
                } else if (ip->protocol == IP_PROTO_TCP) {
                    handle_tcp(ip, payload, payload_len);
                }
            }
        }
    }
}

bool dns_resolve(const char* hostname, uint32_t* out_ip, uint32_t timeout_ms) {
    if (net_parse_ip(hostname, out_ip)) {
        return true;
    }

    dns_resolved_ip = 0;
    dns_got_response = false;
    dns_last_id = (uint16_t)(timer_get_ticks() & 0xFFFF);

    uint8_t packet[512];
    struct udp_header* udp = (struct udp_header*)packet;
    struct dns_header* dns = (struct dns_header*)(packet + sizeof(struct udp_header));

    dns->id = htons(dns_last_id);
    dns->flags = htons(0x0100); // Standard query with recursion desired
    dns->qdcount = htons(1);
    dns->ancount = 0;
    dns->nscount = 0;
    dns->arcount = 0;

    // Convert hostname to DNS format (e.g. "www.example.com" -> 3www7example3com0)
    uint8_t* qname = packet + sizeof(struct udp_header) + sizeof(struct dns_header);
    const char* src = hostname;
    while (*src) {
        const char* dot = strchr(src, '.');
        uint8_t part_len = dot ? (uint8_t)(dot - src) : (uint8_t)strlen(src);
        *qname++ = part_len;
        memcpy(qname, src, part_len);
        qname += part_len;
        if (!dot) break;
        src = dot + 1;
    }
    *qname++ = 0; // Terminating zero

    *(uint16_t*)qname = htons(1); qname += 2; // QTYPE A
    *(uint16_t*)qname = htons(1); qname += 2; // QCLASS IN

    uint16_t dns_len = qname - (packet + sizeof(struct udp_header));
    udp->src_port = htons(54321);
    udp->dst_port = htons(53);
    udp->length = htons(sizeof(struct udp_header) + dns_len);
    udp->checksum = 0;

    send_ip_packet(current_net.dns, IP_PROTO_UDP, packet, sizeof(struct udp_header) + dns_len);

    uint32_t start = timer_get_ticks();
    uint32_t max_ticks = (timeout_ms / 10) + 1;
    while (!dns_got_response && (timer_get_ticks() - start < max_ticks)) {
        net_poll();
    }

    if (dns_got_response) {
        *out_ip = dns_resolved_ip;
        return true;
    }
    return false;
}

bool ping_send(uint32_t target_ip, uint32_t* rtt_ms) {
    ping_got_reply = false;
    uint16_t seq = (uint16_t)(timer_get_ticks() & 0xFFFF);
    ping_last_seq = seq;

    uint8_t buf[64];
    struct icmp_header* icmp = (struct icmp_header*)buf;
    icmp->type = 8; // Echo request
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = htons(0x4D4F); // 'MO'
    icmp->seq = htons(seq);
    memset(buf + sizeof(struct icmp_header), 0xAA, 32);
    icmp->checksum = checksum(buf, sizeof(struct icmp_header) + 32);

    uint32_t start = timer_get_ticks();
    send_ip_packet(target_ip, IP_PROTO_ICMP, buf, sizeof(struct icmp_header) + 32);

    while (!ping_got_reply && (timer_get_ticks() - start < 200)) { // 2s timeout
        net_poll();
    }

    if (ping_got_reply) {
        if (rtt_ms) *rtt_ms = (timer_get_ticks() - start) * 10;
        return true;
    }
    return false;
}

static struct tcp_conn global_tcp_conn;

struct tcp_conn* tcp_connect(uint32_t dest_ip, uint16_t dest_port, uint32_t timeout_ms) {
    active_tcp = &global_tcp_conn;
    memset(active_tcp, 0, sizeof(struct tcp_conn));
    active_tcp->remote_ip = dest_ip;
    active_tcp->remote_port = dest_port;
    active_tcp->local_port = 49152 + (timer_get_ticks() % 10000);
    active_tcp->local_seq = 1000 + timer_get_ticks();
    active_tcp->connected = false;
    active_tcp->closed = false;
    tcp_rx_len = 0;

    // Send SYN
    struct tcp_header syn;
    memset(&syn, 0, sizeof(syn));
    syn.src_port = htons(active_tcp->local_port);
    syn.dst_port = htons(active_tcp->remote_port);
    syn.seq = htonl(active_tcp->local_seq);
    syn.ack = 0;
    syn.data_offset = (sizeof(struct tcp_header) / 4) << 4;
    syn.flags = TCP_FLAG_SYN;
    syn.window = htons(4096);
    syn.checksum = tcp_checksum(current_net.ip, dest_ip, &syn, NULL, 0);

    send_ip_packet(dest_ip, IP_PROTO_TCP, &syn, sizeof(syn));

    uint32_t start = timer_get_ticks();
    uint32_t max_ticks = (timeout_ms / 10) + 1;
    while (!active_tcp->connected && (timer_get_ticks() - start < max_ticks)) {
        net_poll();
    }

    if (!active_tcp->connected) {
        active_tcp = NULL;
        return NULL;
    }

    return active_tcp;
}

bool tcp_send(struct tcp_conn* conn, const void* data, size_t len) {
    if (!conn || !conn->connected) return false;

    struct tcp_header th;
    memset(&th, 0, sizeof(th));
    th.src_port = htons(conn->local_port);
    th.dst_port = htons(conn->remote_port);
    th.seq = htonl(conn->local_seq);
    th.ack = htonl(conn->remote_seq);
    th.data_offset = (sizeof(struct tcp_header) / 4) << 4;
    th.flags = TCP_FLAG_PSH | TCP_FLAG_ACK;
    th.window = htons(4096);
    th.checksum = tcp_checksum(current_net.ip, conn->remote_ip, &th, data, len);

    uint8_t pkt[1500];
    memcpy(pkt, &th, sizeof(th));
    memcpy(pkt + sizeof(th), data, len);

    send_ip_packet(conn->remote_ip, IP_PROTO_TCP, pkt, sizeof(th) + len);
    conn->local_seq += len;
    return true;
}

int tcp_recv(struct tcp_conn* conn, void* buf, size_t max_len, uint32_t timeout_ms) {
    if (!conn) return -1;

    uint32_t start = timer_get_ticks();
    uint32_t max_ticks = (timeout_ms / 10) + 1;

    while (tcp_rx_len == 0 && !conn->closed && (timer_get_ticks() - start < max_ticks)) {
        net_poll();
    }

    if (tcp_rx_len > 0) {
        size_t copy_len = tcp_rx_len > max_len ? max_len : tcp_rx_len;
        memcpy(buf, tcp_rx_buf, copy_len);
        if (copy_len < tcp_rx_len) {
            memmove(tcp_rx_buf, tcp_rx_buf + copy_len, tcp_rx_len - copy_len);
            tcp_rx_len -= copy_len;
        } else {
            tcp_rx_len = 0;
        }
        return (int)copy_len;
    }

    if (conn->closed) return 0; // EOF
    return -1; // Timeout
}

void tcp_close(struct tcp_conn* conn) {
    if (!conn || conn->closed) return;

    struct tcp_header fin;
    memset(&fin, 0, sizeof(fin));
    fin.src_port = htons(conn->local_port);
    fin.dst_port = htons(conn->remote_port);
    fin.seq = htonl(conn->local_seq);
    fin.ack = htonl(conn->remote_seq);
    fin.data_offset = (sizeof(struct tcp_header) / 4) << 4;
    fin.flags = TCP_FLAG_FIN | TCP_FLAG_ACK;
    fin.window = htons(4096);
    fin.checksum = tcp_checksum(current_net.ip, conn->remote_ip, &fin, NULL, 0);

    send_ip_packet(conn->remote_ip, IP_PROTO_TCP, &fin, sizeof(fin));
    conn->closed = true;
    active_tcp = NULL;
}

static char* strstr_custom(const char* haystack, const char* needle) {
    size_t nlen = strlen(needle);
    while (*haystack) {
        if (strncmp(haystack, needle, nlen) == 0) {
            return (char*)haystack;
        }
        haystack++;
    }
    return NULL;
}

int curl_fetch(const char* url, bool show_headers, bool verbose) {
    // Parse URL: http://hostname[:port]/path
    char host[128] = {0};
    char path[256] = "/";
    uint16_t port = 80;

    const char* p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    }

    // Extract hostname
    int h_idx = 0;
    while (*p && *p != ':' && *p != '/' && h_idx < 127) {
        host[h_idx++] = *p++;
    }
    host[h_idx] = '\0';

    if (*p == ':') {
        p++;
        port = (uint16_t)atoi(p);
        while (*p && *p != '/') p++;
    }

    if (*p == '/') {
        strncpy(path, p, 255);
    }

    if (verbose) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        kprintf("* Connecting to %s:%u (path: %s)...\n", host, port, path);
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }

    // Resolve DNS
    uint32_t dest_ip = 0;
    if (!dns_resolve(host, &dest_ip, 3000)) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprintf("curl: could not resolve host: %s\n", host);
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return 1;
    }

    char ip_str[20];
    net_ip_to_str(dest_ip, ip_str);
    if (verbose) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        kprintf("* Resolved IP: %s\n", ip_str);
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }

    // Connect via TCP
    struct tcp_conn* conn = tcp_connect(dest_ip, port, 3000);
    if (!conn) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        kprintf("curl: failed to connect to %s:%u\n", ip_str, port);
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return 2;
    }

    if (verbose) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprintf("* Connected! Sending HTTP GET request...\n");
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }

    // Build HTTP request
    char req[512];
    strcpy(req, "GET ");
    size_t req_len = strlen(req);
    strcpy(req + req_len, path);
    req_len = strlen(req);
    strcpy(req + req_len, " HTTP/1.0\r\nHost: ");
    req_len = strlen(req);
    strcpy(req + req_len, host);
    req_len = strlen(req);
    strcpy(req + req_len, "\r\nUser-Agent: 4megaOS-curl/1.0\r\nAccept: */*\r\nConnection: close\r\n\r\n");

    tcp_send(conn, req, strlen(req));

    // Stream and print response
    char rx_chunk[512];
    bool in_headers = true;
    size_t total_bytes = 0;
    uint32_t start_time = timer_get_ticks();

    while (1) {
        int n = tcp_recv(conn, rx_chunk, sizeof(rx_chunk) - 1, 2000);
        if (n <= 0) break; // Finished or closed

        rx_chunk[n] = '\0';
        total_bytes += n;

        if (in_headers) {
            char* body_split = strstr_custom(rx_chunk, "\r\n\r\n");
            if (body_split) {
                *body_split = '\0';
                if (show_headers || verbose) {
                    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
                    kprintf("%s\r\n\r\n", rx_chunk);
                    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                }
                char* body = body_split + 4;
                kprintf("%s", body);
                in_headers = false;
            } else {
                if (show_headers || verbose) {
                    kprintf("%s", rx_chunk);
                }
            }
        } else {
            kprintf("%s", rx_chunk);
        }
    }

    tcp_close(conn);

    uint32_t elapsed_ms = (timer_get_ticks() - start_time) * 10;
    if (verbose) {
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        kprintf("\n* Received %u bytes in %u ms\n", total_bytes, elapsed_ms);
        vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    return 0;
}



void net_init(void) {
    memset(&current_net, 0, sizeof(current_net));

    struct pci_device nic;
    if (pci_find_network_card(&nic)) {
        if (e1000_init(&nic)) {
            e1000_get_mac(current_net.mac);
            current_net.has_link = true;
            // Default NAT configuration (QEMU / VirtualBox subnet)
            current_net.ip = MAKE_IP(10, 0, 2, 15);
            current_net.subnet = MAKE_IP(255, 255, 255, 0);
            current_net.gateway = MAKE_IP(10, 0, 2, 2);
            current_net.dns = MAKE_IP(10, 0, 2, 3);
            current_net.rx_packets = 0;
            current_net.tx_packets = 0;
        }
    }
}

bool net_is_available(void) {
    return current_net.has_link;
}

struct net_config* net_get_config(void) {
    return &current_net;
}

void net_print_config(void) {
    if (!current_net.has_link) {
        kprintf("No network interface detected.\n");
        return;
    }

    char ip_str[20], sub_str[20], gw_str[20], dns_str[20];
    net_ip_to_str(current_net.ip, ip_str);
    net_ip_to_str(current_net.subnet, sub_str);
    net_ip_to_str(current_net.gateway, gw_str);
    net_ip_to_str(current_net.dns, dns_str);

    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    kprintf("eth0 (Intel 82540EM Gigabit Ethernet):\n");
    vga_set_color(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    kprintf("  MAC Address : %02x:%02x:%02x:%02x:%02x:%02x\n",
            current_net.mac[0], current_net.mac[1], current_net.mac[2],
            current_net.mac[3], current_net.mac[4], current_net.mac[5]);
    kprintf("  IPv4 Address: %s\n", ip_str);
    kprintf("  Subnet Mask : %s\n", sub_str);
    kprintf("  Gateway     : %s\n", gw_str);
    kprintf("  DNS Server  : %s\n", dns_str);
    kprintf("  Packets RX  : %u | TX: %u\n", current_net.rx_packets, current_net.tx_packets);
    vga_set_color(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}
