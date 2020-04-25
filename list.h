#ifndef __MY_LIST__
#define __MY_LIST__

#include "common.h"
//#define P_SND_LIST_SZ (4)

#define PKT_SENT (0x00000001)

// == initial values ==
// idx = 0
// seq = 0 (若seq为0，其他无效)
// byte_offset = 0
// begin = 0
// sz = 0
// flags = 0
typedef struct sending_packet {
    unsigned int idx; // 发送该包所用的tunnel在tunlist中的数组下标
    unsigned int seq; // packet 的 seq
    unsigned int byte_offset; // 这个包的首字节在bytes stream的offset
    unsigned int begin; // 在buffer中的起始位置
    unsigned int sz; // 字节数
    unsigned int flags;
    
    // if ( flags & PKT_SENT ): t is the sent time of pkt
    // if ( ! ( flags & PKT_SENT ) ): t is the sending time of pkt
    struct timeval t;
} SendingPkt;

// == initial values ==
// head = 0
// tail = 0
// sending_cnt = 0 (若sending_cnt为0，则head和tail无效)
typedef struct sending_packet_list {
    unsigned int head; // 第一个SendingPkt所在的下标
    unsigned int tail; // 最后一个SendingPkt所在的下标
    unsigned int sending_cnt; // 还有多少个在发送
    SendingPkt   pkts[P_SND_LIST_SZ];
} SendingList;

void initSndList( SendingList * );
void destroySndList( SendingList * );

void dumpSndList( SendingList * );

int isSndListFull( SendingList * );
int isSndListEmpty( SendingList * );

void addSeq( SendingList *, int, unsigned int, unsigned int, size_t, size_t );
unsigned int delSeq( SendingList *, unsigned int ); 

void getATimout( SendingList *, unsigned int );

void moveHeadToTail( SendingList *, unsigned int );

#endif
