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
 
#define MYPORT  8787
#define BUFFER_SIZE 1024

int main(){

    int opt = 1;
    //定义sockfd
    int sock_cli = socket(AF_INET,SOCK_STREAM, 0);

    //定义sockaddr_in
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(MYPORT);  ///服务器端口
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1");  ///服务器ip
    fd_set_noblocking(sock_cli);
    //setsockopt(sock_cli, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));
    //连接服务器，成功返回0，错误返回-1
    fd_set set;
    struct timeval tm;
    if (connect(sock_cli, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        if(errno != EINPROGRESS)
            return -1;
        else{
            printf("%d\n", 1);
            tm.tv_sec = 5;
            tm.tv_usec = 0;
            FD_ZERO(&set);
            FD_SET(sock_cli, &set);
            if(select(sock_cli+1, NULL, &set, NULL, &tm) > 0){
                printf("%d\n", 1);
				int slen = sizeof(int);
                int error;
                getsockopt(sock_cli, SOL_SOCKET, SO_ERROR, (void*)(&error), (socklen_t *)&slen);
                if(error == 0)
                    goto BEGIN;
                else{
					printf("%d\n", error);
					return -1;
				}
            }
        }
        printf("%d", errno);
    }

    char sendbuf[BUFFER_SIZE];
BEGIN:
    while (fgets(sendbuf, sizeof(sendbuf), stdin) != NULL){
        send(sock_cli, sendbuf, strlen(sendbuf),0); ///发送
        if(strcmp(sendbuf,"exit\n")==0)
            break;
        fputs(sendbuf, stdout);
        memset(sendbuf, 0, sizeof(sendbuf));
    }
    close(sock_cli);
    return 0;
}

int fd_set_noblocking(int fd){                                                  
    int ret = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);              
    if(ret < 0)                                                                 
        return -1;                                                              
    return 0;                                                                                                                                                  
}  

