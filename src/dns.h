#ifndef DNSPROXY_DNS_H
#define DNSPROXY_DNS_H

#include <stdint.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>

#define DNS_GET_QR(flags) (((flags) & 0x8000) >> 15)

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} __attribute__((packed)) dns_header_t;

typedef struct {
    char **labels;
    int len;
} domain_t;

domain_t parse_domain(const char *buffer, size_t *offset);
char *domain_to_str(const domain_t *domain);
void free_domain(domain_t domain);

#endif
