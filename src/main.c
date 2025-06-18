#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <poll.h>
#include <ctype.h>

#include "dns.h"

#define DNS_PORT 53
#define EXTERNAL_DNS_SERVER "8.8.8.8"
#define UDP_MESSAGE_LIMIT 512
#define BUFFER_SIZE UDP_MESSAGE_LIMIT

typedef struct {
    struct sockaddr_in addr;
    socklen_t addr_len;

} queued_request_t;

typedef struct {
    int sock_fd;
    char *buffer;
    char is_running;
    struct sockaddr_in external_dns_addr;
} server_ctx_t;

static server_ctx_t ctx;

static int init_context();
static int init_server();
static void run_server();
static void cleanup_server();

static void process_request();

int main() {
    int ret = init_server();
    if (ret) {
        fprintf(stderr, "failed to initialize server\n");
        cleanup_server();
        return -1;
    }

    run_server();

    cleanup_server();

    return 0;
}

static int init_context() {
    ctx.sock_fd = -1;
    ctx.buffer = malloc(BUFFER_SIZE);
    if (!ctx.buffer) {
        fprintf(stderr, "failed to allocate buffer\n");
        return -1;
    }

    ctx.is_running = 0;

    memset(&ctx.external_dns_addr, 0, sizeof(ctx.external_dns_addr));
    ctx.external_dns_addr.sin_family = AF_INET;
    ctx.external_dns_addr.sin_port = htons(DNS_PORT);
    inet_pton(AF_INET, EXTERNAL_DNS_SERVER, &ctx.external_dns_addr.sin_addr);

    return 0;
}

static int init_server() {
    int ret = init_context();
    if (ret) {
        fprintf(stderr, "failed to initialize context\n");
        return -1;
    }

    ctx.sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx.sock_fd == -1) {
        fprintf(stderr, "socket creation failed with: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(DNS_PORT);

    ret = bind(ctx.sock_fd, (const struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret) {
        fprintf(stderr, "bind failed with: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static void run_server() {
    ctx.is_running = 1;

    struct pollfd poll_fd;
    poll_fd.fd = ctx.sock_fd;
    poll_fd.events = POLLIN;

    while (ctx.is_running) {
        int ret = poll(&poll_fd, 1, 1000);
        if (ret < 0) {
            fprintf(stderr, "poll failed with: %s", strerror(errno));
            ctx.is_running = 0;
            break;
        }

        if (poll_fd.revents & POLLIN) {
            process_request();
        }

        if (poll_fd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "socket error\n");
            ctx.is_running = 0;
            break;
        }
    }
}

static void cleanup_server() {
    if (ctx.sock_fd != -1) {
        close(ctx.sock_fd);
    }

    if (ctx.buffer) {
        free(ctx.buffer);
        ctx.buffer = 0;
    }
}

static void process_request() {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    ssize_t buffer_size = recvfrom(ctx.sock_fd,
                                   ctx.buffer,
                                   UDP_MESSAGE_LIMIT,
                                   0,
                                   (struct sockaddr *)&client_addr,
                                   &client_addr_len);

    if (buffer_size < (ssize_t)sizeof(dns_header_t)) {
        return;
    }
    printf("received %d bytes\n", (int)buffer_size);

    const dns_header_t *header = (const dns_header_t *)ctx.buffer;
    offsetof(dns_header_t, flags);

    if (DNS_GET_QR(header->flags) == 0) { // request
        printf("request accepted\n");
        domain_t *domains = malloc(sizeof(domain_t *) * ntohs(header->qd_count));

        size_t offset = sizeof(dns_header_t);

        for (int i = 0; i < ntohs(header->qd_count); i++) {
            domain_t domain = parse_domain(ctx.buffer, &offset);
            domains[i] = domain;
            char *str = domain_to_str(&domain);
            if (str) {
                printf("requested domain: %s\n", str);
                free(str);
            }
        }
    } else { // response
        printf("answer accepted\n");
    }
}
