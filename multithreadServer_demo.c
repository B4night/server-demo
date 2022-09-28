#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>

struct info {
    struct sockaddr_in addr;
    int fd;
};

void* fun(void* arg) {
    struct info* i = (struct info*)arg;
    fprintf(stdout, "client fd = %d\n", i->fd);
    char buffer[4096];
    char ip[16];
    int port = ntohs(i->addr.sin_port);
    fprintf(stdout, "client addr:%s  %d\n", inet_ntop(AF_INET, &i->addr.sin_addr.s_addr, ip, 16), 
            port);
    int n;
    while ((n = read(i->fd, buffer, 4096)) != 0) {
        buffer[n] = 0;
        fprintf(stdout, "receive from %s:%d  :  %s\n", ip, port, buffer);
        for (int i = 0; i < n; i++)
            buffer[i] = toupper(buffer[i]);
        fprintf(stdout, "send to %s:%d  :  %s\n", ip, port, buffer);
        write(i->fd, buffer, n);
    }
    close(i->fd);
    i->fd = -1;
}

void sys_err(const char* str) {
    perror(str);
    exit(1);
}

void sys_exit(int errno) {
    fprintf(stderr, "%s\n", strerror(errno));
    exit(1);
}

int main(int argc, const char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s  <port>", argv[0]);
        exit(1);
    }
    //socket bind listen accept
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1)
        sys_err("socket error");

    struct sockaddr_in addr;
    bzero(&addr, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[1]));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(lfd, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) == -1)
        sys_err("bind error");
    if (listen(lfd, 256) == -1)
        sys_err("listen error");

    struct info clients[256];
    for (int i = 0; i < 256; i++)
        clients[i].fd = -1;
    while (1) {
        int idx = 0;
        while (clients[idx].fd != -1 && idx < 256)
            idx++;
        if (idx == 256) {
            fprintf(stderr, "clients full\n");
            exit(1);
        }
        int len = sizeof(clients[idx]);
        int connfd = accept(lfd, (struct sockaddr*)&clients[idx], &len);
        if (connfd == -1)
            sys_err("accept error");
        
        clients[idx].fd = connfd;

        pthread_t tid;
        int ret = pthread_create(&tid, NULL, fun, &clients[idx]);
        if (ret != 0) {
            sys_exit(ret);
        }
        pthread_detach(tid);   
    }
    close(lfd);
    return 0;
}