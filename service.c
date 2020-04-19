#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <pthread.h>

#include "tunnel.h"
#include "pipe.h"
#include "auth.h"
#include "server.h"

static int _authToServer( Tunnel * , int *, int , Pipe * );

/* 这函数要认证多个socket，
 * 我们认为所有都认证成功才是认证成功，
 * 否则就算失败。
 *
 * == return ==
 *  2: auth successfully
 *  1: service failed, ecode will be set
 *  0: socket block
 *     nread == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)
 *     或者
 *     nwrite == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)
 */
int authToServer( Pipe * p, int * ecode ) {
    int rt = 0;
    int i = 0;

    assert( p != NULL );
    assert( ecode != NULL );

    /* 得等 tunnel[0] 的 AUTH_NEW 成功了，再说后续的。
     */
    if ( p->tun_list.tuns[0].status != TUN_AUTHED ) {
        rt = _authToServer( p->tun_list.tuns, ecode, AUTH_NEW, p );
	if ( rt < 0 ) {
            dprintf(2, "Error[%s:%d]: auth tunnel[0] error, errno=%d, %s\n", __FILE__, __LINE__, errno, strerror(errno));
	    return 1;
	}
	if ( rt == 2 ) {
            dprintf(2, "Error[%s:%d]: auth tunnel[0] error, errcode=%d\n", __FILE__, __LINE__, *ecode);
	    return 1;
	}
	if ( rt == 0 ) {
	    return 0;
	}

	// rt == 3
        printf("Info[%s:%d]: tunnel[0] auth success\n", __FILE__, __LINE__);
        p->tun_list.auth_ok += 1;
    }

    /* 哪个有事件，处理哪个。
     */
    for ( i = 1; i < p->tun_list.len; i++ ) {
	if ( p->tun_list.tuns[i].status != TUN_AUTHED ) {
            rt = _authToServer( p->tun_list.tuns + i, ecode, AUTH_JOIN, p );
	    if ( rt < 0 ) {
                dprintf(2, "Error[%s:%d]: auth tunnel[%d] error, errno=%d, %s\n", __FILE__, __LINE__, i, errno, strerror(errno));
	        return 1;
	    }
	    if ( rt == 2 ) {
                dprintf(2, "Error[%s:%d]: auth tunnel[%d] error, errcode=%d\n", __FILE__, __LINE__, i, *ecode);
	        return 1;
	    }
	    if ( rt == 3 ) {
                printf("Info[%s:%d]: tunnel[%d] auth success\n", __FILE__, __LINE__, i);
	        p->tun_list.auth_ok += 1;
	    }
	}
    }

    if ( p->tun_list.auth_ok == p->tun_list.len ) {
        return 2;
    }

    return 0;
}

/*
 *
 * == return ==
 *  3: auth successfully
 *  2: service failed, ecode will be set
 *  0: socket block
 *     nread == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)
 *     或者
 *     nwrite == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)
 * -1: socket error
 *     nread == -1 && errno != EAGAIN && errno != EWOULDBLOCK
 *     或者
 *     nwrite == -1 && errno != EAGAIN && errno != EWOULDBLOCK
 * -2: peer closed socket, nread == 0
 * -3: allocate memory failed
 */
