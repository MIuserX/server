#ifndef __MY_COMMON_H__
#define __MY_COMMON_H__

#include <sys/epoll.h>

#define BOOL int
#define TRUE  (1)
#define FALSE (0)

#define    MAXBACK      1000
#define    MAXEPOLL     1024    /* 对于服务器来说，这个值可以很大的！ */

// pipe key size
#define P_KEY_SZ (16)

// tunnel list max length
#define TUN_LIST_SZ (2) // wire and wireless NIC

// tunnel move-buffer size
#define TUN_BUFF_SZ (4096)

// server pipe list max length
#define P_LIST_SZ (64)

// pipe's buffer size, every pipe has two buffers
#define P_BUFF_SZ (1024*1024)

// 
#define P_PREV_SEND_MAXSZ (8)

// 乱序包最多缓存多少个。
// 例如想要 5 号包，
// 那么 [6, 13] 号包来了，都会被缓存。
// 如果大于13的号来了，丢弃。
#define P_PREV_RECV_MAXSZ (32)

#define P_PREV_NEED_REPUSH (8)

#define FD_HAS_UNWRITE  (0x00000001) // (废弃)
#define FD_HAS_UNREAD   (0x00000002) // (废弃)
#define FD_IS_EPOLLIN   (0x00000004) // fd 处于EPOLLIN
#define FD_IS_EPOLLOUT  (0x00000008) // fd 处于监听EPOLLOUT状态
#define FD_CLOSED       (0x00000010) // fd closed
#define FD_READ_BLOCK   (0x00000020) // fd遭遇read block
#define FD_WRITE_BLOCK  (0x00000040) // fd遭遇write block
#define FD_NO_WRITING   (0x00000080) // 对方merge fd 已关闭
#define FD_NO_READING   (0x00000100) // 

#define FDNODE_BUFF     (0x10000000) // bf 申请了动态内存(关闭时需要释放)
#define FDNODE_AUTHED   (0x20000000) // fd 已关联pipe
#define FDNODE_IS_TUNFD (0x40000000) // fd 是个tunnel fd
#define FDNODE_ACT_NEW  (0x80000000) // fdnode 进行了new pipe 动作


#define P_END_WAIT_SEC (1)

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
