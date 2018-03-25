#include "avl_tree.h"
#include "list.h"
#include "le1w_svr.h"
#include "comm.h"

le1w_cntx_t *g_le1w_ctx = NULL;

/* > main */
int main()
{
    le1w_cntx_t *ctx;
    int i, thread_id[EPOLL_THREAD_NUM];

    //signal(SIGINT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    /* > 创建套接字 */
    int svr_fd = le1w_listen_port();
    if (svr_fd < 0) {
        return -1;
    }

    /* > 设定非阻塞 */ 
    if (fd_set_noblocking(svr_fd) < 0) {
        return -1;
    }
    
    /* > 初始化上下文 */
    ctx = le1w_ctx_init(svr_fd);

    /* > 启动accept线程 */
    le1w_creat_thread(le1w_accept_proc, (void *)ctx, (void *)&thread_id);

    /* > 启动心跳检测线程 */
    le1w_creat_thread(le1w_beat_proc, NULL, (void *)&thread_id);

    /* > 启动服务线程 */
    for (i = 0; i < EPOLL_THREAD_NUM; i++){
        thread_id[i] = i;
        le1w_creat_thread(le1w_launch, (void *)ctx, (void *)&thread_id[i]);
    }

    while(1) {
        sleep(1000);
    }

    return 0;
}

/******************************************************************************
 **函数名称: le1w_creat_thread
 **功    能: 启动线程
 **输入参数: 
 **     func: 函数指针
 **     args：参数
 **     id  ：线程id
 **输出参数: 
 **返    回: 
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
void le1w_creat_thread(void *(* func)(void *), void *args, void *id)
{                                                                                  
    int ret;
    thread_args_t *param = (thread_args_t *)calloc(1, sizeof(thread_args_t));
    param->ctx = (le1w_cntx_t *)args;
    param->id = (int *)id;

    pthread_t thread; 
    pthread_attr_t attr;
    pthread_attr_init(&attr);

    if ((ret = pthread_create(&thread, &attr, func, (void *)param)) != 0) {                  
        fprintf(stderr, "Can't create thread: %s\n",                               
            strerror(ret)); 
        exit(1); 
    }
}

/******************************************************************************
 **函数名称:le1w_accept_proc 
 **功    能: 接收线程, 监听客户端连接
 **输入参数: 
 **     args：参数
 **输出参数: 
 **返    回: 
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
static void *le1w_accept_proc(void *args)
{
    thread_args_t *param = (thread_args_t *)args;
    le1w_cntx_t *ctx = param->ctx;
    fd_set rdset;
    struct timeval tv;
    int max, ret;

    while (1) {
        FD_ZERO(&rdset);
        FD_SET(ctx->svr_fd, &rdset);
        max = ctx->svr_fd;
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        ret = select(max + 1, &rdset, NULL, NULL, &tv);
        if (ret < 0) {
            if (EINTR == errno) { continue; }
            fprintf(stderr, "select < 0");
            return NULL;
        } else if (ret == 0) {
            fprintf(stderr, "select timeout");
            continue;
        }

        if (FD_ISSET(ctx->svr_fd, &rdset)) {
            fprintf(stderr, "select get cmd\n");
            le1w_to_accept(ctx);
        }
    }
}

/******************************************************************************
 **函数名称:le1w_to_accept 
 **功    能: 接收线程, 监听客户端连接
 **输入参数: 
 **     ctx：全局变量
 **输出参数: 
 **返    回: 
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
int le1w_to_accept(le1w_cntx_t *ctx){

    int opt = 1, idx;
    struct sockaddr_in client_addr;
    socklen_t length = sizeof(client_addr);
    int cli_fd = accept(ctx->svr_fd, (struct sockaddr*)&client_addr, &length);
    
    //setsockopt(cli_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int)); /* 解决: 端口被占用的问题 */
    fprintf(stderr, "the accept_fd is [%d]\n", cli_fd);

    if (cli_fd < 0) {
        fprintf(stderr, "errmsg:[%d] %s!\n", errno, strerror(errno));
        return -1;
    }

    le1w_add_fd_t *add = (le1w_add_fd_t *)calloc(1, sizeof(le1w_add_fd_t));
    add->fd = cli_fd;

    idx = cli_fd % EPOLL_THREAD_NUM;
    fprintf(stderr, "queue's idx = %d\n", idx);
    ring_push(ctx->accept_queue[idx], add);
    fprintf(stderr, "accept_queue insert good\n");

    /* > 发送add_fd请求 */
    if (le1w_add_fd(ctx, idx) < 0 ) {
        fprintf(stderr, "le1w_add_fd\n");
        return -1;
    }

    return 0;
}

