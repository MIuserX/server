#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "pipe.h"
#include "packet.h"
#include "server.h"

/*
 * == param ==
 * p: 管道指针
 * sz: buffer 的 size，pipe 内有俩buffer
 *
 * == return ==
 *  0: successfully
 * -1: allocate memory failed
 */
int initPipe( Pipe * p, size_t sz, int ntun ) {
    int idx;

    assert( p != NULL );
    assert( sz > 0 );

    idx = p->idx;

    bzero( (void *)p, sizeof(Pipe) );
    
    if ( initTunList( &(p->tun_list), ntun ) ) {
        return -1;
    }
    if ( initBuff( &(p->fd2tun), sz, BUFF_MD_ACK ) ) {
        destroyTunList( &(p->tun_list) );
	return -1;
    }
    if ( initBuff( &(p->tun2fd), sz, BUFF_MD_2FD ) ) {
        destroyTunList( &(p->tun_list) );
        destroyBuff( &(p->fd2tun) );
	return -1;
    }
    if ( initBuff( &(p->ap), sizeof(AuthPacket), BUFF_MD_2FD ) ) {
        destroyTunList( &(p->tun_list) );
        destroyBuff( &(p->fd2tun) );
        destroyBuff( &(p->tun2fd) );
	return -1;
    }

    initLine( &(p->prev_seglist) );

    p->fd = -1;
    p->tun_closed = 'n';
    p->idx = idx;

    return 0;
}

void cleanPipe( Pipe * p ) {
     assert( p != NULL );
     p->use = 0;
}

void destroyPipe( Pipe * p ) {
    int idx;

    assert( p != NULL );
    
    idx = p->idx;

    if ( hasPipeFd( p ) ) {
        close( p->fd );
    }

    destroyTunList( &(p->tun_list) );
    destroyBuff( &(p->fd2tun) );
    destroyBuff( &(p->tun2fd) );
    
    bzero( (void *)p, sizeof(Pipe) );
    p->idx = idx;
}

static void _initSeq( Pipe * p ) {
    assert( p != NULL );
    p->pkt_seq = random() % 66 + 3; // [3, 68]
}

// (1) process Pipe.pkt_seq
// (2) process Packet.flags
static void _setPktSeq( Pipe * p, Packet * pkt ) {
    assert( p != NULL );
    assert( pkt != NULL );

    if ( p->pkt_seq == 0 ) {
        _initSeq( p );
        pkt->head.flags |= ACTION_SYN;
    }
    ++( p->pkt_seq );

    pkt->head.flags |= ACTION_PSH;
    pkt->head.x_seq = p->pkt_seq;

}

static void _fillDataInPkt( Pipe * p, Packet * pkt, int tun_i ) {
    size_t sz = PACKET_DATA_SZ;

    assert( p != NULL );
    assert( pkt != NULL );

    _setPktSeq( p, pkt );

    pkt->head.offset = p->byte_seq;
    preGetBytesV2( &(p->fd2tun), pkt->data, &sz, tun_i, pkt->x_seq, p->byte_seq );
    pkt->sz = PACKET_HEAD_SZ + sz;
    p->byte_seq += sz;
}

static void _signAsSent( Pipe * p, unsigned int seq ) {
    assert( p != NULL );

    delBytes( &(p->fd2tun), seq );
}

/* pipe.fd 和 pipe.fd_fn 是同步的变量，
 * 所以用独立的函数来设置和取消，
 * 防止数据操作出错。
 */
int hasPipeFd( Pipe * p ) {
    assert( p != NULL );

    return p->fd != -1;
}

void setPipeFd( Pipe * p, int fd, void * fn ) {
    assert( p != NULL );
    assert( fd >= 0 );

    p->fd = fd;
    p->fd_fn = fn;
}

void unsetPipeFd( Pipe * p ) {
    assert( p != NULL );
    p->fd = -1;
    p->fd_fn = NULL;
}

int hasUnSendFin( Pipe * p ) {
    assert( p != NULL );
    return ( ( p->stat == P_STAT_ENDING || p->stat == P_STAT_LAST_FIN ) 
		    && ( p->flags & P_FLG_SEND_FIN ) ); 
}

int hasDataToTun( Pipe * p ) {
    assert( p != NULL );
    return hasActiveData( &(p->fd2tun) ) 
	    || hasUnSendFin( p );
}
/*
static int cmp( void * a, void * b ) {
    UnSendAck * _a = (UnSendAck *)a;
    UnSendAck * _b = (UnSendAck *)b;

    assert( a != NULL );
    assert( b != NULL );

    if ( _a->seq > _b->seq ) { 
        return 1;
    }
    if ( _a->seq < _b->seq ) { 
        return -1;		    
    }
    return 0;
}*/