static int _authToServer( Tunnel * t, int * ecode, int type, Pipe * p ) {
    int        rt;
    size_t     want_sz;
    AuthPacket ap;
    size_t     ap_sz = sizeof(ap);

    assert( p != NULL );
    assert( ecode != NULL );

    //====1) send auth packet

    while ( 1 ) {
        switch ( t->status ) {
            case TUN_ACTIVE:
                printf("debug: will send auth packet\n");
                // clean buffer
                cleanBuff( &(p->ap) );
                // prepare auth packet
                ap.code = type;
                memcpy( (void *)(ap.key), (void *)(p->key), P_KEY_SZ );
                putBytes( &(p->ap), (char *)(&ap), &ap_sz );
                t->status = TUN_SEND_AP;
                break;

	    case TUN_SEND_AP:
                while ( ! isBuffEmpty( &(p->ap) ) ) {
                    rt = getBytesToFd( &(p->ap), t->fd );
                    if ( rt == 1 && isBuffEmpty( &(p->ap) ) ) {
                        // sent successfully
                        t->status = TUN_RECV_AP;
                        printf("debug: auth packet sent\n");
                        break;
                    }
                    if ( rt == -1 ) {
                        dprintf(2, "Error[%s:%d]: socket error, errno=%d, %s\n", __FILE__, __LINE__, errno, strerror(errno));
                    }
                    return rt; // 0, -1
                }
	        break;

	    case TUN_RECV_AP:
                //====2) read server reply
                printf("debug: will read auth reply\n");
                while ( ! isBuffFull( &(p->ap) ) ) {
		    want_sz = 0;
                    rt = putBytesFromFd( &(p->ap), t->fd, &want_sz );
                    if ( isBuffFull( &(p->ap) ) ) {
                        // buffer is full
                        printf("debug: auth packet read\n");
                        break;
                    }

                    switch ( rt ) {
                        case -1:
                            dprintf(2, "Error[%s:%d]: nread=-1, errno=%d, %s\n", __FILE__, __LINE__, errno, strerror(errno));
                            break;
                        case -2:
                            printf("Warning[%s:%d]: nread=0, peer closed, %s\n", __FILE__, __LINE__, strerror(errno));
                    	break;
                    }

                    return rt; // 0, -1, -2
                }
                
                //====3) check auth result
                printf("debug: check auth result\n");
                *ecode = 0;
                getBytes( &(p->ap), (char *)(&ap), &ap_sz );
                if ( ap.code != AUTH_OK ) {
                    *ecode = ap.code;
                    return 2;
                }
		t->status = TUN_AUTHED;
                return 3;
		break;
        }
    }
}

static void client_pthread_exit( int rt, Pipe * p, ForEpoll * ep ) {
    destroyForEpoll( ep );
    destroyPipe( p );
    pthread_exit( (void *)(&rt) );
}

/*
static void _set_fd_LT( Pipe * p, ForEpoll * ep ) {
    assert( p != NULL );
    assert( ep != NULL );

    if ( !( p->fd_flags & FD_IS_EPOLLLT ) ) {
        ep.ev.events = EPOLLIN | EPOLLLT;
        if ( p->fd_flags & FD_HAS_UNWRITE ) {
            ep.ev.events |= EPOLLOUT;
        }
    
        ep.ev.data.fd = p->fd;
        if ( epoll_ctl( ep.epoll_fd, EPOLL_CTL_MOD, p->fd, &(ep.ev) ) != 0 ) {
            dprintf(2, "Error[%s:%d]: epoll_ctl failed, %s\n", __FILE__, __LINE__, strerror(errno) );
            client_pthread_exit( -2, p, ep );
        }
    
        p->fd_flags |= FD_IS_EPOLLLT;
        p->fd_t = time( NULL );
    }
}

static void _set_fd_ET( Pipe * p, ForEpoll * ep ) {
    assert( p != NULL );
    assert( ep != NULL );

    if ( p->fd_flags & FD_IS_EPOLLLT ) {
        ep.ev.events = EPOLLIN | EPOLLET;
        if ( p->fd_flags & FD_HAS_UNWRITE ) {
            ep.ev.events |= EPOLLOUT;
        }
    
        ep.ev.data.fd = p->fd;
        if ( epoll_ctl( ep.epoll_fd, EPOLL_CTL_MOD, p->fd, &(ep.ev) ) != 0 ) {
            dprintf(2, "Error[%s:%d]: epoll_ctl failed, %s\n", __FILE__, __LINE__, strerror(errno) );
            client_pthread_exit( -2, p, ep );
        }
    
        p->fd_flags &= ( ~FD_IS_EPOLLIN );
    }
}
*/

