#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "packet.h"

void dumpPacket( Packet * p ) {
    int i = 0;
    assert( p != NULL );

    printf("\n====\n");
    printf("PSH=%d x_seq=%u\n", p->head.flags&ACTION_PSH ? 1 : 0, p->head.x_seq );
    printf("ACK=%d x_ack=%u\n", p->head.flags&ACTION_ACK ? 1 : 0, p->head.x_ack );
    printf("FIN=%d\n", p->head.flags&ACTION_FIN ? 1 : 0 );
    printf("sz=%u wnd=%u\n", p->head.sz, p->head.wnd );
    printf("data=|");
    for ( i = 0; i < p->head.sz - PACKET_HEAD_SZ; i++ ) {
        putchar(p->data[i]);
    }
    printf("|\n");
    printf("====\n");
}
