/******************************************************************************
 ** Coypright(C) 2014-2024 Qiware technology Co., Ltd
 **
 ** 文件名: client.c
 ** 版本号: 1.0
 ** 描  述: 
 ** 作  者: # Zengyao.pang # 2017年08月22日 星期二 04时11分13秒 #
 ******************************************************************************/
#include <fcntl.h>                                                              
#include <errno.h>                                                              
#include <stdio.h>                                                              
#include <stdlib.h>                                                             
#include <unistd.h>                                                             
#include <stddef.h>                                                             
#include <string.h>                                                             
#include <strings.h>                                                            
#include <pthread.h>                                                            
#include <arpa/inet.h>                                                          
#include <sys/socket.h>                                                         
#include <netinet/in.h>                                                                                                                                                           
#include <netinet/tcp.h>    
#include <sys/epoll.h> 
#include "ring.h"
#include "comm.h"

#define MYPORT          (8787)
#define MYSERVER        ("10.58.93.1")
#define MAX_EVENTS      (20)
#define BUFFER_SIZE     (1024 * 8)
#define QUEUE_MAX       (128)
#define ROOM_ID         (1000000021)
#define TERM_ACTIVE     (0x0909)
#define EXBUF_SIZE      (1024)

#define CMD_1 1
#define CMD_2 2
#define HEART_BEAT 3 

#define fd_set_noblocking(fd) \
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)

/* > 数据缓存 */
typedef struct data_buf
{
    char *buf;
    char *curr;
    size_t size;
    size_t bytes;

}data_buf_t;

/* > epoll全文套接字 */
typedef struct 
{
    int epfd; 
    int ret_ep_events;
    struct epoll_event ev_remov;
    struct epoll_event events[MAX_EVENTS];

}client_cntx_t;

/* > 链接类型 */
typedef struct client_conn
{
    int fd;

    ring_t *send_queue;
    data_buf_t *recv_buf;

    time_t alive_time;

}client_conn_t;

/* > 读模式返回套接字 */
enum client_read_ret
{
    READ_DATA_RECEIVED = 0,
    READ_NO_DATA_RECEIVED = 1,
    READ_ERROR = 2
};

typedef struct 
{
    uint32_t cmd;
    uint32_t len;
    char extra[EXBUF_SIZE];
    char mess[0];
}conn_bin_head_t;

const char *data1 = "Hello World!!";
const char *data2 = "Hello Company...";

int client_launch(client_cntx_t *ctx, client_conn_t *c);
void client_event_proc(client_cntx_t *ctx);
int client_recv_proc(client_cntx_t *ctx, client_conn_t *c);
enum client_read_ret client_read_data(int cli_conn, data_buf_t* recvbuf, int *n);
int client_connect_access();
int client_ctx_init(client_cntx_t *ctx, client_conn_t *conn);
data_buf_t* buf_calloc();
void buf_cfree(data_buf_t* t);
void client_conn_head_ntoh(conn_bin_head_t* bh);
int client_recv_data_proc(client_cntx_t *ctx, client_conn_t *c, char *message, int cmd);
int client_package_mess(client_cntx_t *ctx, client_conn_t *c, int cmd, const char *mess);
int client_send_proc(client_cntx_t *ctx, client_conn_t *c);
int client_echo_send(int fd, data_buf_t *echo_mess);
void client_epoll_out_to_in(client_cntx_t *ctx, client_conn_t *c);
static void *client_recv_cmd(void *arg);
int client_pipe_cmd(client_cntx_t *ctx, client_conn_t *c, int cmd);