static void _set_fd_out_listen( Pipe * p, ForEpoll * ep, BOOL x ) {
    int i;

    assert( p != NULL );
    assert( ep != NULL );

    // ! is_out && TRUE => set
    // is_out && FALSE  => unset
    if ( x ) { 
	if ( !( p->fd_flags & FD_IS_EPOLLOUT ) ) {
            ep->ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
            ep->ev.data.fd = p->fd;
            if ( epoll_ctl( ep->epoll_fd, EPOLL_CTL_MOD, ep->ev.data.fd, &(ep->ev) ) != 0 ) {
                dprintf(2, "Error[%s:%d]: epoll_ctl failed, %s\n", __FILE__, __LINE__, strerror(errno) );
                client_pthread_exit( -2, p, ep );
            }
    
            p->fd_flags |= FD_IS_EPOLLOUT;
	}
    }
    else {
	if ( p->fd_flags & FD_IS_EPOLLOUT ) {
            ep->ev.events = EPOLLIN | EPOLLET;
            ep->ev.data.fd = p->fd;
            if ( epoll_ctl( ep->epoll_fd, EPOLL_CTL_MOD, ep->ev.data.fd, &(ep->ev) ) != 0 ) {
                dprintf(2, "Error[%s:%d]: epoll_ctl failed, %s\n", __FILE__, __LINE__, strerror(errno) );
                client_pthread_exit( -2, p, ep );
            }
    
            p->fd_flags &= ( ~FD_IS_EPOLLOUT );
        }
    }
}

static void _set_tun_out_listen( Pipe * p, ForEpoll * ep, BOOL x, BOOL setall) {
    int i;

    assert( p != NULL );
    assert( ep != NULL );

    for ( i = 0; i < p->tun_list.sz; i++ ) {
	// ! is_out && TRUE => set
	// is_out && FALSE  => unset
	
	// x                                             : 设置EPOLLOUT
	// setall                                        : 所有fd都设置EPOLLOUT
	// (p->tun_list.tuns[i].flags & FD_WRITE_BLOCK ) : fd遭遇write block
	// !( p->tun_list.tuns[i].flags                  : 未监听EPOLLOUT
        if ( !( p->tun_list.tuns[i].flags & FD_IS_EPOLLOUT ) && x && 
			( setall || (p->tun_list.tuns[i].flags & FD_WRITE_BLOCK ) ) ) {
            ep->ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
            ep->ev.data.fd = p->tun_list.tuns[i].fd;
            if ( epoll_ctl( ep->epoll_fd, EPOLL_CTL_MOD, ep->ev.data.fd, &(ep->ev) ) != 0 ) {
                dprintf(2, "Error[%s:%d]: epoll_ctl failed, %s\n", __FILE__, __LINE__, strerror(errno) );
                client_pthread_exit( -2, p, ep );
            }

            p->tun_list.tuns[i].flags |= FD_IS_EPOLLOUT;
        }
	
	// !x                                              : 取消EPOLLOUT
	// ! (p->tun_list.tuns[i].flags & FD_WRITE_BLOCK ) : fd未遭遇write block
	// !( p->tun_list.tuns[i].flags & FD_IS_EPOLLOUT ) : 在监听EPOLLOUT
        if ( ( p->tun_list.tuns[i].flags & FD_IS_EPOLLOUT ) && !x && 
			( ! (p->tun_list.tuns[i].flags & FD_WRITE_BLOCK ) ) ) {
            ep->ev.events = EPOLLIN | EPOLLET;
            ep->ev.data.fd = p->tun_list.tuns[i].fd;
            if ( epoll_ctl( ep->epoll_fd, EPOLL_CTL_MOD, ep->ev.data.fd, &(ep->ev) ) != 0 ) {
                dprintf(2, "Error[%s:%d]: epoll_ctl failed, %s\n", __FILE__, __LINE__, strerror(errno) );
                client_pthread_exit( -2, p, ep );
            }

            p->tun_list.tuns[i].flags &= ( ~FD_IS_EPOLLOUT );
        }
    }
}