static int pktCmp( void * a, void * b ) {
    PacketHead * _a = (PacketHead *)a;
    PacketHead * _b = (PacketHead *)b;

    assert( a != NULL );
    assert( b != NULL );

    if ( _a->x_seq > _b->x_seq ) { 
        return 1;
    }
    if ( _a->x_seq < _b->x_seq ) { 
        return -1;		    
    }
    return 0;
}

static int ackCmp( void * a, void * b ) {
    unsigned int * _a = (unsigned int *)a;
    unsigned int * _b = (unsigned int *)b;

    assert( a != NULL );
    assert( b != NULL );

    if ( *_a > *_b ) { 
        return 1;
    }
    if ( *_a < *_b ) { 
        return -1;		    
    }
    return 0;
}

static int matchUnAck( void * data, void * target ) {
    UnSendAck * _a = data;
    assert( data != NULL );

    if ( _a->sending == 'n' ) {
        return 1;
    }

    return 0;
}

static int matchSeq( void * data, void * target ) {
    UnSendAck * _a = data;
    assert( data != NULL );
    assert( target != NULL );

    if ( _a->seq == *((unsigned int *)target) ) {
        return 1;
    }

    return 0;
}

static int matchPacketSeq( void * data, void * target ) {
    PacketHead * ph  = data;
    PacketHead * ph2 = target;

    assert( data != NULL );
    assert( target != NULL );

    if ( ph->x_seq == ph2->x_seq ) {
        return 1;
    }

    return 0;
}


static int _tunToBuff( int , Pipe * );

/* 读取 tunnel fd 的数据。
 *
 * == description == 
 * 尝试推进 pipe 下所有 tunnel fd 的读取状态。
 * 任何一个tunnel fd 出错，都终止该pipe。
 *
 * 这里有两个block点：
 *   (1) socket block
 *   (2) 向tun2fd转移数据卡住
 *
 * == return ==
 *  2: end service
 *  0: cannot read
 * -1: errors
 */
static int tunToBuff( int evt_fd, Pipe * p ) {
    int       i;  // loop variable
    int       rt; // ret of function
    size_t    want_sz;
    UnSendAck usa;
    Packet *  pkt;

    assert( evt_fd >= 0 );
    assert( p != NULL );

    //==== read evt_fd
    for ( i = 0; i < p->tun_list.len; i++ ) {
        if ( p->tun_list.tuns[i].fd == evt_fd ) {
            if ( p->tun_list.tuns[i].status != TUN_AUTHED ) {
                printf("Error[%s:%d]: fd %d in tunnel list, but not TUN_AUTHED\n", 
				__FILE__, __LINE__, evt_fd );
		return -1;
	    }
	    
    	    rt = _tunToBuff( i, p );
            
            /* Tunnel 对端发送了FIN，
	     * 说明：
	     *   (1) 对方的merge fd已经主动closed了，
	     *       向对端传送数据的行为已经没有意义了，
	     *       因为无法向merge fd发送了。
	     *   (2) 对方已经把要向我方发的数据都发送了，
	     *       且收到了我方的ack。
	     * 
	     *
	     */

	    switch ( rt ) {
		case 2:
	            for ( i = 0; i < p->tun_list.len; i++ ) {
                        if ( p->tun_list.tuns[i].status == TUN_AUTHED ) {
		            p->tun_list.tuns[i].status = TUN_CLOSING;
			}
		    }
		case 1:
	        case 0:
		    return rt;
		case -1: // socket closed
		    if ( p->stat != P_STAT_ACTIVE ) {
		        p->tun_list.tuns[i].status = TUN_CLOSED;
		    }
		    return -1;

		default:
		    return -2;
	    }
	}
    }

    printf("Error[%s:%d]: fd %d not in tunnel list\n", 
    		__FILE__, __LINE__, evt_fd );
    return -1;
}

/*
 * == return ==
 *  0: ok
 * -1: memory not enough
 */
