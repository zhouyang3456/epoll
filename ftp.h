typedef enum {
    FTP_MSG_TYPE_HELLO,

    // request
    FTP_MSG_TYPE_OPEN,
    FTP_MSG_TYPE_USER,
    FTP_MSG_TYPE_PASSWD,

    // notify
    FTP_MSG_TYPE_RECVED_NOTIFY

} ftp_msg_type_e;

typedef struct ftp_msg
{
    /* data */
    ftp_msg_type_e type;
    void *param;
} ftp_msg_t;