/******************************************************************************
 **函数名称:le1w_add_fd 
 **功    能: 向工作线程发送add-fd指令
 **输入参数: 
 **     ctx：全局变量
 **输出参数: 
 **返    回: 
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
int le1w_add_fd(le1w_cntx_t *ctx, int idx) {
    
    le1w_recv_head_t *head = (le1w_recv_head_t *)calloc(1, sizeof(le1w_recv_head_t));
    head->cmd = htonl(ADD_FD);
    head->length = htonl(0);

    if (write(ctx->pipe_fd[idx][1], head, sizeof(le1w_recv_head_t)) < 0) {
        fprintf(stderr, "fd send err\n");
        return -1;
    }
    fprintf(stderr, "send join good\n");

    return 0;
}

/******************************************************************************
 **函数名称:le1w_to_accept 
 **功    能: 向工作线程发送add-fd指令
 **输入参数: 
 **     ctx：全局变量
 **输出参数: 
 **返    回: 
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
static void *le1w_beat_proc()
{
    
    while(1){
        le1w_cntx_t *ctx = le1w_get_ctx();
        time_t now_time = time(NULL);

        /* > 遍历ctx->conn_tree, 检测时间超时 */
        if (avl_trav(ctx->conn_tree, (trav_cb_t)le1w_beat_check_time, (void *)&now_time)) {
            printf("conn_tree trav error");
            return;
        }

        /* > 遍历close_list关闭连接 */
        ctx = le1w_get_ctx();

        if (list_empty(ctx->timeout_list)) {
            return;
        }

        while (1) {

            time_t ntime = time(NULL);

            le1w_conn_t *c = list_lpop(ctx->timeout_list);
            if (c == NULL) {
                break;
            }
        
            if (ntime - c->alive_time > 10) {
                close_conn(ctx, c, idx);
            }
        }

        sleep(5);
    }
}

/******************************************************************************
 **函数名称: le1w_beat_check_time
 **功    能: 检测超时链接
 **输入参数: 
 **     reg1:链接
 **     now_time：当前时间
 **输出参数: 
 **返    回: >0:成功  -1:错误
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
static int le1w_beat_check_time(le1w_conn_t *reg1, time_t now_time)
{
    le1w_cntx_t *ctx = le1w_get_ctx();
    
    /* > 如果时间大于10s，说明连接断网超时，加入超时列表 */
    if (now_time - reg1->alive_time > 10) {
        list_rpush(ctx->timeout_list, reg1);
    }

    return 0;
}

/******************************************************************************
 **函数名称: le1w_launch
 **功    能: 启动服务
 **输入参数: ctx：上下文
 **输出参数: 
 **返    回: >0:成功  -1:错误
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
static void *le1w_launch(void *args)
{
    thread_args_t *param = (thread_args_t *)args;
    le1w_cntx_t *ctx = param->ctx;
    int *idx = param->id;
    int ret_ep_events;

    /* > 等待事件通知 */
    while (1) {
        fprintf(stderr, "begin to epoll_wait[%d]\n", *idx);
        ret_ep_events = epoll_wait(ctx->epfd[*idx], ctx->events[*idx], MAX_EVENTS, -1);
        
        /* > 异常处理 */
        if (-1 == ret_ep_events) {
            if (EINTR == errno) {
                continue;
            }
            fprintf(stderr, "errmsg:[%d] %s!\n", errno, strerror(errno));
            return (void *)-1;
        } else if (0 == ret_ep_events) {
            continue;
        }

        /* > 进行事件处理 */
        le1w_event_proc(ctx, idx, ret_ep_events);
    }
    return (void *)0;
}

