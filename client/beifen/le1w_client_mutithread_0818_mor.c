/******************************************************************************
 ** Coypright(C) 2014-2024 Qiware technology Co., Ltd
 **
 ** 文件名: le1w_client.c
 ** 版本号: 1.0
 ** 描  述: 
 ** 作  者: # Zengyao.pang # 2017年07月22日 星期六 13时14分24秒 #
 ******************************************************************************/

#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>

#define MYPORT  8787
#define BUFFER_SIZE 1024
#define EXBUF_SIZE 1024

#define CMD_1 1
#define CMD_2 2
#define CMD_3 3

static void *to_work();
struct send_mess* pack_data(const char *data, uint32_t cmd);
static void create_worker(void *(*func)(void *));

const char *data1 = "Hello World!!";
const char *data2 = "Hello Company...";
const char *data3 = "Hello Whut~~";

typedef struct send_mess{
   uint32_t cmd;
   uint32_t length;
   char extra[EXBUF_SIZE];
   char mess[0];

}send_mess;

/*

int my_send(int fd, void *buffer, int len){
    int writen_len = 0;
    int rest_len = len;
    char *ptr = buffer;

    while(rest_len > 0){
        writen_len = send(fd, ptr, rest_len,0);
        if(writen_len < 0){
            if(errno == EINTR)
                writen_len = 0;
            else
                return -1;

        }
        rest_len -= writen_len;
        ptr += writen_len; 

    }
    return 0;

}
*/

int main(int argc,char** argv){

    if(argc < 1){
        printf("need num of thread");
        return -1;
    }
    int number = atoi(argv[1]);
    int i = 0;
        srand( (unsigned)time( NULL ) ); 
    for(i = 0; i < number; i++){
        create_worker(to_work);
    }
    
    while(1){
        sleep(1);
    };

    return 0;
}

struct send_mess *pack_data(const char *data, uint32_t cmd){
     
    int data_len  = strlen(data) + 1;
    struct send_mess *mess = (send_mess*)malloc(sizeof(send_mess) + data_len);
    mess->cmd = htonl(cmd);
    mess->length = htonl(data_len);
    memset(mess->mess, 0, data_len);
    memcpy(mess->mess, data, data_len);
    return mess;
    
}

static void *to_work(){

    //定义sockfd
    int sock_cli = socket(AF_INET,SOCK_STREAM, 0);

    //定义sockaddr_in
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(MYPORT);  ///服务器端口
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  ///服务器ip
    //连接服务器，成功返回0，错误返回-1
    if (connect(sock_cli, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        if(errno != EINPROGRESS){
            return ;
        }
        printf("%d", errno);
    }

    int flag = 0;
    while(1){
        //发送数据准备
    
        //printf("mess->cmd = %d\n", mess->cmd);
        //printf("mess->length = %d\n", mess->length);
        //printf("mess->mess = %s\n", mess->mess);

        int tmp_num = rand()%2 + 1;
        const char * data = data1;
        int cmd = 0;

        switch(tmp_num){
            case 1:
                data = data1;
                cmd = CMD_1;
                break;
            case 2:
                data = data2;
                cmd = CMD_2;
                break;
            default:
                return;
        }
 
        struct send_mess *mess = pack_data(data, cmd);
        if((send(sock_cli, (char *)mess, strlen(data) + 1 + sizeof(send_mess),0)) < 0){
            printf("send error");
        }
        free(mess);
        mess = NULL;

        char recv_mess[200];
        if(recv(sock_cli, recv_mess, sizeof(recv_mess), 0) < 0){
            printf("recv error");
        }else
            printf("%s, %d, len = %d\n", recv_mess, flag++, strlen(recv_mess));    
            
    
    }

    close(sock_cli);
    return NULL;
}



static void create_worker(void *(*func)(void *))                        
{                                                                                                                                                                                                           
    pthread_t       thread;                                                        
    pthread_attr_t  attr;                                                          
    int             ret;                                                           
                                                                                   
    pthread_attr_init(&attr);                                                      
                                                           
    if ((ret = pthread_create(&thread, &attr, func, NULL)) != 0) {                  
        fprintf(stderr, "Can't create thread: %s\n",                               
                strerror(ret));                                                    
         exit(1);                                                                   
    }                                                                              
}  
