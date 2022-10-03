#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <ctype.h>
#include <fcntl.h>

#define NUM 1024

void sys_err(const char* str) {
    perror(str);
    exit(1);
}

void sys_exit(int errno) {
    fprintf(stderr, "error : %s\n", strerror(errno));
    exit(1);
}

struct my_events {
    void       *m_arg;                                   	//call_back的第三个参数
    int        m_event;                                     //EPOLLIN或EPOLLOUT,监测的事件
    int        m_fd;                                        //文件描述符
    void       (*call_back)(int fd, int event, void *arg);  //回调函数

    char       m_buf[BUFSIZ];								//每个fd都有一个buf,8192字节
    int        m_buf_len;									//buf的有效字节数
    int        m_status;                                  	//status=1说明在epfd红黑树上
    time_t     m_lasttime;                                	//最后访问时间,用于将不活跃的client剔除
};

int epfd;	//epoll_create返回值
struct my_events my_evs[NUM + 1];	//每个my_evs元素代表一个client, my_evs[NUM]代表服务器

//初始化listen fd
void initlistensocket(int ep_fd, unsigned short port);

//设置一个epoll_event
void eventset(struct my_events *my_ev, int fd, 
              void (*call_back)(int fd, int event, void *arg), void *event_arg);

//在epfd中增加一个结点
void eventadd(int ep_fd, int event, struct my_events *my_ev);

//在epfd中删去一个结点
void eventdel(int ep_fd, struct my_events *ev);

//从client接收数据
void recvdata(int client_fd, int event, void *arg);

//向client传递数据
void senddata(int client_fd, int event, void *arg);

//server(my_evs[NUM])的回调函数
void acceptconnect(int listen_fd, int event, void *arg);



int main(int argc, const char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    } 
    epfd = epoll_create(NUM);
    if (epfd == -1)
        sys_err("epoll_create error");

    initlistensocket(epfd, atoi(argv[1]));	//创建listen fd,并将其添加到epfd红黑树中,设置回调函数
    struct epoll_event events[NUM];	//用于epoll_wait

    while (1) {
        int num = epoll_wait(epfd, events, NUM, -1);
        if (num == -1)
            sys_err("epoll wait error");
        for (int rnd = 0; rnd < num; rnd++) {
            struct my_events* ev = (struct my_events*)(events[rnd].data.ptr);
            if (ev->m_event & EPOLLIN && events[rnd].events & EPOLLIN)
                ev->call_back(ev->m_fd, events[rnd].events, ev->m_arg);
            if (ev->m_event & EPOLLOUT && events[rnd].events & EPOLLOUT)
                ev->call_back(ev->m_fd, events[rnd].events, ev->m_arg); 
        }
    }

    
    return 0;
}

void initlistensocket(int ep_fd, unsigned short port) {
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1)
        sys_err("socket failed");

    int flags = fcntl(lfd, F_GETFD);	//设置lfd为非阻塞
    flags |= O_NONBLOCK;
    fcntl(lfd, F_SETFD, flags);

    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(lfd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
        sys_err("bind error");

    if (listen(lfd, NUM) == -1)
        sys_err("listen error");
    
    //eventset, eventadd
    //lfd为my_evs[NUM]
    eventset(&my_evs[NUM], lfd, acceptconnect, &my_evs[NUM]);	//设置属性
    eventadd(epfd, EPOLLIN, &my_evs[NUM]);	//添加到epfd红黑树上

}

//设置my_events的值
void eventset(struct my_events *my_ev, int fd, void (*call_back)(int fd, int event, void *arg), void *event_arg) {
    my_ev->m_fd = fd;
    my_ev->m_event = 0;	//eventadd中设置
    my_ev->call_back = call_back;
    my_ev->m_arg = event_arg;
    my_ev->m_status = 0;
    my_ev->m_lasttime = time(NULL);
    
}

