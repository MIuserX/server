#ifndef __MY_COMMON_H__
#define __MY_COMMON_H__

#include <sys/epoll.h>

#define BOOL int
#define TRUE  (1)
#define FALSE (0)

#define    BUF_SIZE     1024
#define    MAXLINE      1024
#define    MAXBACK      1000
#define    MAX_CLI_CONN 16
#define    MAX_PATH     4
#define    MAXEPOLL     256    /* 对于服务器来说，这个值可以很大的！ */

// pipe key size
#define P_KEY_SZ (16)

// tunnel list max length
#define TUN_LIST_SZ (2) // wire and wireless NIC

// server pipe list max length
#define P_LIST_SZ (4)

// pipe's buffer size, every pipe has two buffers
#define P_BUFF_SZ (40960)

// 
#define P_PREV_SEND_MAXSZ (3)
#define P_PREV_RECV_MAXSZ (2)

#define FD_HAS_UNWRITE  (0x00000001)
#define FD_HAS_UNREAD   (0x00000002)
#define FD_IS_EPOLLLT   (0x00000004)
#define FD_IS_EPOLLOUT  (0x00000008)
//#define FD_HAS_EPOLLOUT (0x00000010)
//#define FD_HAS_EPOLLOUT (0x00000020)
//#define FD_HAS_EPOLLOUT (0x00000040)

typedef struct ForEpoll {
    int                epoll_fd;
    int                wait_fds;
    struct epoll_event ev;
    struct epoll_event evs[MAXEPOLL];
    int                fd_count;  // 当前已经存在的数量
} ForEpoll;

void initForEpoll( ForEpoll * );
void destroyForEpoll( ForEpoll * );

int createSocket(int *, char *, size_t);
int setnonblocking( int fd );
int local_listen( int , int );
void createKey( char * );

#endif
