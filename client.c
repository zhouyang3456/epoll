#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <pthread.h>
#include <mqueue.h>
#include <sys/stat.h>
#include "ftp.h"

#define SERVER_IP "1.94.142.116" // 服务器IP
#define SERVER_PORT 21    // 服务器端口
#define MAX_EVENTS 10        // 最大事件数
#define BUFFER_SIZE 1024     // 缓冲区大小

#define MQ_NAME "/FTP_MSG_QUEUE3" // 消息队列名称
char hello_msg[] = "Hello, FTP World!";

pthread_t ftp_task_loop_thread;
pthread_t epoll_task_loop_thread;

int ctrl_sock;
ftp_msg_type_e g_msg_type;

// 设置文件描述符为非阻塞模式
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        return -1;
    }
    return 0;
}

void* epoll_task_loop(void* arg) {
    int sockfd, epfd, nfds;
    struct sockaddr_in server_addr;
    struct epoll_event ev, events[MAX_EVENTS];
    char buffer[BUFFER_SIZE];

    // 创建套接字
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    ctrl_sock = sockfd;

    // 设置非阻塞模式
    if (set_nonblocking(sockfd) < 0) {
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // 配置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // 连接到服务器
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        if (errno != EINPROGRESS) {
            perror("connect");
            close(sockfd);
            exit(EXIT_FAILURE);
        }
    }

    // 创建epoll实例
    if ((epfd = epoll_create1(0)) < 0) {
        perror("epoll_create1");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // 注册套接字到epoll
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLERR | EPOLLHUP; // 监听读、写和边沿触发
    // ev.events = EPOLLIN | EPOLLET; // 监听读、写和边沿触发
    ev.data.fd = sockfd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) < 0) {
        perror("epoll_ctl");
        close(sockfd);
        close(epfd);
        exit(EXIT_FAILURE);
    }

    printf("Connected to server. Waiting for events...\n");

    mqd_t ftp_msg_queue;
    struct mq_attr attr;

    // 打开消息队列
    ftp_msg_queue = mq_open(MQ_NAME, O_WRONLY);
    if (ftp_msg_queue == (mqd_t)-1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }

    // 获取消息队列属性
    if (mq_getattr(ftp_msg_queue, &attr) == -1) {
        perror("mq_getattr");
        mq_close(ftp_msg_queue);
        exit(EXIT_FAILURE);
    }

    // printf("Message queue maxmsg: %ld, msgsize: %ld\n", attr.mq_maxmsg, attr.mq_msgsize);

    // 主循环
    while (1) {
        nfds = epoll_wait(epfd, events, MAX_EVENTS, -1); // 等待事件
        if (nfds < 0) {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].events & EPOLLERR || events[i].events & EPOLLHUP) {
                // 错误处理或连接关闭处理
                printf("Error or hang up\n");
                close(events[i].data.fd);
                break;
            }
            if (events[i].events & EPOLLIN) { // 可读事件
                memset(buffer, 0, BUFFER_SIZE);
                // int n = read(events[i].data.fd, buffer, BUFFER_SIZE);
                int n = recv(events[i].data.fd, buffer, sizeof(buffer), MSG_PEEK); // not remove data
                if (n <= 0) {
                    if (n == 0) {
                        printf("Server closed the connection.\n");
                    } else {
                        perror("read");
                    }
                    close(events[i].data.fd);
                    break;
                }
                // printf("Received: %s\n", buffer);
                ftp_msg_t msg = {
                    .type = FTP_MSG_TYPE_RECVED_NOTIFY,
                    .param = (void *)n
                };
                mq_send(ftp_msg_queue, (char *)&msg, sizeof(ftp_msg_t), 0);
            }
            if (events[i].events & EPOLLOUT) { // 可写事件
                int so_error;
                socklen_t len = sizeof(so_error);
                getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &so_error, &len);

                if (so_error == 0) {
                    printf("Connection established successfully.\n");
                    // 此时可以移除 EPOLLOUT 监听，只监听 EPOLLIN
                    struct epoll_event event;
                    event.events = EPOLLIN | EPOLLET | EPOLLERR | EPOLLHUP;
                    event.data.fd = sockfd;
                    epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &event);
                } else {
                    printf("Connection failed with error %d\n", so_error);
                    close(sockfd);
                    // 继续处理其他逻辑...
                }
                // printf("writable ...\r\n");
                /*
                const char *msg = "Hello, server!";
                int n = write(events[i].data.fd, msg, strlen(msg));
                if (n < 0) {
                    perror("write");
                } else {
                    printf("Sent: %s\n", msg);
                }
                // 只发送一次消息，取消写事件监听
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = sockfd;
                epoll_ctl(epfd, EPOLL_CTL_MOD, sockfd, &ev);
                */
            }
        }
    }

    // 清理资源
    close(epfd);
    close(sockfd);
}

