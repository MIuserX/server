#ifndef __TUNNEL_H__
#define __TUNNEL_H__

#include <netinet/in.h>            /* socket类定义需要*/
#include <sys/types.h>
#include <sys/socket.h>

#include "common.h"
#include "buffer.h"

#define TUN_INIT    (0) // fd 为空
#define TUN_OPEN    (1) // 
#define TUN_ACTIVE  (2) // fd 已connect
                        // [s]  正在接收auth packet
#define TUN_AUTHED  (3) // [c|s]已认证
#define TUN_SEND_AP (4) // [c]  已发送 auth packet
#define TUN_RECV_AP (5) // [s]  已接收 auth packet，正在处理
#define TUN_REPLIED (6) // [s]  正在向 client 回复 auth result
#define TUN_CLOSED  (7) // [c|s] 收到c|s，

#define TUN_W_INIT    (31) // send_seg is empty
#define TUN_W_SEND    (32) // sending data

#define TUN_R_INIT    (42) // 正在读取pakcet head部分
#define TUN_R_DATA    (43) // 正在读取packet data部分
#define TUN_R_FULL    (44) // 读取完毕，做一些处理
#define TUN_R_MOVE    (45) // 正在转移数据

typedef struct tunnel {
    char if_name[36];  // network interface name
    int  fd;           // 该tunnel 的fd
    int  status;
    unsigned int flags;

    int    r_stat;   // 读状态
    Buffer r_seg;    // 接收 packet

    int    w_stat;   // 写状态
    Buffer w_seg;    // 发送 pakcet
} Tunnel;

int initTunnel( Tunnel * );
void destroyTunnel( Tunnel * );

typedef struct tunnel_list {
    Tunnel * tuns;    // tunnel 数组
    int      len;     // tunnel 数组长度
    int      sz;      // 当前数组存储的tunnel数

    int      auth_ok; // 认证成功的个数
    int      auth_ng; // 认证失败的个数

    // 负载均衡策略：目前用简单的轮询
    int      cur_idx; // 当前使用的tunnel的数组下标

    int      sending_count; // 处于发送状态的tunnel数
} TunList;

int initTunList( TunList *, size_t );
void destroyTunList( TunList * );

int isTunListEmpty( TunList * );
int isTunListFull( TunList * );

int exitTunList( TunList *, int );
int joinTunList( TunList *, int );

int activeTunnels( TunList * , struct sockaddr_in, char ** );

int getATun( TunList * );


/* 一些思想：
 *   1. TunList 需要一个负载均衡策略
 *
 *
 */

#endif