static void _relay_fd_to_tun( Pipe * p, int evt_fd, ForEpoll * ep, char rw ) {
    char   wblock = 'n';
    char   rblock = 'n';
    int    rt;

    while ( 1 ) {
	printf("debug[%s:%d]: _relay_fd_to_tun read client fd\n", __FILE__, __LINE__ );
        //==== 从client fd读
	// 前提：
	//   fd not closed
	//
	// 结果：
	//   (1) buffer is full (socket non-block)
	//   (2) socket block (buffer is not full)
        rt = stream( P_STREAM_FD2BUFF, p, evt_fd );
        switch ( rt ) {
	    case 2: // socket closed
                if ( ! isBuffEmpty( &(p->tun2fd) ) ) {
                    dprintf(2, "Error[%s:%d]: merge fd %d closed, but tun2fd has data", __FILE__, __LINE__, p->fd);
                    client_pthread_exit( -2, p, ep );
	        }
            case 0: // socket block
                rblock = 'y';
                break;

	    case -1:// errors
                dprintf(2, "Error[%s:%d]: merge fd %d, errno=%d %s\n",
                            __FILE__, __LINE__,
                            evt_fd,
                            errno, strerror(errno));
                client_pthread_exit( -2, p, ep );
        }
    
	printf("debug[%s:%d]: _relay_fd_to_tun write tunnel fds\n", __FILE__, __LINE__ );
        //==== 往tunnels写
	// 前提：
	//   有数据可写：
	//     (1) fd2tun has active data
	//     (2) 需要给对方回复ack
	//
	// 结果：
        // (1) wblock == 'y' && buffer is full
        // (2) wblock == 'y' && buffer is not full
        // (3) wblock == 'n' && buffer is empty
        // 对于(2)和(3)，就要去尝试下再去读，消耗读事件。
        // 但如果读事件已经碰到rblock了，就不用再循环下去了。
        rt = stream( P_STREAM_BUFF2TUN, p, evt_fd );
        switch ( rt ) {
            case -1: // 
                dprintf(2, "Error[%s:%d]: client socket fd %d, errno=%d %s\n",
                            __FILE__, __LINE__,
                            evt_fd,
                            errno, strerror(errno));
                client_pthread_exit( -2, p, ep );
                break;

            case 40:
                wblock = 'y'; //写不动了，有可能socket block，有可能到了未ack发包极限
                break;

            //case 41: 
	    //
        }

        /* 处理 fd closed 的情况。
         *
         * (1) fd 端已完成数据收发，确认要关闭了。
         * (2) fd 端因为非正常关闭。
         * 
         * 但作为一个中专，我们不管这些，我们只是关闭。
         *
         */
        if ( p->fd_flags & FD_CLOSED ) { 
	    if ( ( ! hasDataToTun( p ) ) && ( p->tun_list.sending_count == 0 ) 
		    && ( ! hasUnAckData( &(p->fd2tun) ) ) ) {
                printf("debug[%s:%d]: ENDING1 - will send FIN\n", __FILE__, __LINE__ );
                p->stat = P_STAT_ENDING1;
	    }
	    else {
                printf("debug[%s:%d]: ENDING - FIN waiting\n", __FILE__, __LINE__ );
	        p->stat = P_STAT_ENDING;
	    }
        }
	
        if ( rw == 'r' ) { // client fd met read event
            /* 尽力把client fd读到block。
	     * 这里write是为了配合read的，因为有了read，所以才要write。
	     * 所以，只要还能read，就继续循环，即：
	     *   rblock == 'n' && ! isBuffFull( &(p->fd2tun) 
	     */
	    
	    if ( rblock == 'n' && ( ! isBuffFull( &(p->fd2tun) ) ) ) {
	        continue;
	    }
	    break;
	}
	else { // rw == 'w': tunnel fds met write event
	    if ( wblock == 'n' && hasDataToTun( p ) ) {
	        continue;
	    }
	    break;
	}
    }
    
    if ( p->stat == P_STAT_ENDING1 ) {
        printf("debug[%s:%d]: ENDING1 - all tunnel fds EPOLLOUT\n", __FILE__, __LINE__ );
	_set_tun_out_listen( p, ep, TRUE, TRUE );
    }
    else if ( hasDataToTun( p ) ) {
        printf("debug[%s:%d]: set EPOLLOUT for all tunnel fds\n", __FILE__, __LINE__ );
	_set_tun_out_listen( p, ep, TRUE, TRUE );
    }
    else if ( p->tun_list.sending_count > 0 ) {
        // 尝试为哪些碰到write block的fd设置EPOLLOUT
        printf("debug[%s:%d]: set EPOLLOUT for tunnel fds who met write-block\n", __FILE__, __LINE__ );
        _set_tun_out_listen( p, ep, TRUE, FALSE );
    }
    else {
        printf("debug[%s:%d]: unset EPOLLOUT for tunnel fds\n", __FILE__, __LINE__ );
	_set_tun_out_listen( p, ep, FALSE, FALSE );
    }
}

