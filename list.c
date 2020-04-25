#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "list.h"


static unsigned int nextPos( unsigned int x ) {
    return ( x + 1 ) % P_SND_LIST_SZ; 
}

static unsigned int posOf( SendingList * l, unsigned int seq ) {
    return ( l->head + ( seq - l->pkts[l->head].seq ) ) % P_SND_LIST_SZ;
}

static unsigned int lastPos( SendingList * l ) {
    return posOf( l, l->pkts[l->tail].seq );
}

void initSndList( SendingList * l ) {
    assert( l != NULL);
    bzero( l, sizeof(SendingList) );
}

void destroySndList( SendingList * l ) {
    initSndList( l );
}

void dumpSndList( SendingList * l ) {
    unsigned int i;
    assert( l != NULL );

    printf("====\n");
    if ( l->sending_cnt > 0 ) {
        printf("head=%u tail=%u sending_cnt=%u\n", 
               l->head, l->tail, l->sending_cnt);

        i = l->head;
        do {
            if ( l->pkts[i].seq != 0 ) {
                printf("i=%d begin=%lu offset=%lu seq=%u\n", 
                       i,
                       l->pkts[i].begin, 
                       l->pkts[i].offset, 
                       l->pkts[i].seq);
            }
            i = nextPos( i );
        } while ( i != nextPos( lastPos( l ) ) );
    }
    else {
        printf("head=X tail=X sending_cnt=%u\n", 
               l->sending_cnt);
    }
    printf("====\n");
}

int isSndListFull( SendingList * l ) {
    assert( l != NULL);
    return (l->sending_cnt > 0 && l->head == nextPos( l->tail ) );
}

int isSndListEmpty( SendingList * l ) {
    assert( l != NULL);
    return l->sending_cnt == 0;
}

void addSeq( SendingList * l, int i, unsigned int seq, unsigned int byte_offset, size_t begin, size_t sz ) {
    assert( l != NULL);

    // 由于发包是一个一个按顺序发的，
    // 所以新来的seq在最后。
    l->tail = nextPos( l->tail );
    
    if ( isSndListEmpty( l ) ) {
        l->head = l->tail;
    }

    l->pkts[l->tail].idx = i;
    l->pkts[l->tail].seq = seq;
    l->pkts[l->tail].byte_offset = byte_offset;
    l->pkts[l->tail].begin = begin;
    l->pkts[l->tail].sz = sz;

    ( l->sending_cnt )++;
}

/*
 * == desc ==
 * 该函数不返回错误，外部程序负责正确的调用该函数。
 * 
 * == return ==
 * >=0: next sending byte's index
 *  -1: send all
 */
unsigned int delSeq( SendingList * l, unsigned int seq ) {
    unsigned int idx;

    assert( l != NULL);

    //== 计算要删除的位置    
    idx = posOf( l, seq );

    //== 将指定的seq删除
    l->pkts[idx].seq = 0;
    --( l->sending_cnt );

    //== 如果删除的是head并且还有正在发送的包，
    //   将head移到下一个正在发送的包上
    if ( idx == l->head ) { 
        if ( l->sending_cnt > 0 ) {
            // 如果head后面还有在发的包，
            // 把head设置为后面第一个还在发的包的下标。
            while ( 1 ) {
                l->head = nextPos( l->head ) ;
                if ( l->pkts[l->head].seq > 0 ) {
                    break;
                }
            }
        }
        else {
            // 如果发完了，返回1
            return l->pkts[l->tail].end;
        }
    }

    return l->pkts[l->head].begin;
}
