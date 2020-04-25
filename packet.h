#ifndef __PACKET_H__
#define __PACKET_H__

#include <stdint.h>

#define ACTION_SYN   (0x00000001)
#define ACTION_PSH   (0x00000002) // 发送数据
//#define ACTION_ACK   (0x00000004) // 发送ACK
#define ACTION_RST   (0x00000008) //
#define ACTION_FIN   (0x00000010) // 终止

#define PACKET_MAX_SZ  (1024)
#define PACKET_HEAD_SZ (sizeof(PacketHead))
#define PACKET_DATA_SZ (PACKET_MAX_SZ - PACKET_HEAD_SZ)

typedef struct packet_head {
    unsigned int flags;// SYN|PSH|RST|FIN
                       // SYN: 含有认证信息
		       // PSH: 含有数据
		       // RST: reset seq number
		       // FIN: close socket
    unsigned int x_seq;    // packet sequence number
    //unsigned int x_ack;    // packet acknowleaement number
    unsigned int offset;   // 数据的第一byte在data stream中的序号
    uint8_t      checksum; // 
    unsigned int sz;       // packet总大小
    unsigned int wnd;      // 缓冲区剩余大小
} PacketHead;

typedef struct packet {
    PacketHead head;
    char       data[PACKET_DATA_SZ];
} Packet;

void dumpPacket( Packet * );

#endif