/******************************************************************************
 **函数名称: le1w_event_proc
 **功    能: 进行事件处理
 **输入参数: ctx：上下文
 **输出参数: 
 **返    回: 
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
void le1w_event_proc(le1w_cntx_t *ctx, int *idx, int ret_ep_events){

    int ret, num = 0;
    for (num = 0; num < ret_ep_events; num++) {
        
        /* > 获取链接 */
        le1w_conn_t *c = (le1w_conn_t *)ctx->events[*idx][num].data.ptr;
        ctx->ev_remov = ctx->events[*idx][num];
        if (c->fd < 0) {
            fprintf(stderr, "Connection fd is closed!\n");
            continue;
        }

        if (ctx->events[*idx][num].events & EPOLLIN) {
            /* > 可读事件事件处理回调 */
            fprintf(stderr, "Read data! fd:%d ret_ep_events:%d\n", c->fd, ret_ep_events);
            ret = c->recv_cb(ctx, c, idx);
            if (ret < 0) {
                close_conn(ctx, c, idx);
                continue;
            }

        } else if (ctx->events[*idx][num].events & EPOLLOUT) {
            /* > 可写事件 */
            ret = c->send_cb(ctx, c, idx);
            if (ret < 0) {
                close_conn(ctx, c, idx);
                continue;
            }
        }
    }
}

/******************************************************************************
 **函数名称: le1w_send_proc
 **功    能: 处理可写事件
 **输入参数: ctx：上下文 c: 事件连接
 **输出参数: 
 **返    回: >0:成功  -1:错误
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
int le1w_send_proc(le1w_cntx_t *ctx, le1w_conn_t *c, int *idx){
    
    if (list_empty(c->send_list)) {
        return -1;
    }

    while (1) {

        data_buf_t *send_buf = list_fetch(c->send_list, 0);
        if (send_buf == NULL)
            break;

        //printf("begin to send (%s)\n", send_buf + 1);
        data_buf_t * tp_buf = send_buf;

        int sret = le1w_echo_send(c->fd, tp_buf);
        if (sret < 0) {
            list_lpop(c->send_list);
            FREE(send_buf);
            return -1;
        } else if (sret == tp_buf->size) {
            struct epoll_event ev;

            list_lpop(c->send_list);
            FREE(send_buf);

            ev.data.ptr = c;
            ev.events = EPOLLIN | EPOLLET;

            epoll_ctl(ctx->epfd[*idx], EPOLL_CTL_MOD, c->fd, &ev);
            break;
        } else {
            break;
        }
    }
    return 0;
}

/******************************************************************************
 **函数名称: le1w_listen_port
 **功    能: 创建监听对象
 **输入参数: NONE
 **输出参数: NONE
 **返    回: >0:帧听fd -1:错误
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
int le1w_listen_port()
{

    int opt = 1;
    struct sockaddr_in svr_sockaddr;
    
    int svr_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr_fd < 0) {
        return -1;
    }

    svr_sockaddr.sin_family = AF_INET;
    svr_sockaddr.sin_port = htons(MYPORT);
    svr_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    setsockopt(svr_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));

    if ((bind(svr_fd, (struct sockaddr *)&svr_sockaddr, sizeof(svr_sockaddr)) == -1)) {
        return -1;
    }

    if (listen(svr_fd, QUEUE) < 0) {
        close(svr_fd);
        return -1;
    }

    return svr_fd;
}

/******************************************************************************
 **函数名称: le1w_ctx_init
 **功    能: 初始化上下文
 **输入参数: svr_fd:监听fd
 **输出参数: NONE
 **返    回: ctx:上下文
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
le1w_cntx_t * le1w_ctx_init(int svr_fd)
{
    int i, ret;

    le1w_cntx_t *ctx = (le1w_cntx_t *)calloc(1, sizeof(le1w_cntx_t));

    ctx->svr_fd = svr_fd;

    /* > 建立epoll */
    for (i = 0; i < EPOLL_THREAD_NUM; ++i) {
        ctx->epfd[i] = epoll_create(MAX_EVENTS);
        if (ctx->epfd[i] < 0)
            return NULL;
    }
    ctx->conn_tree = avl_creat(NULL, (cmp_cb_t)buf_cmp_cb);
    ctx->timeout_list = list_creat(NULL);

    for (i = 0; i< EPOLL_THREAD_NUM; ++i) {
        ctx->accept_queue[i] = ring_creat(128);
    }

    ctx->flag = 0;

    /* > 建立管道套接字 */
    for (i = 0; i < EPOLL_THREAD_NUM; ++i) {
        struct epoll_event ev;
        if (pipe(ctx->pipe_fd[i]) < 0) {
            fprintf(stderr, "pipe err\n");
            return NULL;
        }
    
    
        le1w_conn_t *c = (le1w_conn_t *)calloc(1, sizeof(le1w_cntx_t));
        c->fd = ctx->pipe_fd[i][0];
        fd_set_noblocking(c->fd);
        c->recv_addr = buf_alloc();
        c->send_list = list_creat(NULL);
        c->recv_cb = (conn_recv_cb_t)le1w_recv_proc;
        c->send_cb = (conn_send_cb_t)le1w_send_proc;
        c->alive_time = time(NULL);
    
        ev.events = EPOLLET | EPOLLIN;
        ev.data.ptr = c;

        /* > 将管道套接字注册到epoll中 */
        ret = epoll_ctl(ctx->epfd[i], EPOLL_CTL_ADD, c->fd, &ev);
        if (ret < 0)
            return NULL;
    }

    pthread_mutex_init (&ctx->mutex,NULL);
    le1w_set_ctx(ctx);

    return ctx;
}

