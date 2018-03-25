/******************************************************************************
 ** Coypright(C) 2014-2024 Qiware technology Co., Ltd
 **
 ** 文件名: le1w_svr.h
 ** 版本号: 1.0
 ** 描  述: 
 ** 作  者: # Zengyao.pang # 2017年08月15日 星期二 13时29分08秒 #
 ******************************************************************************/
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <errno.h>
#include <sys/epoll.h>
#include <ring.h>
#include <time.h>

#define MAX_EVENTS          (20000)
#define EPOLL_THREAD_NUM    (10)
#define MYPORT              (8787)
#define QUEUE               (128)
#define BUFFER_SIZE         (1024 * 8 )
#define EXBUF_SIZE          (1024)
#define CMD_1               (1)
#define CMD_2               (2)
#define HEART_BEAT          (3)
#define ADD_FD              (4)

#define fd_set_noblocking(fd) \
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK)

#define FREE(buf) {if (buf) { free(buf), buf =NULL;} }
#define le1w_set_ctx(ctx)   (g_le1w_ctx = (ctx))
#define le1w_get_ctx()      (g_le1w_ctx)

/* > 上下文 */
typedef struct
{
    int svr_fd;            

    int epfd[EPOLL_THREAD_NUM];
    struct epoll_event ev_remov;
    struct epoll_event events[EPOLL_THREAD_NUM][MAX_EVENTS];
    
    avl_tree_t *conn_tree;    /* > 所有链接消息（连接ptr地址为key） */
    list_t *timeout_list;     /* > 超时列表，后面会关闭 */

    ring_t *accept_queue[EPOLL_THREAD_NUM];  /* > accept接收队列 */
    int pipe_fd[EPOLL_THREAD_NUM][2];    /* > 管道 */

    int flag;
    pthread_mutex_t mutex;

}le1w_cntx_t;

/* > 线程参数传递 */
typedef struct 
{
    le1w_cntx_t *ctx;
    int *id;

}thread_args_t;

/* > 自定义协议头 */
typedef struct recv_head
{

    uint32_t cmd;
    uint32_t length;
    char extra[EXBUF_SIZE];

}le1w_recv_head_t;

/* > 数据接收内存 */
typedef struct data_buf
{

    char *buf;
    char *curr;
    size_t size;
    size_t bytes;

}data_buf_t;

typedef int (*conn_recv_cb_t)(void *ctx, void* c, void* idx);
typedef int (*conn_send_cb_t)(void *ctx, void* c, void* idx);

/* > 链接储存单元（平衡二叉树中储存）*/
typedef struct le1w_conn
{

    int fd;
    data_buf_t *recv_addr;
    list_t *send_list;

    time_t alive_time;   /* > 最后一次活跃时间 */

    conn_recv_cb_t recv_cb;
    conn_send_cb_t send_cb;

}le1w_conn_t;

typedef struct le1w_add_fd
{
    int fd;
}le1w_add_fd_t;

/* > 读状态返回 */
enum le1w_read_ret
{

    READ_DATA_RECEIVED = 0,
    READ_NO_DATA_RECEIVED = 1,
    READ_ERROR = 2

};

extern le1w_cntx_t *g_le1w_ctx;

int le1w_listen_port();
enum le1w_read_ret le1w_read_data (int cli_conn, data_buf_t* recvbuf, int *n);
data_buf_t* buf_alloc();
void buf_free(data_buf_t* t);
static int buf_cmp_cb(le1w_conn_t *reg1, le1w_conn_t *reg2);
int le1w_echo_send(int fd, data_buf_t *echo_mess);
void close_conn(le1w_cntx_t *ctx, le1w_conn_t *c, int *idx);
void le1w_pack_send_mess(le1w_cntx_t *ctx, le1w_conn_t *c, int cmd, int *idx);
le1w_cntx_t * le1w_ctx_init(int svr_fd);
int le1w_accept(le1w_cntx_t *ctx, int *idx);
void le1w_event_proc(le1w_cntx_t *ctx, int *id, int n);
//int le1w_launch(le1w_cntx_t *ctx);
static void *le1w_launch(void *args);
int le1w_send_proc(le1w_cntx_t *ctx, le1w_conn_t *c, int *idx);
int le1w_recv_proc(le1w_cntx_t *ctx, le1w_conn_t *c, int *idx);
int le1w_recv_data_proc(le1w_cntx_t *ctx, le1w_conn_t *c, char *message, int head_cmd, int *idx);

void le1w_creat_thread(void *(* func)(void *), void *args, void *id);
static void *le1w_beat_proc();
static int le1w_beat_check_time(le1w_conn_t *reg1, time_t now_time);
static void *le1w_accept_proc(void *args);
int le1w_add_fd(le1w_cntx_t *ctx, int idx);