static void _relay_tun_to_fd( Pipe * p, int evt_fd, ForEpoll * ep, char rw ) {
    char wblock = 'n';
    char rblock = 'n';
    char bblock = 'n';
    int  rt;

    assert( p != NULL );
    assert( ep != NULL );

    while ( 1 ) {
        //==== 从tunnels读
	printf("debug[%s:%d]: _relay_tun_to_fd > read tunnel fd\n", __FILE__, __LINE__ );
        rt = stream( P_STREAM_TUN2BUFF, p, evt_fd );
        switch ( rt ) {
            case -1: // errors
            case -66:
                dprintf(2, "Error[%s:%d]: tunnel socket fd %d, errno=%d %s\n", 
                		__FILE__, __LINE__, 
                		evt_fd,
                		errno, strerror(errno));
                client_pthread_exit( -2, p, ep );
                break;

            case 32: 
		// Tunnel对端发送了FIN，证明：
		//   (1) 对端的fd已关闭
		//   (2) 对端已向我方发送完了数据并收到了我方的ack
		//
		// 这时，我方仍需要将tun2fd的数据发送给我方的fd。
		if ( hasDataToTun( p ) || p->tun_list.sending_count > 0 ) {
                    dprintf(2, "Error[%s:%d]: 还有数据未发送完毕，Tunnel对端结束\n", __FILE__, __LINE__ );
		}
		if ( p->prev_seglist.len > 0 ) {
                    dprintf(2, "Error[%s:%d]: Tunnel对端结束，prev_seglist里还有数据\n", __FILE__, __LINE__ );
		}
		p->tun_closed = 'y';
	    case 30: // socket block
		rblock = 'y';
                break;

            case 31: // buffer空间可能不足，不读了
                bblock = 'y';
	        break;
        }

        //==== 向client fd写
	printf("debug[%s:%d]: _relay_tun_to_fd > write merge fd\n", __FILE__, __LINE__ );
        if ( ! isBuffEmpty( &(p->tun2fd) ) ) {
            rt = stream( P_STREAM_BUFF2FD, p, p->fd );
            switch ( rt ) {
                case -1: // errors
                case -66:
                    dprintf(2, "Error[%s:%d]: merge fd %d, errno=%d %s\n", 
                    		__FILE__, __LINE__, 
                    		p->fd,
                    		errno, strerror(errno));
                    client_pthread_exit( -2, p, ep );
                    break;

                case 22: // 消耗了tun2fd的bytes
		    bblock = 'n';
		    break;

                case 21: // 消耗了tun2fd的bytes
                    bblock = 'n';
		    wblock = 'y';
		    break;

                case 20: // 未消耗tun2fd的bytes
                    wblock = 'y';
	    	    break;
            }
	}

	if ( p->tun_closed == 'y' && isBuffEmpty( &(p->tun2fd) ) ) {
	    p->stat = P_STAT_END;
	}

        if ( rw == 'r' ) { // a tunnel fd met read event
            /* 尽力把tunnel fd读到block,
	     * 所以只要还能读，就继续。
	     * 能读的条件：
	     *   rblock == 'n' &&
	     *   ! isBuffFull( p->fd2tun ) &&
	     *   bblock == 'n'
	     */
	    if ( rblock == 'n' && ( ! isBuffFull( &(p->tun2fd) ) ) && bblock == 'n' ) {
	        continue;
	    }
	    break;
	}
	else { // rw == 'w': client fd met write event
            if ( wblock == 'n' && ( ! isBuffEmpty( &(p->tun2fd) ) ) ) {
	        continue;
	    }
	    break;
	}
    }
    
    if ( ! isBuffEmpty( &(p->tun2fd) ) ) { 
        printf("warning[%s:%d]: client fd %d met wblock, tun2fd is not empty\n", __FILE__, __LINE__, p->fd );
	
	// 如果 tun2fd 非空，
	// 监听 EPOLLOUT 事件。
	_set_fd_out_listen( p, ep, TRUE );
    }
    else {
	_set_fd_out_listen( p, ep, FALSE );
    }
}