/*
int _process_ack( Pipe * p ) {
    PacketHead   * ph;    
    unsigned int * pu;

    assert( p != NULL );
    assert( t != NULL );

    ph = ( PacketHead *)( t->r_seg.buff);
        // case 1: ph->x_ack <  p->last_send_ack + 1
        //   这个忽略就成，需要receiver的ack目的是：确认自己需不需要重发。
        //   既然之前的packet已经被ack了，再ack也不管了。
        // case 2: ph->x_ack == p->last_send_ack + 1
        //   是想要的结果
        // case 3: ph->x_ack >  p->last_send_ack + 1
        //   有可能会发生，因为多个tunnel fd 可能会发生
        //   后来的packet先到的情况。
    if ( ph->x_ack == p->last_send_ack + 1 ) {
        ackBytes( &(p->fd2tun), ph->x_ack );
        p->last_send_ack = ph->x_ack;
        //printf("debug[%s:%d]: ackBytes x_ack=%u \n", 
        //	       __FILE__, __LINE__, ph->x_ack);

        while ( p->prev_acklist.len > 0 ) {
            pu = getHeadPtr( &(p->prev_acklist) );
            if ( *pu == p->last_send_ack + 1 ) {
                ackBytes( &(p->fd2tun), *pu );
                p->last_send_ack = (*pu);
                
                justOutLine( &(p->prev_acklist) );
                //printf("debug[%s:%d]: ackBytes x_ack=%u \n", 
    	        //	        __FILE__, __LINE__, *pu);
            }
            else {
                break;
            }
        }
    }
    else if ( ph->x_ack > p->last_send_ack + 1 ) {
        //printf("debug[%s:%d]: prev x_ack=%u, last_send_ack=%u\n", 
        //		__FILE__, __LINE__, 
        //	        ph->x_ack, 
        //          p->last_send_ack);
        if ( ph->x_ack <= p->pkt_seq ) {
            if ( seqInLine( &(p->prev_acklist), (void *)&(ph->x_ack), sizeof(ph->x_ack), ackCmp ) ) {
                printf("Error[%s:%d]: OOM\n", __FILE__, __LINE__);
                return -1;
            }
        }
        else {
            printf("Warning[%s:%d]: recervied:x_ack > our:pkt_seq\n", __FILE__, __LINE__);
        }
    }
    else { // case 1
        // 对端重发了ACK，只有一个可能原因：
        // 我方重发了packet，所以对方重发确认。
        // 对此，我方不做任何动作
    }
    return 0;
}
*/

/*
 * == return ==
 *  1: 去转移数据
 *  0: 去TUN_R_INIT
 * -1: memory not enough
 */
int _process_seq( Pipe * p, Tunnel * t ) {
    PachetHead * ph;
    UnSendAck    usa;
    
    ph = ( PacketHead *)( t->r_seg.buff);
    
    //printf("debug[%s:%d]: 待转移的packet如下：\n",  __FILE__, __LINE__);
    //dumpBuff( &(t->r_seg) );
    
    if ( ph->x_seq > p->last_recv_seq + 1 ) {
        //==> 提前收到了更靠后的packet

        if ( ph->x_seq <= p->last_recv_seq + P_PREV_RECV_MAXSZ ) {
            // 如果当前的seq大于等于last_recv_seq+1，
            // 先把packet存入prev_seglist 队列。
    	    if ( seqInLine( &(p->prev_seglist), 
    			(void *)(t->r_seg.buff), 
    			t->r_seg.sz, 
    			pktCmp ) ) {
                dprintf(2, "Error[%s:%d]: OOM\n", __FILE__, __LINE__);
                return -1;
            }
        }
        else {
           // ( ph->x_seq > p->last_recv_seq + P_PREV_RECV_MAXSZ )
           // 理论上不会发生，因为sender发送未确认队列最大长度是3，
           // receiver最多预先收两个包。
           dprintf(2, "Warning[%s:%d]: 理论上不可能，please check codes\n",  __FILE__, __LINE__);
        }
        return 0;
    }
    else if ( ph->x_seq < p->last_recv_seq + 1 ) {
        //==> 收到了更靠前的包
        // 如果发生了这种情况，则表示sender发送了该seq的packet，
        // 未收到receiver的ack，直到超时，sender重发了该数据。
        // receiver需要回复ack，以免sender继续重发。
        dprintf(2, "Error[%s:%d]: receiver需要回复ack，以免sender继续重发\n",  __FILE__, __LINE__);
        return 0;
    }

    return 1;
}


/* 向tun2fd转移数据。
 * 
 * == desc ==
 * 1. 将tunnel的r_seg的数据向tun2fd转移
 * 2. 将prev_seglist的数据向tun2fd转移
 *
 * == return ==
 *  1: 转移完毕
 *  0: 未转移完毕，r_seg中还有数据
 * -1: memory not enough
 */
