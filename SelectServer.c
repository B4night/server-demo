#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <ctype.h>

struct client_info {
    char ip[16];
    int port;
    int fd;
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
    if (listen(lfd, 256) == -1)
        sys_err("listen error");

    struct client_info clients[256];
    memset(clients, -1, sizeof(clients));
    int maxIdx = 0;
    fd_set rst, allset;
    FD_ZERO(&allset);
    FD_SET(lfd, &allset);
    char buf[4096];
    int upperBound = lfd;

    while (1) {
        fprintf(stdout, "upperbound = %d\n", upperBound);
        rst = allset;
        int num = select(upperBound + 1, &rst, NULL, NULL, NULL);
        if (num == -1)
            perror("select error");
        if (FD_ISSET(lfd, &rst)) {
            struct sockaddr_in clientAddr;
            int len = sizeof(clientAddr);
            int ret = accept(lfd, (struct sockaddr*)&clientAddr, &len);
            if (ret == -1)
                sys_err("accept error");
            int isFull = 1;
            for (int i = 0; i < 256; i++)
                if (clients[i].fd == -1) {
                    clients[i].fd = ret;
                    inet_ntop(AF_INET, &clientAddr.sin_addr.s_addr, clients[i].ip, 16);
                    clients[i].port = ntohs(clientAddr.sin_port);
                    FD_SET(ret, &allset);
                    isFull = 0;
                    if (i > maxIdx) {
                        maxIdx = i;
                    }
                    fprintf(stdout, "client addr %s:%d\n", clients[i].ip, clients[i].port);
                    break;
                }
            if (isFull) {
                fprintf(stderr, "clients full\n");
                exit(1);
            }
            if (ret > upperBound)
                upperBound = ret;
            if (num == 1) continue;
        }

        for (int i = 0; i <= maxIdx; i++) {
            if (FD_ISSET(clients[i].fd, &rst)) {
                int n = read(clients[i].fd, buf, 4096);
                if (n == 0) {
                    close(clients[i].fd);
                    FD_CLR(clients[i].fd, &allset);
                    if (i == maxIdx) {
                        while (clients[--maxIdx].fd == -1)
                            ;
                        int tmp = -1;
                        for (int j = 0; j <= maxIdx; j++)
                            if(clients[j].fd > tmp)
                                tmp = clients[j].fd;
                        if (tmp == -1)
                            upperBound = lfd;
                        else
                            upperBound = tmp;
                    }
                } else {
                    buf[n] = 0;
                    fprintf(stdout, "receive from %s:%d  :  %s\n", clients[i].ip, clients[i].port, buf);
                    for (int i = 0; i < n; i++)
                        buf[i] = toupper(buf[i]);
                    fprintf(stdout, "send to %s:%d  :  %s\n", clients[i].ip, clients[i].port, buf);
                    write(clients[i].fd, buf, n);
                }
            }
        }
    }

    close(lfd);
    return 0;
}