void * client_pthread( void * p ) {
    ForEpoll ep;
    int      rt;
    //char     rw; // 'r' 是读事件；'w'是写事件；'a'读写都有
    int      i;  // loop variables
    int      auth_ok = 0;
    int      ecode = 0;
    int      timeout = 0;
    Pipe     pp;

    assert( p != NULL );

    pp = *( (Pipe *)p );

    initForEpoll( &ep );

    // 设置 non-blocking
    if ( setnonblocking( pp.fd ) ) {
        dprintf(2, "Error[%s:%d]: set nonblocking failed, %s\n", __FILE__, __LINE__, strerror(errno) );
        client_pthread_exit( -5, &pp, &ep );
    }

    // 创建epoll
    ep.epoll_fd = epoll_create( TUN_LIST_SZ + 1 );
    if ( ep.epoll_fd == -1 ) {
        dprintf(2, "Error[%s:%d]: epoll_create failed, %s\n", __FILE__, __LINE__, strerror(errno) );
        client_pthread_exit( -4, &pp, &ep );
    }

    // 将 client fd 加入epoll
    ep.ev.events = EPOLLIN | EPOLLET;
    ep.ev.data.fd = pp.fd;
    if ( epoll_ctl( ep.epoll_fd, EPOLL_CTL_ADD, pp.fd, &(ep.ev) ) < 0 ) {
        dprintf(2, "Error[%s:%d]: epoll_ctl failed, %s\n", __FILE__, __LINE__, strerror(errno) );
        client_pthread_exit( -2, &pp, &ep );
    }
    ep.fd_count = 1;

    // 将 tunnels 的 fd 加入epoll
    for ( i = 0; i < pp.tun_list.len; i++ ) {
        ep.ev.events = EPOLLIN | EPOLLET;
        ep.ev.data.fd = pp.tun_list.tuns[i].fd;
        if ( epoll_ctl( ep.epoll_fd, EPOLL_CTL_ADD, pp.tun_list.tuns[i].fd, &(ep.ev) ) < 0 ) {
	    // 如果某个 tunnel socket fd 操作失败，
	    // close 之前所有的 tunnels socket fd 和 epoll fd。
            dprintf(2, "Error[%s:%d]: epoll_ctl failed, %s\n", __FILE__, __LINE__, strerror(errno) );
            client_pthread_exit( -2, &pp, &ep );
        }
        ++ep.fd_count;
    }

    // main loop
    while ( 1 ) {
        //== 阻塞在这里等待epoll事件
        printf("debug[%s:%d]: epoll_wait...\n", __FILE__, __LINE__ );
        if ( ( ep.wait_fds = epoll_wait( ep.epoll_fd, ep.evs, ep.fd_count, -1 ) ) == -1 ) {
            dprintf( 2, "Epoll Wait Error: %s\n", strerror( errno ) );
            client_pthread_exit( -3, &pp, &ep );
        }
        printf("debug[%s:%d]: epoll_wait events: count=%d\n", __FILE__, __LINE__, ep.wait_fds );

        //== 认证到服务器
	if ( ! auth_ok ) {
            printf("debug[%s:%d]: auth to server\n", __FILE__, __LINE__ );
	    ecode = 0;
            rt = authToServer( &pp, &ecode );
            if ( rt == 2 ) { // auth ok
	        auth_ok = 1;
                printf("Info[%s:%d]: auth to server success\n", __FILE__, __LINE__ );
	    }
	    else if ( rt == 0 ) { // socket block
                //printf("Debug[%s:%d]: auth socket block\n", __FILE__, __LINE__);
		;
            } else {
	        printf("Info[%s:%d]: auth to server failed\n", __FILE__, __LINE__ );
                client_pthread_exit( -4, &pp, &ep );
	    }
	}

        //== 碰到epoll事件就处理事件
        printf("debug[%s:%d]: transfer data\n", __FILE__, __LINE__ );
        for ( i = 0; i < ep.wait_fds; i++ ) {
            printf("debug[%s:%d]: loop[%d] fd=%d\n", __FILE__, __LINE__, i, ep.evs[i].data.fd );
            // 传递数据
            if ( auth_ok ) {
                if ( ep.evs[i].data.fd == pp.fd ) {
		    /*
		     * 如果 client fd 有了 read事件，就 fd2tun。
		     * 如果 client fd 有了write事件，就 tun2fd。
		     * 这俩处理不分先后。
		     *
		     */
		    if ( ep.evs[i].events & EPOLLIN ) {
                        printf("debug[%s:%d]: merge fd r event\n", __FILE__, __LINE__ );
	                _relay_fd_to_tun( &pp, ep.evs[i].data.fd, &ep, 'r' );
		    }
		    if ( ep.evs[i].events & EPOLLOUT ) {
                        printf("debug[%s:%d]: merge fd w event\n", __FILE__, __LINE__ );
	                _relay_tun_to_fd( &pp, ep.evs[i].data.fd, &ep, 'w' );
		    }
	        }
	        else {
		    if ( ep.evs[i].events & EPOLLIN ) {
                        printf("debug[%s:%d]: tunnel fd r event\n", __FILE__, __LINE__ );
	                _relay_tun_to_fd( &pp, ep.evs[i].data.fd, &ep, 'r' );
		    }
		    if ( ep.evs[i].events & EPOLLOUT ) {
                        printf("debug[%s:%d]: tunnel fd w event\n", __FILE__, __LINE__ );
	                _relay_fd_to_tun( &pp, ep.evs[i].data.fd, &ep, 'w' );
		    }
	        }
	    }
	    else {
                if ( ep.evs[i].data.fd == pp.fd ) {
		    if ( ep.evs[i].events & EPOLLIN ) {
                        printf("debug[%s:%d]: merge fd %d: r event\n", __FILE__, __LINE__, ep.evs[i].data.fd );
                        rt = stream( P_STREAM_FD2BUFF, &pp, ep.evs[i].data.fd );
                        printf("debug[%s:%d]: not auth ok, read data\n", __FILE__, __LINE__);
		        dumpBuff( &(pp.fd2tun) );
                        switch ( rt ) {
	                    case 2: // socket closed
                                if ( ! isBuffEmpty( &(pp.tun2fd) ) ) {
                                    dprintf(2, "Error[%s:%d]: client fd %d closed, but tun2fd has data", __FILE__, __LINE__, pp.fd);
                                    client_pthread_exit( -2, &pp, &ep );
	                        }
                            case 0: // socket block
                                break;

	                    case -1:// errors
                                dprintf(2, "Error[%s:%d]: mapping fd %d, errno=%d %s\n",
                                            __FILE__, __LINE__,
                                            ep.evs[i].data.fd,
                                            errno, strerror(errno));
                                client_pthread_exit( -2, &pp, &ep );
                        }

			if ( ! isBuffEmpty( &(pp.fd2tun) ) ) {
			    _set_tun_out_listen( &pp, &ep, TRUE, TRUE );
			}
		    }
		}
	    }
	}

	//== 看 pipe 是否要终结
	if ( pp.stat == P_STAT_END ) {
            printf("Info[%s:%d]: pipe[%8s] end\n", __FILE__, __LINE__, pp.key );
	    break;
	}
    }

    client_pthread_exit( 0, &pp, &ep );
}


