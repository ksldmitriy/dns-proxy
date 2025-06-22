#include <stdio.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <time.h>
#include <toml.h>
#include <ctype.h>

#include "dns.h"

#define DNS_PORT 53
#define UDP_MESSAGE_LIMIT 512
#define BUFFER_SIZE UDP_MESSAGE_LIMIT
#define REQUEST_EXPIRES_AFTER 2000

typedef struct {
    struct sockaddr_in addr;
    socklen_t addr_len;
    uint16_t id;
    uint64_t expiration_time;

} queued_request_t;

typedef struct {
    int sock_fd;
    char *buffer;
    char is_running;
    struct sockaddr_in external_dns_addr;
    queued_request_t *queue;
    int queue_size;
} server_ctx_t;

static server_ctx_t ctx;

static char **blacklist;
static int blacklist_len;
static char *external_dns_server;
static uint8_t refuse_r_code;

static int load_config();

static int init_context();
static int init_server();
static void run_server();
static void cleanup_server();

static void process_request();
static char is_domain_allowed(domain_t *domain);

static void queue_add_request(server_ctx_t *ctx, queued_request_t *request);
static int queue_index_from_id(server_ctx_t *ctx, uint16_t id);
static void queue_delete_expired(server_ctx_t *ctx);
static void queue_delete_by_id(server_ctx_t *ctx, uint16_t id);

static uint64_t get_time_ms();
static void str_to_lower(char *str);

int main() {
    int ret = load_config();
    if (ret) {
        fprintf(stderr, "failed to load config file\n");
        cleanup_server();
        return -1;
    }

    ret = init_server();
    if (ret) {
        fprintf(stderr, "failed to initialize server\n");
        cleanup_server();
        return -1;
    }

    run_server();

    cleanup_server();

    return 0;
}

static int load_config() {
    FILE *fp = fopen("config.toml", "r");
    if (!fp) {
        fprintf(stderr, "failed to open config file\n");
        return -1;
    }

    char error_buffer[200];
    toml_table_t *conf = toml_parse_file(fp, error_buffer, sizeof(error_buffer));
    if (!conf) {
        fprintf(stderr, "failed to parse config file\n");
        return -1;
    }

    toml_datum_t dns_server_toml = toml_string_in(conf, "dns_server");
    if (!dns_server_toml.ok) {
        fprintf(stderr, "failed to parse dns_server field\n");
        toml_free(conf);
        return -1;
    }

    external_dns_server = dns_server_toml.u.s;

    toml_array_t *blacklist_toml = toml_array_in(conf, "blacklist");
    if (!blacklist_toml) {
        fprintf(stderr, "failed to parse blacklist field\n");
        toml_free(conf);
        return -1;
    }

    int len = toml_array_nelem(blacklist_toml);
    blacklist = malloc(sizeof(char *) * len);
    for (int i = 0; i < len; i++) {
        toml_datum_t domain = toml_string_at(blacklist_toml, i);
        if (!domain.ok) {
            fprintf(stderr, "failed to parse blacklist field\n");
            toml_free(conf);
            return -1;
        }

        str_to_lower(domain.u.s);
        blacklist[i] = domain.u.s;
        blacklist_len++;
    }

    toml_datum_t refuse_r_code_toml = toml_int_in(conf, "refuse_r_code");
    if (!refuse_r_code_toml.ok) {
        fprintf(stderr, "failed to parse refuse_r_code field\n");
        toml_free(conf);
        return -1;
    } else if (refuse_r_code_toml.u.i <= 0 || refuse_r_code_toml.u.i > 5) {
        fprintf(stderr, "refuse_r_code should be in range [1, 5]\n");
        toml_free(conf);
        return -1;
    }

    refuse_r_code = refuse_r_code_toml.u.i;

    printf("config file successfully loaded\n");
    printf("external dns server: %s\n", external_dns_server);
    printf("blacklist:\n");
    for (int i = 0; i < blacklist_len; i++) {
        printf("    %s\n", blacklist[i]);
    }

    free(conf);
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
    inet_pton(AF_INET, external_dns_server, &ctx.external_dns_addr.sin_addr);

    ctx.queue = 0;
    ctx.queue_size = 0;

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

    printf("server successfully initialized\n");

    return 0;
}

static void run_server() {
    ctx.is_running = 1;

    struct pollfd poll_fd;
    poll_fd.fd = ctx.sock_fd;
    poll_fd.events = POLLIN;

    printf("server is running\n");

    while (ctx.is_running) {
        int ret = poll(&poll_fd, 1, 100);
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

        queue_delete_expired(&ctx);
    }

    printf("server stopped\n");
}

