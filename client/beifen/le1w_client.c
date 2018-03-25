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

#define CMD_1 1
#define CMD_2 2
#define CMD_3 3

struct send_mess* pack_data(const char *data, uint32_t cmd);

const char *data1 = "Hello World!!";
const char *data2 = "Hello Company...";
const char *data3 = "Hello Whut~~";

typedef struct send_mess{
   uint32_t cmd;
   uint32_t length;
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

int main(){
    
    //发送数据准备
    
   //printf("mess->cmd = %d\n", mess->cmd);
    //printf("mess->length = %d\n", mess->length);
    //printf("mess->mess = %s\n", mess->mess);

	srand( (unsigned)time( NULL ) ); 
    int tmp_num = rand()%2 + 1;
	printf("%d \n", tmp_num);
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
			return -1;
	}
 
    struct send_mess *mess = pack_data(data, cmd);
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
        printf("%d", errno);
        if(errno != EINPROGRESS){
            return -1;
        }
    }


    if((send(sock_cli, (char *)mess, strlen(data) + 1 + sizeof(send_mess),0)) < 0){
        printf("send error");
        return -1;
    }

    while(1){
        sleep(120);
    };

    close(sock_cli);
    return 0;
}

struct send_mess *pack_data(const char *data, uint32_t cmd){
     
    int data_len  = strlen(data) + 1;
    struct send_mess *mess = (send_mess*)malloc(sizeof(send_mess) + data_len);
    mess->cmd = htonl(CMD_1);
    mess->length = htonl(data_len);
    memset(mess->mess, 0, data_len);
    memcpy(mess->mess, data, data_len);
    return mess;
    

}