int _to_buff( Pipe * p, Tunnel * t ) {
    size_t    want_sz;
    Packet *  pkt;
    UnSendAck usa;
    int       loop = 1;
 
    assert( p != NULL );
    assert( t != NULL );

    // packet已读完整，尝试将packet data向tun2fd这个buffer转移。
    //printf("debug[%s:%d]: TUN_R_MOVE1\n",  __FILE__, __LINE__);
    
    //printf("debug[%s:%d]: 待转移的packet如下：\n",  __FILE__, __LINE__);
    //dumpBuff( &(t->r_seg) );
    
    //==> 收到了想要的packet
    /* 如果 tun2fd 的 buffer 剩余空间够，就转移packet数据，
     * 如果不够，这次就不转移了。
     * 主要是保证不卡在转移数据上。
     */

    while ( 1 ) {
        want_sz = PACKET_HEAD_SZ;
        discardBytes( &(t->r_seg), &want_sz );
        
        want_sz = t->r_seg.sz;
        rt = putBytesFromBuff( &(p->tun2fd), &(t->r_seg), &want_sz );

        if ( isBuffEmpty( &(t->r_seg) ) ) {
            //printf("debug[%s:%d]: 转移完成后tun2fd情况：\n",  __FILE__, __LINE__);
            //dumpBuff( &(p->tun2fd) );
            //==== 尝试转移seq靠后的
            //printf("debug[%s:%d]: 存在提前收到靠后的packet\n",  __FILE__, __LINE__);
            p->last_recv_seq = pkt->head.x_seq;
            
            if ( p->prev_seglist.len > 0 ) {            
                pkt = ( Packet *)getHeadPtr( &(p->prev_seglist) );
                if ( pkt->head.x_seq == p->last_recv_seq + 1 ) {
                    /* 如果 tun2fd 的 buffer 剩余空间够，就转移packet数据，
                     * 如果不够，这次就不转移了。
                     * 主要是保证不卡在转移数据上。
                     */
                    printf("debug[%s:%d]: 提前收到的靠后的packet可以向buffer转移\n",  __FILE__, __LINE__);
                    
                    cleanBuff( &(t->r_seg) );
                    //printf("debug[%s:%d]: p->unsend_count=%d\n", __FILE__, __LINE__, p->unsend_count);

                    want_sz = pkt->head.sz - PACKET_HEAD_SZ;
                    putBytes( &(t->r_seg), pkt->data, &want_sz );
                    justOutLine( &(p->prev_seglist) );
                    
                    continue;;
                }
            }
            
            return 0;
        }

        break;
    }
    
    return 1;
}

/* 从 tunnels 向 pipe 里读数据
 * == desc ==
 * 当tunnels的fd有了读事件才会调用这函数。
 * 
 * 这函数会遇到的系统错误有：
 *   (1) socket errors
 *   (2) memory not enough
 * == return ==
 *  2: end service
 *  1: 向buffer转移卡住
 *  0: socket block
 * -1: socket closed
 * -2: errors, see error log
 */