/******************************************************************************
 **函数名称: le1w_accept
 **功    能: accept并注册epoll信息
 **输入参数: ctx：上下文 svr_fd：监听套接字
 **输出参数: NONE
 **返    回: 0:成功 -1:失败
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
int le1w_accept(le1w_cntx_t *ctx, int *idx)
{
    struct epoll_event ev;
    int qidx = *idx;
    /* > 构建链接 */
    le1w_add_fd_t *add = ring_pop(ctx->accept_queue[qidx]);
    fprintf(stderr, "add_fd = [%d]", add->fd);
    if (add == NULL) {
        fprintf(stderr, "the add struct is null");
        return 0;
    }

    le1w_conn_t *c = (le1w_conn_t *)calloc(1, sizeof(le1w_cntx_t));  
    c->fd = add->fd;
    c->recv_addr = buf_alloc();
    c->send_list = list_creat(NULL);
    c->recv_cb = (conn_recv_cb_t)le1w_recv_proc;
    c->send_cb = (conn_send_cb_t)le1w_send_proc;
    c->alive_time = time(NULL);

    fd_set_noblocking(c->fd);
    
    //fprintf(stderr, "add_fd = [%d]\n", c->fd);
    /* > 插入ctx平衡二叉树,加锁处理 */
    pthread_mutex_unlock(&ctx->mutex);
    if (avl_insert(ctx->conn_tree, (void *)c)) {
        free(c);
        return -1;
    }
    pthread_mutex_unlock(&ctx->mutex);

    /* > 注册epoll */
    ev.data.ptr = c;
    ev.events = EPOLLIN | EPOLLET;

    if (epoll_ctl(ctx->epfd[qidx], EPOLL_CTL_ADD, c->fd, &ev) < 0) {
        return -1;
    }

    free(add);
    add = NULL;

    return 0;
}