int main()
{
    /* > 初始化链接状态 */
    client_cntx_t *ctx = (client_cntx_t *)calloc(1, sizeof(client_cntx_t));
    client_conn_t *conn = (client_conn_t *)calloc(1, sizeof(client_conn_t));
    
    /* > 创建链接套接字 */
    conn->fd = client_connect_access();
    if (conn->fd < 0) {
        fprintf(stderr, "fd create err");
        return -1;
    }

    /* > 非阻塞 */
    fd_set_noblocking(conn->fd);

    /* > 初始化上下文，注册epoll */
    if (client_ctx_init(ctx, conn) < 0) {
        fprintf(stderr, "epoll create err");
        return -1;
    }
    
    srand( (unsigned)time( NULL ) );

    /* > 启动服务epoll */
    if (client_launch(ctx, conn) < 0) {
        fprintf(stderr, "server create err");
        return -1;
    }

    return 0;
}


/******************************************************************************
 **函数名称: client_launch
 **功    能: 启动epoll监听
 **输入参数:
 **     ctx: epoll上下文对象
 **       c: 连接对象
 **输出参数:
 **返    回:0:正确 -1:错误
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017.08.23 #
 ******************************************************************************/
int client_launch(client_cntx_t *ctx, client_conn_t *c)
{
    int ret;
    while (1) {
        ctx->ret_ep_events = epoll_wait(ctx->epfd, ctx->events, MAX_EVENTS, 2000);
    
        /* > 异常处理 */
        if (-1 == ctx->ret_ep_events) {
            if (EINTR == errno) {
                continue;
            }

            fprintf(stderr, "errmsg:[%d] %s!\n", errno, strerror(errno));
            return -1;

        } else if (0 == ctx->ret_ep_events) {
            /* > 发送ping包 */
            fprintf(stderr, "this is ping\n");
            ret = client_package_mess(ctx, c, HEART_BEAT, NULL);
            if (ret < 0) {
                fprintf(stderr, "ping err");
            }

            struct epoll_event ev;           
            ev.data.ptr = c;
            ev.events = EPOLLIN | EPOLLET | EPOLLOUT;

            epoll_ctl(ctx->epfd, EPOLL_CTL_MOD, c->fd, &ev);
            
            continue;
        }

        /* > 进行事件处理 */
        client_event_proc(ctx);

    }
    return 0;
}

/******************************************************************************
 **函数名称: client_package_mess
 **功    能: 打包客户端数据并发起out事件触发
 **输入参数:
 **     ctx: epoll上下文对象
 **       c: 连接对象
 **     cmd: 命令
 **    mess: 消息
 **输出参数:
 **返    回:0:正确 -1:错误
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017.08.23 #
 ******************************************************************************/
int client_package_mess(client_cntx_t *ctx, client_conn_t *c, int cmd, const char *mess) 
{

    /* > 准备发送数据 */
    char param[256];
    data_buf_t *send_buf = buf_calloc();
    if (send_buf == NULL) {
        fprintf(stderr, "send_buf alloc err");
        return -1;
    }
    conn_bin_head_t *bh = (conn_bin_head_t *)send_buf->buf;
    bh->cmd = htonl(cmd); 
    bh->len = 0;
    if (mess != NULL) {
        bh->len = htonl(strlen(mess) + 1);
        memcpy(param, mess, strlen(mess) + 1);
        strcat(send_buf->buf + sizeof(conn_bin_head_t), param);
        send_buf->size = sizeof(conn_bin_head_t) + strlen(param) + 1;
    } else {
        send_buf->size = sizeof(conn_bin_head_t);
    }


    send_buf->bytes = 0;
    send_buf->curr = send_buf->buf;
    
    /* > 放入发送队列中 */
    ring_push(c->send_queue, send_buf);

    return 0;
}

/******************************************************************************
 **函数名称: client_event_proc
 **功    能: epoll读写事件处理
 **输入参数:
 **     ctx: epoll上下文对象
 **输出参数:
 **返    回:
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017.08.23 #
 ******************************************************************************/
