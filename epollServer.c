
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <ctype.h>

#define NUM 1024

struct clientInfo {
    int port;
    int fd;
    char ip[16];
};

void sys_err(const char* str) {
    perror(str);
    exit(1);
}

void sys_exit(int errno) {
    fprintf(stderr, "error : %s\n", strerror(errno));
    exit(1);
}

int main(int argc, const char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    } 
    int lfd;
    if ((lfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        sys_err("socker error");
    }
    
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(atoi(argv[1]));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(lfd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        sys_err("bind error");
    }
    if (listen(lfd, NUM) == -1) {
        sys_err("listen error");
    }
    //上述为正常的socket, bind. listen
    
    int epfd = epoll_create(NUM);
    if (epfd == -1)
        sys_err("epoll create failed");
    struct epoll_event event;	//event用于epoll_ctl的第四个参数
    struct epoll_event events[NUM];	//events用于epoll_wait
    event.events = EPOLLIN;
    event.data.fd = lfd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &event) != 0)	//将lfd添加到epfd红黑树上,监听事件为EPOLLIN
        sys_err("epoll_ctl error"); 
    //epoll_create, epoll_ctl, epoll_wait
    struct clientInfo clients[NUM];	//用于记录每个client的信息
    memset(clients, -1, sizeof(clients));
    char buf[4096];
    int maxIdx = -1;	//clients[0, maxIdx]有效(中间可能有无效的), clients[maxIdx + 1, ...]一定无效

    while (1) {
        int num = epoll_wait(epfd, events, NUM, -1);
        if (num == 0)  continue;
        if (num == -1) sys_err("epoll_wait failed");
        for (int rnd = 0; rnd < num; rnd++) {
            if (events[rnd].data.fd == lfd) {	//此时lfd上有EPOLLIN事件
                struct sockaddr_in clientAddr;
                int len = sizeof(clientAddr);
                int connfd = accept(lfd, (struct sockaddr*)&clientAddr, &len);
                if (connfd == -1)
                    sys_err("accept failed");
                
                int idx = 0;
                for (; idx < NUM; idx++) {	//在clients中找到一个合适的位置
                    if (clients[idx].fd == -1) {
                        break;
                    }
                }
                if (idx == NUM) {	//此时已满
                    fprintf(stderr, "clients full\n");
                    exit(1);
                }
                if (idx > maxIdx)	//记录maxIdx
                    maxIdx = idx;
                inet_ntop(AF_INET, &clientAddr.sin_addr.s_addr, clients[idx].ip, 16);	//设置clients的信息
                clients[idx].port = ntohs(clientAddr.sin_port);
                clients[idx].fd = connfd;

                event.events = EPOLLIN;	//设置event的值,将connfd加入到epfd红黑树中,监听EPOLLIN
                event.data.fd = connfd;
                if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &event) == -1)
                    sys_err("epoll_ctl failed");
                
                fprintf(stdout, "connect from %s:%d, clients idx = %d\n", clients[idx].ip, 
                        clients[idx].port, idx);

            } else {	//此时处理非lfd上的EPOLLIN事件
                int sockfd = events[rnd].data.fd;
                int idx = 0;
                for (; idx <= maxIdx; idx++)	//首先找到clients中对应的idx
                    if (clients[idx].fd == sockfd)
                        break;
                int n = read(sockfd, buf, 4096);	//读	
                if (n == 0) {	//此时断开clients[idx].fd的连接
                    fprintf(stdout, "client %s:%d disconnect\n", clients[idx].ip, clients[idx].port);
                    if (epoll_ctl(epfd, EPOLL_CTL_DEL, sockfd, NULL) == -1)
                        sys_err("epoll_ctl error");
                    close(sockfd);
                    clients[idx].fd = -1;
                    if (idx == maxIdx)	//修改maxIdx
                        while (clients[--maxIdx].fd == -1)
                            ;
                } else if (n > 0) {	//此时处理数据
                    buf[n] = 0;
                    fprintf(stdout, "recv from %s:%d  :  %s\n", clients[idx].ip, clients[idx].port, buf);

                    for (int i = 0; i < n; i++)
                        buf[i] = toupper(buf[i]);

                    fprintf(stdout, "send to %s:%d  :  %s\n", clients[idx].ip, clients[idx].port, buf);
                    write(sockfd, buf, n);
                }             
            }
        }
    }
    close(lfd);
    close(epfd);
    return 0;
}