static int _tunToBuff( int i, Pipe * p ) {
    size_t         want_sz;
    int            rt;
    PacketHead *   ph;
    Packet     *   pkt;
    Tunnel       * t = p->tun_list.tuns + i;
    
    assert( p != NULL );

    while ( 1 ) {
        switch ( t->r_stat ) {
            case TUN_R_INIT:
		/* 正在读packet的head
		 *
		 */
                //if ( ( p->tun2fd.len - p->tun2fd.sz ) < ( PACKET_MAX_SZ - PACKET_HEAD_SZ ) ) {
                //    return 1;
                //}

    	        cleanBuff( &(t->r_seg) );
                t->status = TUN_R_HEAD;
                break;

            case TUN_R_HEAD:    
    	        // 读取PacketHead时也可能中途遇到socket block，
    	        // 所以这里待读取size是，PacketHead size减去已读取size。
                want_sz = PACKET_HEAD_SZ - t->r_seg.sz;
        	rt = putBytesFromFd( &(t->r_seg), t->fd, &want_sz );
                //printf("debug[%s:%d]: after rt=%d want_sz=%lu\n",  __FILE__, __LINE__, rt, want_sz);
        	switch ( rt ) {
                    case -2: // socket error
                        printf("Error[%s:%d]: socket error, errmsg is \"%s\"\n", 
                                __FILE__, __LINE__, strerror(errno) );
        	    case -1: // socket closed
        	    case 0:  // socket block
        		return rt;
    		    case 1:  // already read want_sz bytes
    		        break;
    		    default: // -3: buffer is full, no space
                        dprintf(2, "Error[%s:%d]: check code\n",  __FILE__, __LINE__);
        		return -2;
        	}

	        if ( PACKET_HEAD_SZ == t->r_seg.sz ) {
                    //printf("debug[%s:%d]: 读取完PacketHead: \n", __FILE__, __LINE__);
    	            ph = ( PacketHead *)( t->r_seg.buff);
		    if ( ph->flags & ACTION_FIN ) {
                        t->status = TUN_CLOSING;
		    }
		    else if ( ph->flags & ACTION_SYN ) {
                        //printf("debug[%s:%d]:   SYN\n", __FILE__, __LINE__);
		        
		        p->last_recv_ack = ph->x_seq - 1;
		    }

		    if ( ph->flags & ACTION_ACK ) {
                        //printf("debug[%s:%d]:   ACK, x_ack=%u\n", __FILE__, __LINE__, ph->x_ack);
		        _process_ack( p );
		    }

		    if ( PACKET_HEAD_SZ == ph->sz ) {
                        t->r_stat = TUN_R_FULL;
    	            }
		    else {
                        //printf("debug[%s:%d]:   PSH, x_seq=%u\n", __FILE__, __LINE__, ph->x_seq);
        	        t->r_stat = TUN_R_DATA;
	            }
        	}
                break;
        
            case TUN_R_DATA:
    	        /* 正在读packet的数据
		 *
		 */
                //printf("debug[%s:%d]: TUN_R_DATA\n",  __FILE__, __LINE__);
    	        ph = ( PacketHead *)( t->r_seg.buff );
                want_sz = ph->sz - t->r_seg.sz;
        	rt = putBytesFromFd( &(t->r_seg), t->fd, &want_sz );
        	switch ( rt ) {
        	    case 0:  // socket block
                        
        	    case -1: // socket closed
                    case -2: // socket error
        		return rt;
    		    case 1:  // already read want_sz bytes
    		        break;
    		    default: // -3: buffer is full, no space
                        dprintf(2, "Error[%s:%d]: programing error rt=%d\n",  __FILE__, __LINE__, rt);
        	    	return -4;
        	}
        	if ( ph->sz == t->r_seg.sz ) {
                    //printf("debug[%s:%d]: 读取完Packet，数据如下：\n",  __FILE__, __LINE__);
                    //dumpPacket( (Packet *)(t->r_seg.buff) );
                    //dumpBuff( &(t->r_seg) );
        	    t->r_stat = TUN_R_FULL;
        	}
                break;

            case TUN_R_FULL:
    	        // packet已读完整，尝试向tun2fd这个buffer转移，
    	        // 转移之前要抛弃缓冲区前面的PachetHead。
                //printf("debug[%s:%d]: TUN_R_FULL\n",  __FILE__, __LINE__);
    	        // packet已读完整，尝试将packet data向tun2fd这个buffer转移。
                //printf("debug[%s:%d]: TUN_R_MOVE\n",  __FILE__, __LINE__);
    	        
                rt = _process_seq( p, t );
                if ( rt == -1 ) {
                    return -2; // oom
                }
		if ( rt == 0 ) {
                    t->r_stat = TUN_R_INIT;
    		}
                else {
                    t->r_stat = TUN_R_MOVE;
                }
                break;

            case TUN_R_MOVE:
    	        // packet已读完整，尝试将packet data向tun2fd这个buffer转移。
                //printf("debug[%s:%d]: TUN_R_MOVE1\n",  __FILE__, __LINE__);
    	        
		//printf("debug[%s:%d]: 待转移的packet如下：\n",  __FILE__, __LINE__);
    	        //dumpBuff( &(t->r_seg) );
       	        
                rt = _to_buff( p, t );
                if ( rt == -1 ) {
                    return -2; // oom
                }
		if ( rt == 0 ) {
    		    return 1;
    		}
                t->r_stat = TUN_R_INIT;
		break;

        }
    }
}

/* 将pipe的buffer的数据发向fd，
 * 
 * == description == 
 * 将buffer的数据通过各个tunnel fd发送出去，
 * 每个tunnel fd都有write buffer。
 * 分两种情况：
 * (1) fd2tun数据和PacketHead的bytes大于所有
 *     tunnel buffer bytes之和。
 *     这时应该所有tunnel fd 被写到socket block。
 *
 * (2) fd2tun数据和PacketHead的bytes小于所有
 *     tunnel buffer bytes之和。
 *     这时应该所有tunnel fd 被写到socket block。
 * 
 * 这个函数返回大概有以下几种情况：
 * (1) 出错
 * (2) 
 *
 * == return ==
 *   1: 还有socket 未碰到write block
 *   0: 所有socket 遭遇write block
 *  -1: write socket error
 *  -66: check code
 */