/*
 *
 * == return ==
 *  3: auth successfully
 *  2: service failed, ecode will be set
 *  0: socket block
 *     nread == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)
 *     或者
 *     nwrite == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)
 * -1: all error, see errno
 * 
 */
int authCli( FdNode * fn, PipeList * pl, int * ecode, struct sockaddr_in mapping_addr, FdList * fd_list ) {
    int          rt;
    size_t       want_sz;
    int          idx;
    AuthPacket * ap;
    Pipe       * p;
    Pipe       * new_p;
    FdNode       fdnode;

    assert( fn != NULL );
    assert( pl != NULL );

    while ( 1 ) {
        switch ( fn->auth_status ) {
            case TUN_ACTIVE: // read auth packet
                printf("debug: read auth packet => go\n");
                while ( ! isBuffFull( &(fn->bf) ) ) {
                    want_sz = 0;
		    rt = putBytesFromFd( &(fn->bf), fn->fd, &want_sz );
                    if ( isBuffFull( &(fn->bf) ) ) {
                        // buffer is full
            	        fn->auth_status = TUN_RECV_AP;
                        printf("debug: read auth packet => ok\n");
                        break;
                    }

                    switch ( rt ) {
                        case -1:
                            dprintf(2, "Error[%s:%d]: nread=-1, errno=%d, %s\n", __FILE__, __LINE__, errno, strerror(errno));
                            return -1;
                        case -2:
                            printf("Warning[%s:%d]: nread=0, peer closed, %s\n", __FILE__, __LINE__, strerror(errno));
                    	    return -1;
                    }

                    return rt; // 0
                }
                break;
            
            case TUN_RECV_AP: //==== auth
                printf("debug: auth => go\n");
                *ecode = 0;
                ap = (AuthPacket *)(fn->bf.buff);
                switch ( ap->code ) {
                    case AUTH_NEW:
                        p = searchPipeByKey( pl, ap->key );
                        if ( p ) {
                            ap->code = (*ecode) = AUTH_KEY_USED;
                        }
                        else { 
			    idx = getAEmptyPipe( pl );
			    if ( idx == -1 ) {
                                ap->code = (*ecode) = AUTH_USR_FULL;
			        return 2;
			    }
			    else {
			        return -1;
			    }
			    new_p = pl->pipes + idx;

			    // 连接mapping addr
                            if ( ( rt = connectFd( new_p, mapping_addr ) ) != 0 ) {
				delPipeByI( pl, idx );
				new_p = NULL;
				idx = -1;
                                ap->code = (*ecode) = AUTH_SERV_ERR;
                                dprintf(2, "Error[%s:%d]: connect mapping addr failed, errno=%d, %s\n", __FILE__, __LINE__, errno, strerror(errno));
			        return 2;
			    }
                            else { 
                                printf("debug[%s:%d]: mapping fd = %d\n", __FILE__, __LINE__, new_p->fd);
				fdnode.use = 1;
                                fdnode.fd = new_p->fd;
                                fdnode.type = FD_MERGE1;
                                fdnode.t = time( NULL );
                                fdnode.p = new_p;
                                if ( addFd( fd_list, &fdnode ) ) {
				    delPipeByI( pl, idx );
                                    new_p = NULL;
				    idx = -1;
                                    ap->code = (*ecode) = AUTH_USR_FULL;
                                    return 2;
                                }

                                // 设置pipe数据
                                memcpy( (void *)(new_p->key), (void *)(ap->key), P_KEY_SZ );
                                new_p->stat = P_STAT_ACTIVE;
				joinTunList( &(new_p->tun_list), fn->fd );
				printf("new_p=%p fn->p=%p\n", new_p, fn->p);
				
				new_p = NULL;
				idx = -1;
                            }
                        }
                        break;

                    case AUTH_JOIN:
                        printf("debug[%s:%d]: join pipe, key=%36s\n", __FILE__, __LINE__, ap->key);
                        p = searchPipeByKey( pl, ap->key );
                        if ( ! p ) {
                            ap->code = (*ecode) = AUTH_NO_KEY;
                        }
                        else {
			    //printf("p=%p\n", p);
			    fn->p = p;
                    	    if ( joinTunList( &(p->tun_list), fn->fd ) ) {
                                ap->code = (*ecode) = AUTH_TUN_FULL;
                    	    } else {
                                printf("debug[%s:%d]: join pipe successfully, p=%p\n", __FILE__, __LINE__, p);
			    }
                        }
                        break;

                    default:
                        ap->code = (*ecode) = AUTH_BAD_CODE;
                        dprintf(2, "Error[%s:%d]: bad auth code %u\n", __FILE__, __LINE__, ap->code);
                }

                if ( *ecode == 0 ) {
                    ap->code = AUTH_OK;
                }
		fn->auth_status = TUN_REPLIED;
                break;

            case TUN_REPLIED: // reply to client
                printf("debug: reply to client => go\n");
                ap = (AuthPacket *)(fn->bf.buff);
                while ( ! isBuffEmpty( &(fn->bf) ) ) {
                    rt = getBytesToFd( &(fn->bf), fn->fd );
                    if ( rt == 1 && isBuffEmpty( &(fn->bf) ) ) {
                        // sent successfully
                        printf("debug: reply to client => ok\n");
                        break;
                    }

                    if ( rt == -1 ) {
                        dprintf(2, "error[%s:%d]: reply to client failed\n", __FILE__, __LINE__);
                    }
                    printf("debug: reply to client => error\n");

                    return rt; // 0, -1
                }

                if ( ap->code == AUTH_OK  ) {
                    fn->auth_ok = 1;
		    fn->auth_status = TUN_AUTHED;
                    return 3;
                }
                return 2;
                break;
        }
    }
}

int doesNotAuthed( FdNode * fn ) {
    assert( fn != NULL );
    return ! ( fn->auth_ok );
}
