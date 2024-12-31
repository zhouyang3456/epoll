#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>

#define MQ_NAME "/test_mq" // 消息队列名称

int main() {
    mqd_t mq;
    struct mq_attr attr;
    char msg[] = "Hello, POSIX World!";

    // 设置消息队列属性
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;    // 最大消息数
    attr.mq_msgsize = 256;  // 最大消息大小
    attr.mq_curmsgs = 0;

    // 创建消息队列
    mq = mq_open(MQ_NAME, O_CREAT | O_WRONLY, 0644, &attr);
    if (mq == (mqd_t)-1) {
        perror("mq_open");
        exit(EXIT_FAILURE);
    }

    // 发送消息
    if (mq_send(mq, msg, strlen(msg) + 1, 0) < 0) {
        perror("mq_send");
        mq_close(mq);
        exit(EXIT_FAILURE);
    }

    printf("Message sent: %s\n", msg);

    // 关闭消息队列
    mq_close(mq);

    return 0;
}