/******************************************************************************
 **函数名称: le1w_read_data
 **功    能: 读数据
 **输入参数: cli_conn：连接套接字 recvbuf：接收缓存
 **输出参数: n读入消息量
 **返    回: ret:状态
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
enum le1w_read_ret le1w_read_data(int cli_conn, data_buf_t* recvbuf, int *n)
{

    int len = 0;
    data_buf_t* tp_buf = recvbuf;
    enum le1w_read_ret ret = READ_NO_DATA_RECEIVED;
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
 **函数名称: le1w_recv_proc
 **功    能: 处理可读事件
 **输入参数: ctx：上下文 c：连接事件
 **输出参数: 
 **返    回: >0:成功  -1:错误
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
int le1w_recv_proc(le1w_cntx_t *ctx, le1w_conn_t *c, int *idx)
{
    
    int n, head_len, head_cmd;
    int read_fd = c->fd;
    data_buf_t* recvbuf = c->recv_addr;

    /* > 更新时间戳 */
    c->alive_time = time(NULL);

    char message[50];
    
    /* > 开始读数据 */
    while (1) {
        
        n = 0;
        enum le1w_read_ret read_ret = le1w_read_data(read_fd, recvbuf, &n);
        if (read_ret == READ_NO_DATA_RECEIVED || read_ret == READ_ERROR ) {
            fprintf(stderr, "errmsg:[%d] %s! ret:%d fd:%d\n",
                    errno, strerror(errno), read_ret, read_fd);
            return -1;
        } else if (0 == n) {
            fprintf(stderr, "Didn't recv data! fd:%d\n", read_fd);
            break;
        }

        fprintf(stderr, "Recv data! fd:%d n:%d\n", read_fd, n);

        while (1) {

            /* > buf中数据长度是否大于头的长度 */
            if (recvbuf->bytes < sizeof(le1w_recv_head_t)) {
                break;
            }

            /* > 解析协议头 */
            le1w_recv_head_t* p = (le1w_recv_head_t *)recvbuf->curr;
            head_len = ntohl(p->length);
            head_cmd = ntohl(p->cmd);

            /* > 判断消息体的完整性 */
            if (head_len < 0 || head_len > recvbuf->size) {
                return -1;
            }else if (recvbuf->bytes < (head_len + sizeof(le1w_recv_head_t))) {
                break;
            }
            /*
            if (head_cmd != CMD_1 && head_cmd != CMD_2 && head_cmd != HEART_BEAT) {
                return -1;
            }
            */

            /* > 获取数据 */
            memcpy(message, recvbuf->curr+sizeof(le1w_recv_head_t), head_len);

            /* > 进行数据处理 */
            if(le1w_recv_data_proc(ctx, c, message, head_cmd, idx) < 0){
                return -1;
            }

            recvbuf->bytes -= sizeof(le1w_recv_head_t) + head_len;

            printf("now bytes = %d \n", recvbuf->bytes);
            recvbuf->curr += sizeof(le1w_recv_head_t) + head_len;
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
 **函数名称: le1w_recv_data_proc
 **功    能: 可读事件中根据cmd进行数据处理
 **输入参数: 
 **     ctx：上下文
 **     c：连接
 **     message：读入的消息体
 **     head_cmd：客户端命令
 **输出参数: 
 **返    回: >0:成功  -1:错误
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
int le1w_recv_data_proc(le1w_cntx_t *ctx, le1w_conn_t *c, char *message, int head_cmd, int *idx)
{
    //ctx->flag++;
    int ret;
    switch (head_cmd) {
        case CMD_1:
            printf("thread_id = [%d]  cmd_1 and message = %s and flag = %d\n", *idx, message, ctx->flag);
            le1w_pack_send_mess(ctx, c,  head_cmd, idx);
            break;

        case CMD_2:
            printf("thread_id = [%d]  cmd_2 and message = %s and flag = %d\n", *idx, message, ctx->flag);
            le1w_pack_send_mess(ctx, c,  head_cmd, idx);
            break;

        case HEART_BEAT:
            c->alive_time = time(NULL);
            printf("heart_beat from fd = %d\n", c->fd);
            le1w_pack_send_mess(ctx, c,  head_cmd, idx);
            break;

        case ADD_FD:
            ret = le1w_accept(ctx, idx);
            if (ret < 0) {
                fprintf(stderr, "ADD_FD ERR\n");
            }
            break;

        default:
            printf("not have this CMD\n");
            break;
    }

    return 0;
}
/******************************************************************************
 **函数名称: buf_alloc
 **功    能: 内存申请
 **输入参数:
 **输出参数:
 **返    回: t：内存地址
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
data_buf_t* buf_alloc()
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
 **函数名称: buf_free
 **功    能: 内存释放
 **输入参数: t：内存地址
 **输出参数:
 **返    回:
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
void buf_free(data_buf_t* t)
{
    free(t);
    t = NULL;
}

/******************************************************************************
 **函数名称: close_conn
 **功    能: 关闭连接
 **输入参数: 待修改
 **输出参数: 
 **返    回:
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
void close_conn(le1w_cntx_t *ctx, le1w_conn_t *c, int *idx)
{
    le1w_conn_t *addr;

    /* > 删除epoll中的注册 */
    epoll_ctl(ctx->epfd[*idx], EPOLL_CTL_DEL, c->fd, &ctx->ev_remov);

    /* > 删除ctx->conn_tree中的注册 */
    if (avl_delete(ctx->conn_tree, (void *)c, (void *)&addr)) {
        printf("avl delete error\n");
        return;
    }

    /* > 关闭套接字 */
    close(c->fd);

    /* > 释放链接中c的内存 */
    FREE(c->recv_addr);
    if (c->send_list) {
        list_destroy(c->send_list, mem_dummy_dealloc, NULL);
    }
    FREE(c);
}

/******************************************************************************
 **函数名称: buf_cmp_cb
 **功    能: 平衡二叉树回调函数
 **输入参数: reg1 reg2
 **输出参数:
 **返    回:
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
static int buf_cmp_cb(le1w_conn_t *reg1, le1w_conn_t *reg2)
{

    return (int *)reg1 - (int *)reg2;

}

/******************************************************************************
 **函数名称: le1w_echo_send
 **功    能: 数据发送
 **输入参数:
 **输出参数:
 **返    回: fd: 套接字 echo_mess：待发送数据
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
int le1w_echo_send(int fd, data_buf_t *echo_mess)
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
        } else if (send == 0) {
            return -1;
        }

        echo_res->bytes += send_reg;
        echo_res->curr += send_reg;

    }

    return echo_res->bytes;
}

/******************************************************************************
 **函数名称: le1w_pack_send_mess
 **功    能: 将待发送数据放入发送队列
 **输入参数:
 **输出参数:
 **返    回: l: 发送队列列表 cmd：命令
 **实现描述:
 **注意事项:
 **作    者: # Zengyao.pang # 2017-08-15 17:25:26 #
 ******************************************************************************/
void le1w_pack_send_mess(le1w_cntx_t *ctx, le1w_conn_t *c, int cmd, int *idx)
{
    struct epoll_event ev;
    char param[256];
    
    /* > 准备发送内存数据 */
    sprintf(param, "CMD_%d RECV SUCC", cmd);
    data_buf_t *tp = buf_alloc();
    le1w_recv_head_t *bh = (le1w_recv_head_t *)tp->buf;
    bh->cmd = htonl(cmd);
    bh->length = htonl(strlen(param) + 1);
    strcat(tp->buf + sizeof(le1w_recv_head_t), param);
    
    tp->size = sizeof(le1w_recv_head_t) + strlen(param) + 1;
    tp->bytes = 0;
    tp->curr = tp->buf;

    /* > 放入发送链表中 */
    list_rpush(c->send_list, tp);

    /* > epoll注册发送事件 */
    ev.data.ptr = c;
    ev.events = EPOLLIN | EPOLLET | EPOLLOUT;

    epoll_ctl(ctx->epfd[*idx], EPOLL_CTL_MOD, c->fd, &ev);

    return;
}