void client_event_proc(client_cntx_t *ctx)
{
    int ret, num = 0;
    for (num = 0; num < ctx->ret_ep_events; num++) {
        /* > 获取链接 */
        client_conn_t *c = (client_conn_t *)ctx->events[num].data.ptr;
        ctx->ev_remov = ctx->events[num];
        if (c->fd < 0) {
            fprintf(stderr, "Connection fd is closed!\n");
            continue;
        }

        if (ctx->events[num].events & EPOLLIN) {

            ret = client_recv_proc(ctx, c);  
            if (ret < 0) {
                continue;
            }

        } else if (ctx->events[num].events & EPOLLOUT) {           
            ret = client_send_proc(ctx, c);
            if (ret < 0) {
                fprintf(stderr, "send err\n");
                continue;
            }

        }
    }

    fprintf(stderr, "read and write done\n");
}

/******************************************************************************
 **函数名称: client_send_proc
 **功    能: 写事件执行函数
 **输入参数:
 **     ctx: epoll上下文对象
 **       c: 连接对象
 **输出参数:
 **返    回: 0:正确 -1:错误
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017.08.23 #
 ******************************************************************************/
int client_send_proc(client_cntx_t *ctx, client_conn_t *c)
{

    while (1) {
        data_buf_t *send_buf = ring_pop(c->send_queue);
        if (send_buf == NULL) {
            fprintf(stderr, "send_buf is null\n");
            client_epoll_out_to_in(ctx, c);
            break;
        }

        data_buf_t *tp_buf = send_buf;
        int sret = client_echo_send(c->fd, tp_buf);

        if (sret < 0) {
            free(send_buf);
            send_buf = NULL;
            return -1;
        }else if (sret == (int)tp_buf->bytes) {
            free(send_buf);
            send_buf = NULL;
            continue;
        } else {
            client_epoll_out_to_in(ctx, c);
            break;
        }
    }
    return 0;
}

/******************************************************************************
 **函数名称: client_echo_send
 **功    能: 发送消息
 **输入参数:
 **       fd: 连接套接字
 **echo_mess: 消息内容
 **输出参数:
 **返    回: 读到的字节数
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017.08.23 #
 ******************************************************************************/
int client_echo_send(int fd, data_buf_t *echo_mess)
{
    data_buf_t *echo_res = echo_mess;
    while (1) {
        int total_len = echo_res->size - echo_res->bytes;
        if (total_len == 0)
            return echo_res->bytes;
        int send_reg = send(fd, echo_res->curr, total_len, 0);
        if (send_reg < 0) {
            if (errno == EAGAIN) {
                return echo_res->bytes;
            }else if (errno == EINTR)
                continue;
            return -1;
        } else if (send_reg == 0) {
            return -1;
        }
        echo_res->bytes += send_reg;
        echo_res->curr += send_reg;
    }
    return echo_res->bytes;
}

/******************************************************************************
 **函数名称: client_epoll_out_to_in
 **功    能: 触发读事件
 **输入参数:
 **     ctx: epoll上下文对象
 **       c: 连接对象
 **输出参数:
 **返    回:
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017.08.23 #
 ******************************************************************************/
void client_epoll_out_to_in(client_cntx_t *ctx, client_conn_t *c)
{
    
    struct epoll_event ev;

    ev.data.ptr = c;
    ev.events = EPOLLIN | EPOLLET;

    epoll_ctl(ctx->epfd, EPOLL_CTL_MOD, c->fd, &ev);
}

/******************************************************************************
 **函数名称: client_recv_proc
 **功    能: 读事件执行函数
 **输入参数:
 **     ctx: epoll上下文对象
 **       c: 连接对象
 **输出参数:
 **返    回:0:正确 -1:错误
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017.08.23 #
 ******************************************************************************/