static int buffToTun( Pipe * p ) {
    int         rt;
    int         i;
    int         loop = 1;
    int         sending_count = 0;
    Packet *    packet_;

    assert( p != NULL );

    p->flags &= ( ~P_FLG_DATA );

    // 把每个tunnel fd的发送状态都推进一遍
    for ( i = 0; i < p->tun_list.len; i++ ) {
        if ( isTunActive( p->tun_list.tuns + i ) ) {
	    printf("debug[%s:%d]: buffToTun i=%d fd=%d\n", 
	    		__FILE__, __LINE__, 
	    		i, p->tun_list.tuns[i].fd);
	    loop = 1;
	    while ( loop ) {
                switch ( p->tun_list.tuns[i].w_stat ) {
                    case TUN_W_INIT:
	    	        //printf("debug[%s:%d]: TUN_W_INIT\n", __FILE__, __LINE__);
	                if ( !( hasDataToTun( p ) 
				    || ( p->stat == P_STAT_ENDING && ( p->flags & P_FLG_SEND_FIN ) ) ) ) {
	    	            printf("debug[%s:%d]: 无数据待发\n", __FILE__, __LINE__);
	    	            loop = 0;
	    	            break;
	    	        }

                        //==== 组装packet ====
                	packet_ = (Packet *)(p->tun_list.tuns[i].w_seg.buff);
                        packet_->head.flags = 0;
                        packet_->head.sz = PACKET_HEAD_SZ;
			//== 组装FIN
			if ( p->flags & P_FLG_SEND_FIN ) {
			    packet_->head.flags |= ACTION_FIN;
			    p->flags &= ( ~P_FLG_SEND_FIN );
			}
	    	        //== 组装ACTION_PSH和x_seq
	    	        if ( hasActiveData( &(p->fd2tun) ) ) { 
                            _fillDataInPkt( p, packet_, i );
	    	            printf("debug[%s:%d]: packet - make data: x_seq=%u\n", __FILE__, __LINE__, packet_->head.x_seq); 
	    	        }

                        //seg.head.checksum = ;
                        setBuffSize( &(p->tun_list.tuns[i].w_seg), packet_->head.sz );
	    	    
	    	        printf("debug[%s:%d]: 组装好的待发送packet:\n", __FILE__, __LINE__); 
                        dumpPacket( packet_ );
                        
	    	        p->tun_list.tuns[i].w_stat = TUN_W_SEND;
                	break;
    
                    case TUN_W_SEND:
                        //==== 发送packet
	    	        //printf("debug[%s:%d]: TUN_W_SEND\n", __FILE__, __LINE__); 
                	packet_ = (Packet *)(p->tun_list.tuns[i].w_seg.buff);
                        rt = getBytesToFd( &(p->tun_list.tuns[i].w_seg), p->tun_list.tuns[i].fd );
	    	        //printf("debug[%s:%d]: getBytesToFd: rt=%d\n", __FILE__, __LINE__, rt); 
    	    	        if ( rt == -1 ) { // socket error
	    	            printf("Error[%s:%d]: socket error %s\n", __FILE__, __LINE__, strerror(errno)); 
    	    	            return -1;
    	    	        }
			else if ( rt < -1 ) {
	    	            printf("Fatal[%s:%d]: check code\n", __FILE__, __LINE__); 
			    return -66;
			}

	    	        if ( rt == 0 ) {
	    		    // socket block
	    		    p->tun_list.tuns[i].flags |= FD_WRITE_BLOCK;
			    if ( packet_->head.flags & ACTION_PSH ) {
			        p->flags |= P_FLG_DATA;
			    }
	    		    loop = 0;
			    break;
	    	        }

			// rt == 1

                        _signAsSent( p, packet_->head.x_seq );
                        p->tun_list.tuns[i].flags &= ( ~FD_WRITE_BLOCK );
    	    	        cleanBuff( &(p->tun_list.tuns[i].w_seg) );
                    	p->tun_list.tuns[i].w_stat = TUN_W_INIT;
                        break;
                }
            }

	    if ( p->tun_list.tuns[i].flags & FD_WRITE_BLOCK ) {
	        sending_count++;
	    }
	}
    }

    if ( sending_count == p->tun_list.sz ) { 
        return 1;
    }

    return 0;
}

/* 管道数据流动函数
 *
 * #### Param: 
 * mode:
 *     1 : client to tunnels
 *     2 : tunnels to client
 *
 * == return == 
 *  
 *
 *  2: fd closed
 *  1: socket non-block AND buffer is full
 *  0: socket block AND buffer not full
 * -1: errors, destroy pipe
 */ 