size_t __ftp_wirte(int socket, char *data, size_t len) {
    // const char *msg = "Hello, server!";
    int n = write(socket, data, len);
    if (n < 0) {
        perror("write");
    } else {
        printf(">>> %s\n", data);
    }
    return n;
}

void __ftp_ctrl_open() {
    if (pthread_create(&epoll_task_loop_thread, NULL, epoll_task_loop, NULL) != 0) {
        perror("Failed to create epoll_task_loop thread");
        // return EXIT_FAILURE;
    }
}

void __ftp_process_reply_response(int recv_bytes) {
    char *buffer = calloc(1, recv_bytes + 1);
    int n = read(ctrl_sock, buffer, recv_bytes);
    if (n <= 0) {
        if (n == 0) {
            printf("Server closed the connection.\n");
        } else {
            perror("read");
        }
        close(ctrl_sock);
    }
    printf("<<< %s\n", buffer);

    switch (g_msg_type)
    {
    case FTP_MSG_TYPE_OPEN: {
        char *str = "USER ftptest\r\n";
        __ftp_wirte(ctrl_sock, str, strlen(str));
        g_msg_type = FTP_MSG_TYPE_USER;
        break;
    }

    case FTP_MSG_TYPE_USER: {   
        char *str = "PASS psd@12345\r\n";
        __ftp_wirte(ctrl_sock, str, strlen(str));
        g_msg_type = FTP_MSG_TYPE_PASSWD;
        break;
    }

    default:
        break;
    }

    free(buffer);
}

void* ftp_task_loop(void* arg) {
    ftp_msg_t msg;
    ssize_t bytes_read;
    mqd_t ftp_msg_queue;
    struct mq_attr attr;

    // 打开消息队列
    ftp_msg_queue = mq_open(MQ_NAME, O_RDONLY);
    if (ftp_msg_queue == (mqd_t)-1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }

    // 获取消息队列属性
    if (mq_getattr(ftp_msg_queue, &attr) == -1) {
        perror("mq_getattr");
        mq_close(ftp_msg_queue);
        exit(EXIT_FAILURE);
    }

    // printf("Message queue maxmsg: %ld, msgsize: %ld\n", attr.mq_maxmsg, attr.mq_msgsize);

    while (1)
    {
        // 接收消息
        bytes_read = mq_receive(ftp_msg_queue, (char *)&msg, sizeof(ftp_msg_t), NULL);
        if (bytes_read < 0) {
            perror("mq_receive");
            mq_close(ftp_msg_queue);
            exit(EXIT_FAILURE);
        }

        switch (msg.type)
        {
        case FTP_MSG_TYPE_HELLO:
            printf("Message received: %s\n", (char *)msg.param);
            break;

        case FTP_MSG_TYPE_OPEN:
            __ftp_ctrl_open();
            break;

        case FTP_MSG_TYPE_RECVED_NOTIFY:
            int recv_bytes = (int)msg.param;
            // printf("recv_bytes: %d\r\n", recv_bytes);
            __ftp_process_reply_response(recv_bytes);
            break;

        default:
            break;
        }
    }
}

int main(int argc, char const *argv[])
{
    mqd_t ftp_msg_queue;
    struct mq_attr attr = {
        .mq_flags = 0,
        .mq_maxmsg = 10,    // 最大消息数
        .mq_msgsize = sizeof(ftp_msg_t),  // 最大消息大小
        .mq_curmsgs = 0
    };

    // 创建消息队列
    ftp_msg_queue = mq_open(MQ_NAME, O_CREAT | O_WRONLY, 0644, &attr);
    if (ftp_msg_queue == (mqd_t)-1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }

    if (pthread_create(&ftp_task_loop_thread, NULL, ftp_task_loop, NULL) != 0) {
        perror("Failed to create ftp_task_loop thread");
        return EXIT_FAILURE;
    }

    // 发送消息
    ftp_msg_t msg = {
        .type = FTP_MSG_TYPE_HELLO,
        .param = hello_msg
    };
    mq_send(ftp_msg_queue, (char *)&msg, sizeof(ftp_msg_t), 0);

    // 发送消息
    g_msg_type = FTP_MSG_TYPE_OPEN; 
    msg.type = FTP_MSG_TYPE_OPEN,
    msg.param = NULL;
    mq_send(ftp_msg_queue, (char *)&msg, sizeof(ftp_msg_t), 0);    

    while (1)
    {
        sleep(1);
    }
    
    return 0;
}