static void cleanup_server() {
    if (blacklist) {
        for (int i; i < blacklist_len; i++) {
            free(blacklist);
        }

        free(blacklist);
    }

    if (external_dns_server) {
        free(external_dns_server);
    }

    if (ctx.sock_fd != -1) {
        close(ctx.sock_fd);
    }

    if (ctx.buffer) {
        free(ctx.buffer);
        ctx.buffer = 0;
    }

    if (ctx.queue) {
        free(ctx.queue);
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

    const dns_header_t *header = (const dns_header_t *)ctx.buffer;
    offsetof(dns_header_t, flags);

    if (DNS_GET_QR(header->flags) == 0) { // request
        size_t offset = sizeof(dns_header_t);

        char request_allowed = 1;
        for (int i = 0; i < ntohs(header->qd_count); i++) {
            domain_t domain = parse_domain(ctx.buffer, &offset);
            if (!is_domain_allowed(&domain)) {
                request_allowed = 0;
                free_domain(domain);
                break;
            }
            free_domain(domain);
        }

        if (request_allowed) {
            int ret = sendto(ctx.sock_fd,
                             ctx.buffer,
                             buffer_size,
                             0,
                             (const struct sockaddr *)&ctx.external_dns_addr,
                             sizeof(ctx.external_dns_addr));
            if (ret < 0) {
                fprintf(stderr, "sendto to external dns server failed with: %s", strerror(errno));
                return;
            }

            queued_request_t request;
            request.addr = client_addr;
            request.addr_len = client_addr_len;
            request.id = header->id;
            request.expiration_time = get_time_ms() + REQUEST_EXPIRES_AFTER;
            queue_add_request(&ctx, &request);
        } else {
            dns_header_t *refuse_header = create_dns_refuse_header(header->id, refuse_r_code);
            int ret = sendto(ctx.sock_fd,
                             refuse_header,
                             sizeof(dns_header_t),
                             0,
                             (const struct sockaddr *)&client_addr,
                             client_addr_len);
            if (ret < 0) {
                fprintf(stderr, "sendto to external dns server failed with: %s", strerror(errno));
                free(refuse_header);
                return;
            }
            free(refuse_header);
        }
    } else { // response
        if (memcmp(&ctx.external_dns_addr, &client_addr, client_addr_len) != 0) {
            printf("reponse from unauthorized\n");
            return;
        }

        int request_i = queue_index_from_id(&ctx, header->id);
        if (request_i < 0) {
            return;
        }

        queued_request_t *request = &ctx.queue[request_i];

        int ret = sendto(ctx.sock_fd,
                         ctx.buffer,
                         buffer_size,
                         0,
                         (const struct sockaddr *)&request->addr,
                         request->addr_len);
        if (ret < 0) {
            fprintf(stderr, "sendto to client failed with: %s", strerror(errno));
            return;
        }

        queue_delete_by_id(&ctx, header->id);
    }
}

static char is_domain_allowed(domain_t *domain) {
    char *str = domain_to_str(domain);
    str_to_lower(str);

    for (int i = 0; i < blacklist_len; i++) {
        if (strcmp(blacklist[i], str) == 0) {
            free(str);
            return 0;
        }
    }

    free(str);
    return 1;
}

static void queue_add_request(server_ctx_t *ctx, queued_request_t *request) {
    if (ctx->queue_size == 0) {
        ctx->queue_size++;
        ctx->queue = malloc(sizeof(queued_request_t));
        if (!ctx->queue) {
            fprintf(stderr, "failed to allocate memory\n");
            exit(-1);
        }
    } else {
        ctx->queue_size++;
        ctx->queue = realloc(ctx->queue, sizeof(queued_request_t) * ctx->queue_size);
        if (!ctx->queue) {
            fprintf(stderr, "failed to allocate memory\n");
            exit(-1);
        }
    }

    ctx->queue[ctx->queue_size - 1] = *request;
}

static int queue_index_from_id(server_ctx_t *ctx, uint16_t id) {
    for (int i = 0; i < ctx->queue_size; i++) {
        if (ctx->queue[i].id == id) {
            return i;
        }
    }

    return -1;
}

static void queue_delete_expired(server_ctx_t *ctx) {
    if (ctx->queue_size == 0) {
        return;
    }

    uint64_t cur_time = get_time_ms();

    int write_i = 0;
    char changed = 0;
    for (int read_i = 0; read_i < ctx->queue_size; read_i++) {
        if (ctx->queue[read_i].expiration_time > cur_time) { // keep element
            ctx->queue[write_i] = ctx->queue[read_i];
            write_i++;
        } else {
            changed = 1;
        }
    }

    if (!changed) {
        return;
    }

    ctx->queue_size = write_i;
    if (ctx->queue_size == 0) {
        free(ctx->queue);
        ctx->queue = 0;
        return;
    }

    ctx->queue = realloc(ctx->queue, sizeof(queued_request_t) * ctx->queue_size);
    if (!ctx->queue) {
        fprintf(stderr, "failed to allocate memory\n");
        exit(-1);
    }
}

static void queue_delete_by_id(server_ctx_t *ctx, uint16_t id) {
    if (ctx->queue_size == 0) {
        return;
    }

    int write_i = 0;
    char changed = 0;
    for (int read_i = 0; read_i < ctx->queue_size; read_i++) {
        if (ctx->queue[read_i].id != id) { // keep element
            ctx->queue[write_i] = ctx->queue[read_i];
            write_i++;
        } else {
            changed = 1;
        }
    }

    if (!changed) {
        return;
    }

    ctx->queue_size = write_i;
    if (ctx->queue_size == 0) {
        free(ctx->queue);
        ctx->queue = 0;
        return;
    }

    ctx->queue = realloc(ctx->queue, sizeof(queued_request_t) * ctx->queue_size);
    if (!ctx->queue) {
        fprintf(stderr, "failed to allocate memory\n");
        exit(-1);
    }
}

static uint64_t get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void str_to_lower(char *str) {
    int len = strlen(str);
    for (int i = 0; i < len; i++) {
        str[i] = tolower(str[i]);
    }
}
