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

#define P_STAT_ACTIVE   (31)
#define P_STAT_ENDING   (32) // 我方 merge fd 已关闭，准备结束pipe
#define P_STAT_GOT_FIN  (33) // 收到FIN，准备结束pipe
#define P_STAT_LAST_FIN (34) // 收到FIN，我方的merge fd也关闭了
#define P_STAT_END      (35) // pipe可以结束
#define P_STAT_BAD      (50) // pipe有问题，不能再用，结束pipe

#define P_STREAM_BEGIN    (0)
#define P_STREAM_BUFF2TUN (1)
#define P_STREAM_BUFF2FD  (2)
#define P_STREAM_TUN2BUFF (3)
#define P_STREAM_FD2BUFF  (4)
#define P_STREAM_END      (5)

#define P_FLG_TUN_FIN  (0x00000001)
#define P_FLG_FD_FIN   (0x00000002)
#define P_FLG_REPUSH   (0x00000004) // 需要对方re push 数据
#define P_FLG_SENDING  (0x00000008) // 存在tunnel fd处于write block状态
#define P_FLG_DATA     (0x00000010) // tunnel 处于write block状态是因为在发真正的数据
#define P_FLG_SEND_FIN (0x00000020) // 需要发送FIN 
#define P_FLG_RECVING  (0x00000040) // 


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
    unsigned int flags;
    char         key[P_KEY_SZ]; // pipe 的唯一标识，
                                // 提供相同 key 的tunnel 会被加入同一个tun list
    int          fd;            // pipe 的一端是一个 fd
    void       * fd_fn;         // fd 对应的fdnode指针
    unsigned int fd_flags;
    time_t       fd_t;
    
    TunList      tun_list;      // pipe 的另一端是多个 fd
    time_t       tun_t;
    char         tun_closed;

    Buffer       fd2tun;
    Buffer       tun2fd;
    Buffer       ap;

    unsigned int pkt_seq;    // the last seq number that added in SendingList
    unsigned int byte_seq;
    unsigned int last_recved_byte;

    // 接收数据相关的
    unsigned int last_recv_seq; // 最后一个收到的sequence number(数据被完全转移到tun2fd的才算)
    unsigned int last_recv_ack; // 给对方确认的最后一个sequence number    

    Line         prev_seglist;  // 
} Pipe;

int initPipe( Pipe *, size_t, int );
void cleanPipe( Pipe * );
void destroyPipe( Pipe * );

int hasPipeFd( Pipe * );
void setPipeFd( Pipe * , int , void * );
void unsetPipeFd( Pipe * );

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
