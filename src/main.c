#include <stdio.h>
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

#define PORT 53

typedef struct {
    int sock_fd;
    char is_running;
} server_ctx_t;

static server_ctx_t ctx;

static void init_context();
static int init_server();
static void run_server();
static void cleanup_server();

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

static void init_context() {
    ctx.sock_fd = -1;
    ctx.is_running = 0;
}

static int init_server() {
    ctx.sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (ctx.sock_fd == -1) {
        fprintf(stderr, "socket creation failed with: %s\n", strerror(errno));
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    int ret = bind(ctx.sock_fd, (const struct sockaddr *)&server_addr, sizeof(server_addr));
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
            // todo: handle input
            printf("incoming request\n");
        }

        if (poll_fd.revents & (POLLERR | POLLHUP)) {
            fprintf(stderr, "socket error\n");
            ctx.is_running = 0;
            break;
        }
    }
}

static void cleanup_server() {
    if(ctx.sock_fd != -1){
        close(ctx.sock_fd);
    }
}