int client_recv_proc(client_cntx_t *ctx, client_conn_t *c) 
{
    int n;
    int read_fd = c->fd;
    data_buf_t* recvbuf = c->recv_buf;

    char message[1024];
    
    /* > 开始读数据 */
    while (1) {
        
        n = 0;
        enum client_read_ret read_ret = client_read_data(read_fd, recvbuf, &n);
        if (read_ret == READ_NO_DATA_RECEIVED || read_ret == READ_ERROR ) {
            fprintf(stderr, "errmsg:[%d] %s! ret:%d fd:%d\n",
                    errno, strerror(errno), read_ret, read_fd);
            return -1;
        } else if (0 == n) {
            fprintf(stderr, "Didn't recv data! fd:%d\n", read_fd);
            break;
        }

        while (1) {

            /* > buf中数据长度是否大于头的长度 */
            if (recvbuf->bytes < sizeof(conn_bin_head_t)) {
                break;
            }

            /* > 解析协议头 */
            conn_bin_head_t *p = (conn_bin_head_t *)recvbuf->curr;
            client_conn_head_ntoh(p);

            fprintf(stderr, "cmd = %d", p->cmd);
            /* > 判断消息体的完整性 */
            if (p->len < 0 || p->len > recvbuf->size) {
                return -1;
            } else if (recvbuf->bytes < (p->len + sizeof(conn_bin_head_t))) {
                break;
            }

            /* > 进行数据处理 */
            memset(message, 0, sizeof(message));
            memcpy(message, recvbuf->curr+sizeof(conn_bin_head_t), p->len);

            if(client_recv_data_proc(ctx, c, message, p->cmd) < 0){
                return -1;
            }
            /* > 缓存指针跳转 */
            recvbuf->bytes -= sizeof(conn_bin_head_t) + p->len;
            recvbuf->curr += sizeof(conn_bin_head_t) + p->len;
        }

        /* > 数据转移 */
        if (0 == recvbuf->bytes) {
            recvbuf->curr = recvbuf->buf;
            continue;
        } else if (recvbuf->bytes > 0) {
            memmove(recvbuf->buf, recvbuf->curr, recvbuf->bytes);
            recvbuf->curr = recvbuf->buf;
        }
    }
    return 0;
}

/******************************************************************************
 **函数名称: client_recv_data_proc
 **功    能: 根据接收消息处理
 **输入参数:
 **       c: 连接对象
 ** message: 消息体
 **     cmd: 命令
 **输出参数:
 **返    回:0:正确 -1:错误
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017.08.23 #
 ******************************************************************************/
int client_recv_data_proc(client_cntx_t *ctx, client_conn_t *c, char *message, int cmd)
{
    struct epoll_event ev;           
    
    if (CMD_1 == cmd) {

        fprintf(stderr, "NEW CMD[%x] and RECVDATA[%s]\n", cmd, message);

    } else if (CMD_2 == cmd) {

        fprintf(stderr, "NEW CMD[%x] and RECVDATA[%s]\n", cmd, message);

    } else if (HEART_BEAT == cmd) {

        fprintf(stderr, "NEW CMD[%x] and RECVDATA[%s]\n", cmd, message);
    }

    int tmp_num = rand()%2 + 1;
    const char * data = data1;
    int rcmd = 0;
    int ret = 0;

    switch (tmp_num) {
        case 1:
            data = data1;
            rcmd = CMD_1;
            break;
        case 2:
            data = data2;
            rcmd = CMD_2;
            break;
        default:
            return;
    }


    ret = client_package_mess(ctx, c, rcmd, data);
    if (ret < 0){
        return -1;
    }


    ev.data.ptr = c;
    ev.events = EPOLLIN | EPOLLET | EPOLLOUT;
    epoll_ctl(ctx->epfd, EPOLL_CTL_MOD, c->fd, &ev);

    return 0;
}

/******************************************************************************
 **函数名称: client_read_data
 **功    能: 读数据
 **输入参数:
 **cli_conn: 连接套接字
 ** recvbuf: 数据内存地址
 **       n: 标识位
 **输出参数:
 **返    回:
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017.08.23 #
 ******************************************************************************/
enum client_read_ret client_read_data(int cli_conn, data_buf_t* recvbuf, int *n)
{

