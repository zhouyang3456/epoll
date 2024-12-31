#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define MQ_NAME "/test_mq" // 消息队列名称

int main() {
    mqd_t mq;
    struct mq_attr attr;
    char buffer[256];
    ssize_t bytes_read;

    // 打开消息队列
    mq = mq_open(MQ_NAME, O_CREAT | O_RDONLY);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }

    // 获取消息队列属性
    if (mq_getattr(mq, &attr) == -1) {
        perror("mq_getattr");
        mq_close(mq);
        exit(EXIT_FAILURE);
    }

    printf("Message queue maxmsg: %ld, msgsize: %ld\n", attr.mq_maxmsg, attr.mq_msgsize);

    // 接收消息
    bytes_read = mq_receive(mq, buffer, attr.mq_msgsize, NULL);
    if (bytes_read == -1) {
        perror("mq_receive");
        mq_close(mq);
        exit(EXIT_FAILURE);
    }

    printf("Message received: %s\n", buffer);

    // 关闭并删除消息队列
    mq_close(mq);
    mq_unlink(MQ_NAME);

    return 0;
}
