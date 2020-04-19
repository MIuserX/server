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
    assert( p != NULL );
    assert( sz > 0 );
   
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
    initLine( &(p->ack_sending_list) );

    p->fd = -1;
    p->tun_closed = 'n';
    p->unsend_count = 0;

    return 0;
}

void cleanPipe( Pipe * p ) {
     assert( p != NULL );
     p->use = 0;
}

void destroyPipe( Pipe * p ) {
    assert( p != NULL );
    
    if ( p->fd != -1 ) {
        close( p->fd );
    }

    destroyTunList( &(p->tun_list) );
    destroyBuff( &(p->fd2tun) );
    destroyBuff( &(p->tun2fd) );
    
    bzero( (void *)p, sizeof(Pipe) );
}

int hasUnSendAck( Pipe * p ) {
    assert( p != NULL );
    return p->unsend_count > 0;    
}

int hasDataToTun( Pipe * p ) {
    assert( p != NULL );
    return hasActiveData( &(p->fd2tun) ) || hasUnSendAck( p ); 
}

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
}

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

/* 为pipe的fd创建到 xx 的socket连接．
 * 
 * == param ==
 * 
 *
 * == return ==
 *  0: success
 * -1: create socket failed
 * -2: p->fd may not be closed
 * -3: connect failed
 * -4: set nonblocking failed
 */