void eventadd(int ep_fd, int event, struct my_events *my_ev) {
    struct epoll_event ev;
    ev.data.ptr = my_ev;
    ev.events = event;
    my_ev->m_event = event;	//设置event
    int operation;

    if (my_ev->m_status == 0) {	//判断是否在epfd红黑树上
        operation = EPOLL_CTL_ADD;
    } else {
        fprintf(stderr, "already on epoll tree\n");
        exit(1);
    }

    if (epoll_ctl(epfd, operation, my_ev->m_fd, &ev) == -1) {
        fprintf(stdout, "event add false: fd = [%d], event = [%d]\n", my_ev->m_fd, my_ev->m_event);
        exit(1);
    } else {
        fprintf(stdout, "event add success: fd = [%d], event = [%d]\n", my_ev->m_fd, my_ev->m_event);
        my_ev->m_status = 1;
    }
}

void acceptconnect(int listen_fd, int event, void *arg) {	//lfd的回调函数,在lfd上有EPOLLIN时调用
    struct sockaddr_in clientAddr;
    int len = sizeof(clientAddr);

    int connfd = accept(listen_fd, (struct sockaddr*)&clientAddr, &len);
    if (connfd == -1)
        sys_err("accept failed");

    do {
        int i;	//找到my_evs中合适的位置i
        for (i = 0; i < NUM; i++) {
            if (my_evs[i].m_status == 0)
                break;
        }
        if (i == NUM) {
            fprintf(stderr, "my_evs full\n");
            exit(1);
        }

        int flags = fcntl(connfd, F_GETFD);	//设置为非阻塞
        flags |= O_NONBLOCK;
        fcntl(connfd, F_SETFD, flags);

        eventset(&my_evs[i],  connfd, recvdata, &my_evs[i]);	//设置新连接的client,并添加到epfd红黑树中
        eventadd(epfd, EPOLLIN, &my_evs[i]);
    } while (0);

    char ip[16];
    fprintf(stdout, "client addr: %s:%d\n", inet_ntop(AF_INET, &clientAddr.sin_addr.s_addr, ip, 16), 
            ntohs(clientAddr.sin_port));
    return;
}

void recvdata(int client_fd, int event, void *arg) {	//EPOLLIN的回调函数
    struct my_events* ev = (struct my_events*)arg;
    int n = read(ev->m_fd, ev->m_buf, sizeof(ev->m_buf));

    if (n > 0) {
        ev->m_buf_len = n;
        ev->m_buf[n] = 0;
    
        //有EPOLLIN操作时,先读入数据到ev->m_buf中,然后取下该结点,设置为EPOLLOUT后添加到红黑树中
        eventdel(ev->m_fd, ev);	
        eventset(ev, ev->m_fd, senddata, ev);
        eventadd(ev->m_fd, EPOLLOUT, ev);
    } else if (n == 0) {
        //此时断开连接
        close(ev->m_fd);
        eventdel(ev->m_fd, ev);
    }

    return;
}

void eventdel(int ep_fd, struct my_events *ev) {	//从红黑树中删掉结点
    if (ev->m_status != 1)
        return;
    epoll_ctl(epfd, EPOLL_CTL_DEL, ev->m_fd, NULL);
    ev->m_status = 0;

    return;
}

void senddata(int client_fd, int event, void *arg) {	//EPOLLOUT的回调函数
    struct my_events* ev = (struct my_events*)arg;

    for (int i = 0; i < ev->m_buf_len; i++)
        ev->m_buf[i] = toupper(ev->m_buf[i]);

    int n = write(ev->m_fd, ev->m_buf, ev->m_buf_len);
    if (n > 0) {
        //此时从epfd中删掉EPOLLOUT的结点,换成EPOLLIN的结点放上去
        eventdel(epfd, ev);
        eventset(ev, ev->m_fd, recvdata, ev);
        eventadd(epfd, EPOLLIN, ev);
    } else {
        close(ev->m_fd);
        eventdel(epfd, ev);
    }
    return;
}