int stream( int mode, Pipe * p, int fd ) { 
    int rt;
    size_t want_sz;
    size_t before_sz;

    assert( p != NULL );
    assert( mode > P_STREAM_BEGIN && mode < P_STREAM_END );
    assert( fd >= 0 );
    
    //printf("debug[%s:%d]: stream p=%p\n", __FILE__, __LINE__, p); 
    switch ( mode ) {
        case P_STREAM_FD2BUFF:
	    // 这个模式下，就是把fd的数据读到buff里去
            
	    if ( p->fd_flags & FD_CLOSED ) {
	        return 2;
	    }

            want_sz = 0;
            rt = putBytesFromFd( &(p->fd2tun), p->fd, &want_sz );
	    printf("Info[%s:%d]: FD -> BUFF read_sz=%lu\n", 
			    __FILE__, __LINE__, want_sz);

	    if ( p->stat == P_STAT_GOT_FIN && want_sz > 0 ) {
                printf("Error[%s:%d]: P_STAT_GOT_FIN, but read %lu bytes from merge fd\n", 
				__FILE__, __LINE__, want_sz );
	        return -1;
	    }

            switch ( rt ) {
                case 0:  // socket block, buffer is not full
                    //printf("debug[%s:%d]: socket block\n", __FILE__, __LINE__); 
	            return 0;
                case 1:  // buffer is full, socket non-block
                case -3: // buffer is full, socket non-block
                    //printf("warning[%s:%d]: buffer is full\n", __FILE__, __LINE__); 
                    return 1;
                case -1: // socket error
                    printf("Error[%s:%d]: merge socket error\n", __FILE__, __LINE__); 
                    return -1;
                case -2: // socket closed
		    p->fd_flags |= FD_CLOSED;
                    printf("debug[%s:%d]: merge socket closed\n", __FILE__, __LINE__); 
                    return 2;
		default:
                    printf("Error[%s:%d]: 不可能发生，心理安慰\n", __FILE__, __LINE__); 
		    return -66;
            }
            break;

        case P_STREAM_BUFF2FD:
	    // 这个模式下，就是把buffer: tun2fd 的数据尽量发送出去

	    if ( p->fd_flags & FD_CLOSED ) {
	        return 2;
	    }
	    
	    before_sz = p->tun2fd.sz;
            rt = getBytesToFd( &(p->tun2fd), p->fd );
	    printf("Info[%s:%d]: BUFF -> FD read_sz=%lu\n", 
			    __FILE__, __LINE__,
			    before_sz - p->tun2fd.sz); 
            switch ( rt ) {
                case 0:  // socket block
                    //printf("debug[%s:%d]: socket block\n", __FILE__, __LINE__); 
                    if ( before_sz > p->tun2fd.sz ) {
	    	        return 21;
	     	    }
	    	    else {
	    	        return 20;
	    	    }
		    break;
                case 1:  // send successfully, buffer is now empty
                    //printf("debug[%s:%d]: send all\n", __FILE__, __LINE__); 
    	            return 22;
                case -1: //socket error
                    printf("Error[%s:%d]: socket error\n", __FILE__, __LINE__); 
    	            return -1;
                case -3: // send return 0,应该不会发生
                    printf("Info[%s:%d]: send return 0\n", __FILE__, __LINE__); 
    	            return -1;
		default:
                    printf("Error[%s:%d]: 不可能发生，心理安慰\n", __FILE__, __LINE__); 
		    return -66;
            }
            break;
	
	case P_STREAM_TUN2BUFF:
	    before_sz = p->tun2fd.sz;
            rt = tunToBuff( fd, p );
	    printf("Info[%s:%d]: TUN -> BUFF write_sz=%lu\n", 
			    __FILE__, __LINE__, 
			    before_sz - p->tun2fd.sz); 
            switch ( rt ) {
		case 2: // end service
	            p->flags |= P_FLG_TUN_FIN;
                    printf("Info[%s:%d]: end service\n", __FILE__, __LINE__); 
		    return 2;
                case 1: // 向tun2fd转移时卡住
                    //printf("debug[%s:%d]: buffer's remaining space maybe not enough\n", __FILE__, __LINE__); 
	            return 31;
                case 0: // socket block
                    //printf("debug[%s:%d]: socket block\n", __FILE__, __LINE__); 
                    return 30;
		case -1:// socket closed
		    return 29;
                case -2:// errors
                    printf("Error[%s:%d]: errors %d %s\n", __FILE__, __LINE__, errno, strerror(errno) ); 
	            return -1;
		default:
                    printf("Error[%s:%d]: 不可能发生，心理安慰\n", __FILE__, __LINE__); 
		    return -66;
            }
            break;

	case P_STREAM_BUFF2TUN:
	    /* 这个模式下，是把buffer的数据写向各个tunnel fd。
	     *
	     * 
	     *
	     *
	     *
	     */
	    
	    before_sz = p->fd2tun.sz;
            rt = buffToTun( p );
            printf("Info[%s:%d]: BUFF -> TUN write_sz=%lu\n", 
			    __FILE__, __LINE__, 
			    before_sz - p->fd2tun.sz); 
            switch ( rt ) {
		case 1: // 还有socket未碰到write block，但fd2tun没数据了
		    return 41;
                case 0: // 所有socket遭遇write block
                    return 40;
                case -1:// write socket error
	            return -1;
		default:// -66
		    return -66;
            }
	    break;
    }
}


