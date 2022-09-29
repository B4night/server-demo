#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>
#include <sys/epoll.h>

#define NUM 256

struct clientInfo {
    char ip[16];
    int port;
    int connfd;
};

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
    if (listen(lfd, NUM) == -1)
        sys_err("listen error");

    int efd = epoll_create(NUM);
    if (efd == -1)
        sys_err("epoll create fail");
    struct epoll_event lev;
    lev.events = EPOLLIN;
    lev.data.fd = lfd;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, lfd, &lev) == -1)
        sys_err("epoll_ctl failed");
    
    struct epoll_event events[NUM];
    struct clientInfo clients[NUM];
    memset(clients, -1, sizeof(clients));
    int maxIdx = -1;
    char buf[4096];

    while (1) {
        int num = epoll_wait(efd, events, NUM, -1);
        if (num == -1)
            sys_err("epoll_wait failed");
        for (int i = 0; i <= num; i++) {
            if (!(events[i].events & EPOLLIN))
                continue;
            if (events[i].data.fd == lfd) {
                struct sockaddr_in clientAddr;
                int len = sizeof(struct sockaddr_in);
                int connfd = accept(lfd, (struct sockaddr*)&clientAddr, &len);
                if (connfd == -1)
                    sys_err("accept failed");
                int isFull = 1;
                for (int j = 0; j < NUM; j++) {
                    if (clients[j].connfd == -1) {
                        clients[j].port = ntohs(clientAddr.sin_port);
                        inet_ntop(AF_INET, &clientAddr.sin_addr.s_addr, clients[j].ip, 16);
                        clients[j].connfd = connfd;
                        isFull = 0;
                        if (j > maxIdx)
                            maxIdx = j;
                        lev.events = EPOLLIN;
                        lev.data.fd = connfd;
                        if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &lev) == -1)
                            sys_err("epoll_ctl failed");
                        fprintf(stdout, "client addr:%s %d\n", clients[j].ip, clients[j].port);
                        break;
                    }
                }
                if (isFull) {
                    fprintf(stderr, "clients are full\n");
                    exit(1);
                }

            } else {
                int n = read(events[i].data.fd, buf, 4096);
                int idx = -1;
                for (int k = 0; k <= maxIdx; k++) {
                    if (clients[k].connfd == events[i].data.fd) {
                        idx = k;
                        break;
                    }
                }
                if (n == 0) {
                    clients[idx].connfd = -1;
                    if (idx == maxIdx)
                        while (clients[--maxIdx].connfd == -1)
                            ;
                    int tmp = events[i].data.fd;
                    if (epoll_ctl(efd, EPOLL_CTL_DEL, events[i].data.fd, NULL) == -1)
                            sys_err("epoll_ctl failed");
                    close(tmp);
                } else {
                    buf[n] = 0;
                    fprintf(stdout, "receive from %s:%d   :   %s\n", clients[idx].ip, clients[idx].port, buf);
                    for (int k = 0; k < n; k++)
                        buf[k] = toupper(buf[k]);
                    fprintf(stdout, "send to %s:%d   :   %s\n", clients[idx].ip, clients[idx].port, buf);
                    write(clients[idx].connfd, buf, n);
                }
            }
        }
    }
    close(lfd);
    close(efd);
    return 0;
}