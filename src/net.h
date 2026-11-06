#ifndef NET_H
#define NET_H

#include "types.h"

// Convert byte orders
static inline uint16_t htons(uint16_t v) {
    return (v << 8) | (v >> 8);
}
static inline uint16_t ntohs(uint16_t v) {
    return (v << 8) | (v >> 8);
}
static inline uint32_t htonl(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}
static inline uint32_t ntohl(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}

#define MAKE_IP(a, b, c, d) (((uint32_t)(a)) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

struct net_config {
    uint8_t  mac[6];
    uint32_t ip;
    uint32_t subnet;
    uint32_t gateway;
    uint32_t dns;
    bool     has_link;
    uint32_t rx_packets;
    uint32_t tx_packets;
};

struct tcp_conn {
    uint32_t remote_ip;
    uint16_t remote_port;
    uint16_t local_port;
    uint32_t local_seq;
    uint32_t remote_seq;
    uint8_t  remote_mac[6];
    bool     connected;
    bool     closed;
};

void net_init(void);
bool net_is_available(void);
struct net_config* net_get_config(void);
void net_print_config(void);

// Polling loop
void net_poll(void);

// Utilities
bool net_parse_ip(const char* str, uint32_t* out_ip);
void net_ip_to_str(uint32_t ip, char* out_str);

// Protocols
bool arp_resolve(uint32_t ip, uint8_t* out_mac, uint32_t timeout_ms);
bool dhcp_request_lease(uint32_t timeout_ms);
bool dns_resolve(const char* hostname, uint32_t* out_ip, uint32_t timeout_ms);
bool ping_send(uint32_t target_ip, uint32_t* rtt_ms);

// TCP
struct tcp_conn* tcp_connect(uint32_t dest_ip, uint16_t dest_port, uint32_t timeout_ms);
bool tcp_send(struct tcp_conn* conn, const void* data, size_t len);
int tcp_recv(struct tcp_conn* conn, void* buf, size_t max_len, uint32_t timeout_ms);
void tcp_close(struct tcp_conn* conn);

// HTTP / curl
int curl_fetch(const char* url, bool show_headers, bool verbose);

#endif