    int len = 0;
    data_buf_t* tp_buf = recvbuf;
    enum client_read_ret ret = READ_NO_DATA_RECEIVED;
    while (1){
        len = tp_buf->size - tp_buf->bytes;

        if (len ==0 )
            return ret;

        int read_len = read(cli_conn, tp_buf->buf + tp_buf->bytes, len);
        if (read_len > 0) {
            *n += read_len;
            ret = READ_DATA_RECEIVED;
            tp_buf->bytes += read_len;
            fprintf(stdout, "Recv data length:%d\n", read_len);
            continue;
        } else if (read_len == 0) {
            fprintf(stdout, "Socket error! total:%d len:%d\n", read_len, len);
            return READ_ERROR;
        } else {
            if (errno == EAGAIN) {
                ret = READ_DATA_RECEIVED;
                return ret;
            } else if (errno == EINTR) {
                continue;
            }
        }
    }
    return READ_ERROR;
}

/******************************************************************************
 **函数名称: client_connect_access
 **功    能: 建立tcp连接
 **输入参数:
 **输出参数:
 **返    回: fd：连接套接字 -1:错误
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017.08.23 #
 ******************************************************************************/
int client_connect_access()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "socket creat err\n");
        return -1;
    }
    
    struct sockaddr_in client_addr;
    bzero(&client_addr, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = inet_addr(MYSERVER);
    client_addr.sin_port = htons(MYPORT);

    int rc = connect(fd, (struct sockaddr*)&client_addr, sizeof(client_addr));
    if (rc < 0) {
        fprintf(stderr, "connect err\n");
        return -1;
    }

    return fd;
}

/******************************************************************************
 **函数名称: client_ctx_init
 **功    能: 初始化
 **输入参数:
 **     ctx: epoll上下文对象
 **       c: 连接对象
 **输出参数:
 **返    回:0:正确 -1:错误
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017.08.23 #
 ******************************************************************************/
int client_ctx_init(client_cntx_t *ctx, client_conn_t *conn)
{
    struct epoll_event ev, pipe_ev;
    
    /* > 建立epoll */
    ctx->epfd = epoll_create(MAX_EVENTS);
    if (ctx->epfd < 0)
        return -1;

    /* > 初始化链接 */
    conn->recv_buf = buf_calloc();
    conn->send_queue = ring_creat(128);
    conn->alive_time = time(NULL);

    ev.data.ptr = conn;
    ev.events = EPOLLIN | EPOLLET;

    if (epoll_ctl(ctx->epfd, EPOLL_CTL_ADD, conn->fd, &ev) < 0) {
        return -1;
    }

    return 0;
}

/******************************************************************************
 **函数名称: buf_calloc
 **功    能: 申请读写内存
 **输入参数:
 **输出参数:
 **返    回: 内存地址
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017.08.23 #
 ******************************************************************************/

data_buf_t* buf_calloc() 
{
    int sz = BUFFER_SIZE + sizeof(data_buf_t);
    char* buf = calloc(1, sz);
    data_buf_t* t = (data_buf_t*)buf;
    t->buf = t->curr = (char*)((char*)t + sizeof(data_buf_t));
    t->size = BUFFER_SIZE;
    t->bytes = 0;

    return t;
}

/******************************************************************************
 **函数名称: buf_cfree
 **功    能: 释放读写内存
 **输入参数:
 **       t: 内存地址
 **输出参数:
 **返    回:0:正确 -1:错误
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017.08.23 #
 ******************************************************************************/
void buf_cfree(data_buf_t* t)
{
    free(t);
    t = NULL;
}

/******************************************************************************
 **函数名称: client_conn_head_ntoh
 **功    能: 网络字节流转换
 **输入参数:
 **       bh: 待转换头
 **输出参数:
 **返    回:
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017.08.23 #
 ******************************************************************************/
void client_conn_head_ntoh(conn_bin_head_t* bh)
{
    bh->cmd = ntohl(bh->cmd);
    bh->len = ntohl(bh->len);
}
