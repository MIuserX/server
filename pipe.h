#ifndef __PIPE_H__
#define __PIPE_H__

#include <stdint.h>
#include <time.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <pthread.h>

#include "common.h"
#include "buffer.h"
#include "tunnel.h"
#include "packet.h"

#define B_INIT       0
#define B_ACTIVE     2
#define B_FIN_SENT   3
#define B_FIN_RECVED 4

#define P_STAT_ACTIVE  (31)
#define P_STAT_ENDING  (32) // 等待数据发送完毕
#define P_STAT_END     (33) // 已发送FIN
#define P_STAT_ENDING1 (34) // 数据已发送完毕，可以发送FIN
#define P_STAT_ENDING2 (35) // 正在发送FIN

#define P_STREAM_BEGIN    (0)
#define P_STREAM_BUFF2TUN (1)
#define P_STREAM_BUFF2FD  (2)
#define P_STREAM_TUN2BUFF (3)
#define P_STREAM_FD2BUFF  (4)
#define P_STREAM_END      (5)

//#define P_SEG_HEAD  (1) //正在读packet的head
//#define P_SEG_DATA  (2) //正在读packet的data
//#define P_SEG_FULL  (3) //packet已读满，待向buffer转移


typedef struct un_send_ack {
    char         sending; // 'y' or 'n'
    unsigned int seq;
} UnSendAck;

// == init ==
//
//
//
typedef struct pipe {
    int          use;
    int          idx;

    pthread_t    pid;

    int          stat;
    char         key[P_KEY_SZ]; // pipe 的唯一标识，
                                // 提供相同 key 的tunnel 会被加入同一个tun list
    int          fd;            // pipe 的一端是一个 fd
    unsigned int fd_flags;
    time_t       fd_t;
    
    TunList      tun_list;      // pipe 的另一端是多个 fd
    time_t       tun_t;
    char         tun_closed;

    Buffer       fd2tun;
    Buffer       tun2fd;
    Buffer       ap;

    unsigned int last_send_seq; // 最后一个发送的sequence number
    unsigned int last_send_ack; // 对方确认的最后一个sequence number
    Line         prev_acklist;  // 提前收到的对方给的ack_list


    // 接收数据相关的
    unsigned int last_recv_seq; // 最后一个收到的sequence number(数据被完全转移到tun2fd的才算)
    unsigned int last_recv_ack; // 给对方确认的最后一个sequence number    

    int          unsend_count;     // 未发送给对方的ack数
    Line         ack_sending_list; // 正在发送的ack 列表 
    
    Line         prev_seglist;  // 
} Pipe;

int initPipe( Pipe *, size_t, int );
void cleanPipe( Pipe * );
void destroyPipe( Pipe * );

// connect to target used by server endpoint
int connectFd( Pipe *, struct sockaddr_in );

int hasUnSendAck( Pipe * );
int hasDataToTun( Pipe * );

int stream( int, Pipe *, int );

typedef struct pipe_list {
    Pipe pipes[P_LIST_SZ]; // 数组
    int  sz;               // 有效的pipe数
} PipeList;

void initPipeList( PipeList * );
void destroyPipeList( PipeList * );

int isPipeListEmpty( PipeList * );
int isPipeListFull( PipeList * );

int getAEmptyPipe( PipeList * pl );
int addPipe( PipeList * , Pipe *, Pipe ** );
int delPipe( PipeList * , Pipe * );
int delPipeByKey( PipeList * , char * );
void delPipeByI( PipeList * , int );
Pipe * searchPipeByKey( PipeList *, char * );
//Pipe * searchByTunFd( PipeList * pl, int fd );

#endif