int connectFd( Pipe * p, struct sockaddr_in to_addr ) {
    int fd;

    assert( p != NULL );

    if ( p->fd != -1 ) {
        return -2;
    }

    fd = socket( AF_INET, SOCK_STREAM, 0 );

    if ( fd == -1 ) {
        return -1;
    }

    if ( connect( fd, (struct sockaddr *)(&to_addr), sizeof(to_addr) ) == -1 ) {
        return -3;
    }
    
    if ( setnonblocking( fd ) ) {
        return -4;
    }

    p->fd = fd;
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
 *  1: buffer not enough
 *  0: socket block
 * -1: errors
 */
static int tunToBuff( int evt_fd, Pipe * p ) {
    int       i;  // loop variable
    int       rt; // ret of function
    int       ret = 1;
    size_t    want_sz;
    UnSendAck usa;
    Packet *  pkt;

    assert( evt_fd >= 0 );
    assert( p != NULL );

    //==== read evt_fd
    for ( i = 0; i < p->tun_list.len; i++ ) {
        if ( p->tun_list.tuns[i].status == TUN_AUTHED && 
		p->tun_list.tuns[i].fd == evt_fd ) {
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

	    if ( rt == 2 ) {
	        for ( i = 0; i < p->tun_list.len; i++ ) {
		    p->tun_list.tuns[i].status = TUN_CLOSED;
		}
	    }

	    if ( rt > -1 ) {
	        ret = rt;
		break;
	    }
	    else {
	        return -1;
	    }
	    /*
	    switch ( rt ) {
		case 2:
		    ret = 2
	            break;

		case 1:
		    // buffer空间不足
		    ret = 1;
		    break;

		case 0:
		    // socket block
		    // 这时已经将读事件耗尽，可以返回。
		    ret = 0;
		    break;

		default:
		    return -1;
		    break;
	    }*/
	}
    }

    //==== 尝试转移seq靠后的包
    if ( p->prev_seglist.len > 0 ) {
        printf("debug[%s:%d]: 存在提前收到靠后的packet\n",  __FILE__, __LINE__);
	pkt = (Packet *)getHeadPtr( &(p->prev_seglist) );
        if ( pkt->head.x_seq == p->last_recv_seq + 1 && 
		( p->tun2fd.len - p->tun2fd.sz ) >= ( pkt->head.sz - PACKET_HEAD_SZ ) ) {
            /* 如果 tun2fd 的 buffer 剩余空间够，就转移packet数据，
             * 如果不够，这次就不转移了。
             * 主要是保证不卡在转移数据上。
             */
            printf("debug[%s:%d]: 提前收到的靠后的packet可以向buffer转移\n",  __FILE__, __LINE__);
            want_sz = pkt->head.sz - PACKET_HEAD_SZ;
            rt = putBytes( &(p->tun2fd), pkt->data, &want_sz );
            if ( rt ==  0 ) {
                printf("debug[%s:%d]: 转移完成后tun2fd情况：\n",  __FILE__, __LINE__);
                dumpBuff( &(p->tun2fd) );
                        
                usa.sending = 'n';
                usa.seq = pkt->head.x_seq;
                if ( seqInLine( &(p->ack_sending_list), (void *)&usa, sizeof(usa), cmp ) ) {
                    return -1;
                }
                p->unsend_count++;
                
    	        removeFromLine( &(p->prev_seglist), pkt, matchPacketSeq );
    
                p->last_recv_seq = pkt->head.x_seq;
            }
            else {
                dprintf(2, "Error[%s:%d]: programing error rt=%d\n",  __FILE__, __LINE__, rt);
                return -1;
            }
        }
        printf("debug[%s:%d]: 提前收到的靠后的packet无法向buffer转移\n",  __FILE__, __LINE__);
    }

    return ret;
}

/* 从 tunnels 向 pipe 里读数据
 *
 * 当tunnels的fd有了读事件才会调用这函数。
 *
 * == return ==
 *  2: end service
 *  1: buffer remaining space maybe not enough
 *  0: socket block
 * -1: socket closed
 * -2: socket error
 * -3: 申请内存失败
 * -4: X
 */
static int _tunToBuff( int i, Pipe * p ) {
    size_t         want_sz;
    int            rt;
    unsigned int * pu;
    PacketHead *   ph;
    PacketHead *   phead;
    UnSendAck      usa;
    
    assert( p != NULL );

    while ( 1 ) {
        switch ( p->tun_list.tuns[i].r_stat ) {
            case TUN_R_INIT:
		/* 正在读packet的head
		 *
		 *
		 */
                if ( p->tun_list.tuns[i].status == TUN_CLOSED ) {
		    return 2;
		}

                if ( ( p->tun2fd.len - p->tun2fd.sz ) < ( PACKET_MAX_SZ - PACKET_HEAD_SZ ) ) {
                    return 1;
                }

    	        cleanBuff( &(p->tun_list.tuns[i].r_seg) );
                    
    	        // 读取PacketHead时也可能中途遇到socket block，
    	        // 所以这里待读取size是，PacketHead size减去已读取size。
                want_sz = PACKET_HEAD_SZ - p->tun_list.tuns[i].r_seg.sz;
        	rt = putBytesFromFd( &(p->tun_list.tuns[i].r_seg), p->tun_list.tuns[i].fd, &want_sz );
                printf("debug[%s:%d]: after rt=%d want_sz=%lu\n",  __FILE__, __LINE__, rt, want_sz);
        	switch ( rt ) {
        	    case -1: // socket closed
                    case -2: // socket error
        	    case 0:  // socket block
        		return rt;
    		    case 1:  // already read want_sz bytes
    		        break;
    		    default: // -3: buffer is full, no space
                        dprintf(2, "Error[%s:%d]: programing error rt=%d\n",  __FILE__, __LINE__, rt);
        		return -4;
        	}

	        if ( PACKET_HEAD_SZ == p->tun_list.tuns[i].r_seg.sz ) {
    	            ph = ( PacketHead *)( p->tun_list.tuns[i].r_seg.buff);
		    if ( ph->flags & ACTION_FIN ) {
                        p->tun_list.tuns[i].status = TUN_CLOSED;
                        p->tun_list.tuns[i].r_stat = TUN_R_INIT;
		    }
		    else {
                        printf("debug[%s:%d]: 读取完PacketHead: \n", __FILE__, __LINE__);
		        if ( ph->flags & ACTION_SYN ) {
                            printf("debug[%s:%d]:   SYN\n", __FILE__, __LINE__);
			    p->last_recv_seq = ph->x_seq - 1;
			    p->last_recv_ack = ph->x_seq - 1;
			}
		        if ( ph->flags & ACTION_ACK ) {
                            printf("debug[%s:%d]:   ACK, x_ack=%u\n", __FILE__, __LINE__, ph->x_ack);
			}

			if ( PACKET_HEAD_SZ == ph->sz ) {
                            p->tun_list.tuns[i].r_stat = TUN_R_FULL;
    	                }
			else {
                            printf("debug[%s:%d]:   PSH, x_seq=%u\n", __FILE__, __LINE__, ph->x_seq);
        	            p->tun_list.tuns[i].r_stat = TUN_R_DATA;
			}
		    }
        	}
                break;
        
            case TUN_R_DATA:
    	        /* 正在读packet的数据
		 *
		 *
		 *
		 */
                printf("debug[%s:%d]: TUN_R_DATA\n",  __FILE__, __LINE__);
    	        ph = ( PacketHead *)( p->tun_list.tuns[i].r_seg.buff );
                want_sz = ph->sz - p->tun_list.tuns[i].r_seg.sz;
        	rt = putBytesFromFd( &(p->tun_list.tuns[i].r_seg), p->tun_list.tuns[i].fd, &want_sz );
        	switch ( rt ) {
        	    case -1: // socket closed
                    case -2: // socket error
        	    case 0:  // socket block
        		return rt;
    		    case 1:  // already read want_sz bytes
    		        break;
    		    default: // -3: buffer is full, no space
                        dprintf(2, "Error[%s:%d]: programing error rt=%d\n",  __FILE__, __LINE__, rt);
        	    	return -4;
        	}
        	if ( ph->sz == p->tun_list.tuns[i].r_seg.sz ) {
                    printf("debug[%s:%d]: 读取完Packet，数据如下：\n",  __FILE__, __LINE__);
                    dumpPacket( (Packet *)(p->tun_list.tuns[i].r_seg.buff) );
                    dumpBuff( &(p->tun_list.tuns[i].r_seg) );
        	    p->tun_list.tuns[i].r_stat = TUN_R_FULL;
        	}
                break;

            case TUN_R_FULL:
    	        // packet已读完整，尝试向tun2fd这个buffer转移，
    	        // 转移之前要抛弃缓冲区前面的PachetHead。
                printf("debug[%s:%d]: TUN_R_FULL\n",  __FILE__, __LINE__);
    	    
    	        ph = ( PacketHead *)( p->tun_list.tuns[i].r_seg.buff);
    	        if ( ph->flags & ACTION_ACK ) {
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
                        printf("debug[%s:%d]: ackBytes x_ack=%u \n", 
					__FILE__, __LINE__, ph->x_ack);

			while ( p->prev_acklist.len > 0 ) {
			    pu = getHeadPtr( &(p->prev_acklist) );
			    if ( *pu == p->last_send_ack + 1 ) {
    	                        ackBytes( &(p->fd2tun), *pu );
			        p->last_send_ack = (*pu);
			        justOutLine( &(p->prev_acklist) );
                                printf("debug[%s:%d]: ackBytes x_ack=%u \n", 
						__FILE__, __LINE__, *pu);
			    }
			    else {
			        break;
			    }
			}
		    }
		    else if ( ph->x_ack > p->last_send_ack + 1 ) {
                        printf("debug[%s:%d]: prev x_ack=%u, last_send_ack=%u\n", 
					__FILE__, __LINE__, 
					ph->x_ack, 
					p->last_send_ack);
		        if ( seqInLine( &(p->prev_acklist), (void *)&(ph->x_ack), sizeof(ph->x_ack), ackCmp ) ) {
		            return -3;
			}
		    }
		    else { // case 1
		        // 理论上不可能发生，除非对端重发了ACK
		    }
		    
                    if ( ph->sz == PACKET_HEAD_SZ ) {
    	                 p->tun_list.tuns[i].r_stat = TUN_R_INIT;
		         break;
		    }
    	        }
		if ( ph->flags & ACTION_PSH ) {
    	            want_sz = PACKET_HEAD_SZ;
    	            discardBytes( &(p->tun_list.tuns[i].r_seg), &want_sz );
		    p->tun_list.tuns[i].r_stat = TUN_R_MOVE;
		}
    	        break;

    	    case TUN_R_MOVE:
    	        // packet已读完整，尝试将packet data向tun2fd这个buffer转移。
                printf("debug[%s:%d]: TUN_R_MOVE\n",  __FILE__, __LINE__);
    	        
		ph = ( PacketHead *)( p->tun_list.tuns[i].r_seg.buff);
                
		printf("debug[%s:%d]: 待转移的packet如下：\n",  __FILE__, __LINE__);
    	        dumpBuff( &(p->tun_list.tuns[i].r_seg) );
       	        
		if ( ph->x_seq == p->last_recv_seq + 1 ) {
	            //==> 收到了想要的packet
    	            /* 如果 tun2fd 的 buffer 剩余空间够，就转移packet数据，
		     * 如果不够，这次就不转移了。
		     * 主要是保证不卡在转移数据上。
		     */
		    want_sz = 0;
    	            rt = putBytesFromBuff( &(p->tun2fd), &(p->tun_list.tuns[i].r_seg), &want_sz );
    	            switch ( rt ) {
    		        case 0:
    		            if ( isBuffEmpty( &(p->tun_list.tuns[i].r_seg) ) ) {
                                printf("debug[%s:%d]: 转移完成后tun2fd情况：\n",  __FILE__, __LINE__);
    	                        dumpBuff( &(p->tun2fd) );
    		                
		        	usa.sending = 'n';
		        	usa.seq = ph->x_seq;
		                if ( seqInLine( &(p->ack_sending_list), (void *)&usa, sizeof(usa), cmp ) ) {
		        	    return -3;
		        	}
		        	p->unsend_count++;
		        
                                printf("debug[%s:%d]: p->unsend_count=%d\n", __FILE__, __LINE__, p->unsend_count);
		        	p->last_recv_seq = ph->x_seq;
    		            } 
		    	    else {
                                dprintf(2, "Error[%s:%d]: programing error rt=%d\n",  __FILE__, __LINE__, rt);
    		                return -4;
    		            }
    		            break;
    		        default: // -1, -2
                            dprintf(2, "Error[%s:%d]: programing error rt=%d\n",  __FILE__, __LINE__, rt);
        	            return -4;
    		    }
    	        }
		else if ( ph->x_seq > p->last_recv_seq + 1 ) {
		    //==> 提前收到了更靠后的packet
	            if ( ph->x_seq <= p->last_recv_seq + P_PREV_RECV_MAXSZ ) {
    	                // 如果当前的seq大于等于last_recv_seq+1，
	                // 先把packet存入prev_seglist 队列。
			if ( seqInLine( &(p->prev_seglist), 
					(void *)(p->tun_list.tuns[i].r_seg.buff), 
					p->tun_list.tuns[i].r_seg.sz, 
					pktCmp ) ) {
    	                    return -3;
	                }
	            }
	            else { 
		       // ( ph->x_seq > p->last_recv_seq + P_PREV_RECV_MAXSZ )
	               // 理论上不会发生，因为sender发送未确认队列最大长度是3，
	               // receiver最多预先收两个包。
                       dprintf(2, "Error[%s:%d]: 理论上不可能，please check codes\n",  __FILE__, __LINE__);
		    }
    	        }
		else { // ph->x_seq < p->last_recv_seq + 1
    	            //==> 收到了更靠前的包
		    // 如果发生了这种情况，则表示sender发送了该seq的packet，
		    // 未收到receiver的ack，直到超时，sender重发了该数据。
    	            // receiver需要回复ack，以免sender继续重发。
                    dprintf(2, "Error[%s:%d]: receiver需要回复ack，以免sender继续重发\n",  __FILE__, __LINE__);
    	            return -4;
    	        }
                p->tun_list.tuns[i].r_stat = TUN_R_INIT;
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
 *   1: 还有发送能力
 *   0: 所有socket 遭遇write block 或到了发送上限
 *  -1: errors
 */
static int buffToTun( Pipe * p ) {
    int         rt;
    int         i;
    int         loop = 1;
    Packet *    packet_;
    size_t      seg_sz;
    UnSendAck * usap;
    UnSendAck   usa;

    assert( p != NULL );

    p->tun_list.sending_count = 0;

    // 把每个tunnel fd的发送状态都推进一遍
    for ( i = 0; i < p->tun_list.len; i++ ) {
	printf("debug[%s:%d]: buffToTun i=%d fd=%d\n", 
			__FILE__, __LINE__, 
			i, p->tun_list.tuns[i].fd);
	loop = 1;
	while ( loop ) {
            switch ( p->tun_list.tuns[i].w_stat ) {
                case TUN_W_INIT:
		    printf("debug[%s:%d]: TUN_W_INIT\n", __FILE__, __LINE__);
	            if ( !( hasDataToTun( p ) || p->stat == P_STAT_ENDING1 ) 
			    || p->fd2tun.buff2segs.len >= P_PREV_SEND_MAXSZ ) {
		        // 如果没有 active data 和 ack 可发就break
			// 如果已发送且未收到ack的packet数大于P_PREV_SEND_MAXSZ就break
			// 如果没有FIN需要发，就break
			loop = 0;
			break;
		    }
		    if ( hasDataToTun( p ) ) {
		        printf("debug[%s:%d]: hasDataToTun\n", __FILE__, __LINE__);
			dumpBuff( &(p->fd2tun) );
		        printf("debug[%s:%d]: p->unsend_count=%d\n", 
					__FILE__, __LINE__, 
					p->unsend_count);
		    }

                    //==== 组装packet ====
            	    packet_ = (Packet *)(p->tun_list.tuns[i].w_seg.buff);
                    packet_->head.flags = 0;
                    packet_->head.sz = PACKET_HEAD_SZ;
	            //== 组装ACTION_ACK和x_ack
		    if ( hasUnSendAck( p ) ) {
                        // 获取一个ack seq
			getNode( &(p->ack_sending_list), (void **)(&usap), matchUnAck, NULL );
			p->unsend_count--;
			usap->sending = 'y';
                        // 组装packet
			packet_->head.flags |= ACTION_ACK;
                        packet_->head.x_ack = usap->seq;
		        
			printf("debug[%s:%d]: packet - make ack: x_ack=%u\n", __FILE__, __LINE__, packet_->head.x_ack); 
		    }
		    //== 组装ACTION_PSH和x_seq
		    if ( hasActiveData( &(p->fd2tun) ) ) { 
                        packet_->head.flags |= ACTION_PSH;

			if ( p->last_send_seq == 0 ) {
		            p->last_send_seq = random() % 66 + 1;
			    p->last_send_ack = p->last_send_seq;
			    packet_->head.flags |= ACTION_SYN;
			}
                        packet_->head.x_seq = ( ++(p->last_send_seq) );
    
                        seg_sz = PACKET_DATA_SZ;
                        rt = preGetBytes( &(p->fd2tun), packet_->data, &seg_sz, packet_->head.x_seq ); 
                        if ( -3 == rt ) {
		            return -1;
                        }
                        packet_->head.sz += seg_sz;
		        printf("debug[%s:%d]: packet - make data: x_seq=%u\n", __FILE__, __LINE__, packet_->head.x_seq); 
		    }

		    //== 发送FIN
		    if ( !( packet_->head.flags & ACTION_PSH )
			    && !( packet_->head.flags & ACTION_ACK )
		            && p->stat == P_STAT_ENDING1 ) {
		        packet_->head.flags |= ACTION_FIN;
			p->stat == P_STAT_ENDING2;
		    }
    
                    //seg.head.checksum = ;
                    setBuffSize( &(p->tun_list.tuns[i].w_seg), packet_->head.sz );
		    
		    printf("debug[%s:%d]: 组装好的待发送packet:\n", __FILE__, __LINE__); 
                    dumpPacket( packet_ );
                    
		    p->tun_list.tuns[i].w_stat = TUN_W_SEND;
            	    break;
    
                case TUN_W_SEND:
                    //==== 发送packet
		    printf("debug[%s:%d]: TUN_W_SEND\n", __FILE__, __LINE__); 
            	    packet_ = (Packet *)(p->tun_list.tuns[i].w_seg.buff);
                    rt = getBytesToFd( &(p->tun_list.tuns[i].w_seg), p->tun_list.tuns[i].fd );
		    printf("debug[%s:%d]: getBytesToFd: rt=%d\n", __FILE__, __LINE__, rt); 
    		    if ( rt == -1 ) { // socket error
		        printf("Error[%s:%d]: socket error %s\n", __FILE__, __LINE__, strerror(errno)); 
    		        return -1;
    		    }

		    if ( rt == 0 ) {
			// socket block
			p->tun_list.tuns[i].flags |= FD_WRITE_BLOCK;
			loop = 0;
		    }
		    else {
			p->tun_list.tuns[i].flags &= ( ~FD_WRITE_BLOCK );
		    }

                    if ( isBuffEmpty( &(p->tun_list.tuns[i].w_seg) ) ) {
                        if ( packet_->head.flags & ACTION_ACK ) {
			    usa.seq = packet_->head.x_ack;
			    if ( p->last_recv_ack + 1 == usa.seq ) {
			        p->last_recv_ack = usa.seq;
			        removeFromLine( &(p->ack_sending_list), &usa, matchSeq );
			    }
			}

                        if ( packet_->head.flags & ACTION_FIN ) {
			    p->stat = P_STAT_END;
			}

    		        cleanBuff( &(p->tun_list.tuns[i].w_seg) );
                	p->tun_list.tuns[i].w_stat = TUN_W_INIT;
                    }
                    break;
            }
        }

	if ( p->tun_list.tuns[i].flags & FD_WRITE_BLOCK ) {
	    p->tun_list.sending_count++;
	}
    }

    if ( p->tun_list.sending_count == p->tun_list.len || p->fd2tun.buff2segs.len >= P_PREV_SEND_MAXSZ ) { 
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
 *  3: service ending
 *  2: fd closed
 *  1: socket non-block AND buffer is full
 *  0: socket block AND buffer not full
 * -1: errors
 */ 
int stream( int mode, Pipe * p, int fd ) { 
    int rt;
    size_t want_sz;
    size_t before_sz;

    assert( p != NULL );
    assert( mode > P_STREAM_BEGIN && mode < P_STREAM_END );
    assert( fd >= 0 );
    
    printf("debug[%s:%d]: stream p=%p\n", __FILE__, __LINE__, p); 
    switch ( mode ) {
        case P_STREAM_FD2BUFF:
	    // 这个模式下，就是把fd的数据读到buff里去
	    printf("debug[%s:%d]: mode=P_STREAM_FD2BUFF\n", __FILE__, __LINE__); 
            
	    if ( p->fd_flags & FD_CLOSED ) {
	        return 2;
	    }

            want_sz = 0;
            rt = putBytesFromFd( &(p->fd2tun), p->fd, &want_sz );
            printf("debug[%s:%d]: putBytesFromFd read_sz=%lu rt=%d\n", __FILE__, __LINE__, want_sz, rt); 
            switch ( rt ) {
                case 0:  // socket block, buffer is not full
                    printf("debug[%s:%d]: socket block\n", __FILE__, __LINE__); 
	            return 0;
                case 1:  // buffer is full, socket non-block
                case -3: // buffer is full, socket non-block
                    printf("debug[%s:%d]: buffer is full\n", __FILE__, __LINE__); 
                    return 1;
                case -1: // socket error
                    printf("Error[%s:%d]: socket error\n", __FILE__, __LINE__); 
                    return -1;
                case -2: // socket closed
		    p->fd_flags |= FD_CLOSED;
                    printf("debug[%s:%d]: socket closed\n", __FILE__, __LINE__); 
                    return 2;
		default:
                    printf("Error[%s:%d]: 不可能发生，心理安慰\n", __FILE__, __LINE__); 
		    return -66;
            }
            break;

        case P_STREAM_BUFF2FD:
	    // 这个模式下，就是把buffer: tun2fd 的数据尽量发送出去
	    printf("debug[%s:%d]: mode=P_STREAM_BUFF2FD\n", __FILE__, __LINE__); 

	    if ( p->fd_flags & FD_CLOSED ) {
	        return 2;
	    }
	    
	    before_sz = p->tun2fd.sz;
            rt = getBytesToFd( &(p->tun2fd), p->fd );
            printf("debug[%s:%d]: getBytesToFd write_sz=%lu\n", 
			    __FILE__, __LINE__, 
			    before_sz - p->tun2fd.sz); 
            switch ( rt ) {
                case 0:  // socket block
                    printf("debug[%s:%d]: socket block\n", __FILE__, __LINE__); 
                    if ( before_sz > p->tun2fd.sz ) {
	    	        return 21;
	     	    }
	    	    else {
	    	        return 20;
	    	    }
		    break;
                case 1:  // send successfully, buffer is now empty
                    printf("debug[%s:%d]: send all\n", __FILE__, __LINE__); 
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
	    printf("debug[%s:%d]: mode=P_STREAM_TUN2BUFF\n", __FILE__, __LINE__); 
	    
	    if ( p->tun_closed == 'y' ) {
	        return 32;
	    }

	    // if fd != p->fd, then tun_fd has read event
            rt = tunToBuff( fd, p );
	    printf("debug[%s:%d]: tunToBuff rt=%d\n", __FILE__, __LINE__, rt); 
            switch ( rt ) {
		case 2: // end service
                    printf("Info[%s:%d]: end service\n", __FILE__, __LINE__); 
		    return 32;
                case 1: // buffer's remaining space maybe not enough
                    printf("debug[%s:%d]: buffer's remaining space maybe not enough\n", __FILE__, __LINE__); 
	            return 31;
                case 0: // socket block
                    printf("debug[%s:%d]: socket block\n", __FILE__, __LINE__); 
                    return 30;
                case -1:// errors
                    printf("Error[%s:%d]: errors %d %s\n", __FILE__, __LINE__, errno, strerror(errno) ); 
	            return rt;
		default:
                    printf("Error[%s:%d]: 不可能发生，心理安慰\n", __FILE__, __LINE__); 
		    return -66;
            }
            break;

	case P_STREAM_BUFF2TUN:
	    
	    if ( p->tun_closed == 'y' ) {
	        return 40;
	    }
	    /* 这个模式下，是把buffer的数据写向各个tunnel fd。
	     *
	     * 
	     *
	     *
	     *
	     */
	    printf("debug[%s:%d]: mode=P_STREAM_BUFF2TUN\n", __FILE__, __LINE__); 
	    
            rt = buffToTun( p );
            printf("debug[%s:%d]: buffToTun rt=%d\n", __FILE__, __LINE__, rt); 
            switch ( rt ) {
		case 1: // 还有发送能力
		    return 41;
                case 0: // 无法再发送
		    // (1) fd2tun已空
		    // (2) 遭遇发送packet数量上限
		    // (3) 所有socket遭遇write block
                    return 40;
                case -1:// error
	            return -1;
		default:
                    printf("Error[%s:%d]: 不可能发生，心理安慰\n", __FILE__, __LINE__); 
		    return -66;
            }
	    break;
    }
}


void initPipeList( PipeList * pl ) {
    assert( pl != NULL );
    bzero( (void *)pl, sizeof(PipeList) );
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
 *   -3: code error
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
	    printf("==> empty pipe i=%d\n", i);
	    return i;
	}
    }
    return -3;
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