void initPipeList( PipeList * pl ) {
    int i;
    assert( pl != NULL );
    bzero( (void *)pl, sizeof(PipeList) );
    for ( i = 0; i < P_LIST_SZ; i++ ) {
        pl->pipes[i].idx = i;
    }
}

void destroyPipeList( PipeList * pl ) {
    assert( pl != NULL );
    bzero( (void *)pl, sizeof(PipeList) );
}

int isPipeListEmpty( PipeList * pl ) {
    assert( pl != NULL );

    return pl->sz == 0;
}

int isPipeListFull( PipeList * pl ) {
    assert( pl != NULL );

    return pl->sz >= P_LIST_SZ;
}

/*
 * == return ==
 * 一个下标。
 *  >=0: 
 *   -1: full
 *   -2: memory not enough
 *  -66: code error
 */
int getAEmptyPipe( PipeList * pl ) {
    int i;
    assert( pl != NULL );
    
    if ( isPipeListFull( pl ) ) {
        return -1;
    }

    for ( i = 0; i < P_LIST_SZ; i++ ) {
        if ( !(pl->pipes[i].use) ) {
	    if ( initPipe( pl->pipes + i, P_BUFF_SZ, TUN_LIST_SZ ) ) {
	        return -2;
	    }
	    pl->pipes[i].use = 1;
	    pl->sz++;
	    //printf("==> empty pipe i=%d\n", i);
	    return i;
	}
    }

    printf("Fatal[%s:%d]: check codes\n", __FILE__, __LINE__ );
    return -66;
}

/*
 * == return ==
 *  0: success
 * -1: pipe list is full
 */
int addPipe( PipeList * pl, Pipe * p, Pipe ** rt ) {
    int i;
    assert( pl != NULL );
    assert( p != NULL );

    if ( isPipeListFull( pl ) ) {
        return -1;
    }
    
    for ( i = 0; i < P_LIST_SZ ; i++ ) {
	// get a white seat, store the data in it, then break
        if ( !( pl->pipes[i].use ) ) {
	    pl->pipes[i] = ( *p );
	    pl->pipes[i].use = 1;
	    pl->sz += 1;
	    if ( rt ) {
	        printf("==> addPipe i=%d\n", i);
	        *rt = pl->pipes + i;
	    }
	    break;
	}
    }

    return 0;
}

/*
 * == return ==
 *  0: deleted
 * -1: list is emtpy
 * -2: not found
 */
int delPipeByKey( PipeList * pl, char * key ) {
    int i;
    assert( pl != NULL );
    assert( key != NULL );

    if ( isPipeListEmpty( pl ) ) {
        return -1;
    }

    for ( i = 0; i < P_LIST_SZ ; i++ ) {
        if ( pl->pipes[i].use != 0 && 0 == memcmp( (void *)(pl->pipes[i].key), (void *)key, P_KEY_SZ ) ) {
	    destroyPipe( pl->pipes + i );
	    pl->pipes[i].use = 0;
	    pl->sz -= 1;
	    return 0;
	}
    }

    return -2;
}

/*
 * == return ==
 *  0: deleted
 * -1: list is emtpy
 * -2: fd's pipe not found
 */
int delPipeByFd( PipeList * pl, int fd ) {
    int i;
    assert( pl != NULL );

    if ( isPipeListEmpty( pl ) ) {
        return -1;
    }

    for ( i = 0; i < P_LIST_SZ ; i++ ) {
        if ( pl->pipes[i].use && pl->pipes[i].fd == fd ) {
	    destroyPipe( pl->pipes + i );
	    pl->pipes[i].use = 0;
	    pl->sz -= 1;
	    return 0;
	}
    }

    return -2;
}

void delPipeByI( PipeList * pl, int i ) {
    assert( pl != NULL );

    if ( pl->pipes[i].use ) {
        destroyPipe( pl->pipes + i );
        pl->pipes[i].use = 0;
        pl->sz -= 1;
    }
}

Pipe * searchPipeByKey( PipeList * pl, char * key) {
    int i;

    assert( pl != NULL );
    assert( key != NULL );

    if ( isPipeListEmpty( pl ) ) {
        return NULL;
    }

    for ( i = 0; i < P_LIST_SZ; i++ ) {
        if ( pl->pipes[i].use && 0 == memcmp( (void *)(pl->pipes[i].key), (void *)key, P_KEY_SZ ) ) {
	    printf("==> searchPipeByKey i=%d\n", i);
	    return pl->pipes + i;
	}
    }

    return NULL;
}
