#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <sys/resource.h>        /* 设置最大的连接数需要setrlimit */
#include <pthread.h>
#include <netinet/in.h>            /* socket类定义需要*/
#include <net/if.h>
#include <linux/sockios.h>
#include <fcntl.h>                    /* nonblocking需要 */
#include <arpa/inet.h>

#include "pipe.h"
#include "common.h"
#include "server.h"
#include "service.h"

PipeList plist;
FdList fd_list;

struct {
    int    local_port; // 本地监听端口
    char * mapping_addr;
    int    mapping_port;
} serv_conf = {
    10082,
    "127.0.0.1",
    10087
};

/*
 *
 * == return ==
 *  0: ok
 * -1: failed
 */
int _insert_fdlist( int conn_fd, int typ ) {
    FdNode fn;

    fn.use = 1;
    fn.fd = conn_fd;
    fn.type = typ;
    fn.t = time( NULL );
    fn.auth_status = TUN_ACTIVE;
    fn.p = NULL;
    if ( initBuff( &(fn.bf), sizeof(AuthPacket), BUFF_MD_2FD ) ) {
        return -1;
    }
    if ( addFd( &fd_list, &fn ) ) {
	destroyBuff( &(fn.bf) );
        close(conn_fd);
	dprintf(2, "Error[%s:%d]: fd list is full\n", __FILE__, __LINE__ );
        return -1;
    }

    return 0;
}

int _accept_a_client( int listen_fd, ForEpoll * ep ) {
    int                conn_fd;
    struct sockaddr_in cliaddr;
    socklen_t          len = sizeof( struct sockaddr_in );

    // accept 客户端连接
    if ( ( conn_fd = accept( listen_fd, (struct sockaddr *)&cliaddr, &len ) ) == -1 ) {
        dprintf( 2, "Accept Error: %s\n", strerror( errno ) );
        return -1;
    }
    printf( "Info: fd=%d, client %s:%d \n", conn_fd, inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port );
    
    // fd 设置 non-blocking
    if ( setnonblocking( conn_fd ) ) {
        close(conn_fd);
        dprintf(2, "error[%s:%d]: set nonblocking failed, fd = %d\n", __FILE__, __LINE__, conn_fd );
        return -1;
    }

    // fd 加入 fd_list
    if ( _insert_fdlist( conn_fd, FD_TUN ) ) {
        return -1;
    }

    // 将 fd 加入 epoll 监听
    ep->ev.events = EPOLLIN | EPOLLET;
    ep->ev.data.fd = conn_fd;
    if ( epoll_ctl( ep->epoll_fd, EPOLL_CTL_ADD, conn_fd, &(ep->ev) ) < 0 ) {
        dprintf( 2, "Epoll Error: %s\n", strerror ( errno ) );
        delFd( &fd_list, conn_fd );
	return -1;
    }
    (ep->fd_count)++;

    return 0;
}


static int _set_fd_out_listen( Pipe * p, ForEpoll * ep, BOOL x ) {
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
		return -1;
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
		return -1;
            }
    
            p->fd_flags &= ( ~FD_IS_EPOLLOUT );
        }
    }

    return 0;
}

static int _set_tun_out_listen( Pipe * p, ForEpoll * ep, BOOL x, BOOL setall) {
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
		return -1;
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
		return -1;
            }

            p->tun_list.tuns[i].flags &= ( ~FD_IS_EPOLLOUT );
        }
    }

    return 0;
}

/*
 * == return ==
 *  0: ok
 * -1: errors
 */
static int _relay_fd_to_tun( Pipe * p, int evt_fd, ForEpoll * ep, char rw ) {
    char   wblock = 'n';
    char   rblock = 'n';
    int    rt;

    while ( 1 ) {
	printf("debug[%s:%d]: _relay_fd_to_tun read merge fd", __FILE__, __LINE__ );
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
                    return -1;
	        }
            case 0: // socket block
                rblock = 'y';
                break;

	    case -1:// errors
                dprintf(2, "Error[%s:%d]: merge fd %d, errno=%d %s\n",
                            __FILE__, __LINE__,
                            evt_fd,
                            errno, strerror(errno));
                return -1;
        }
    
	printf("debug[%s:%d]: _relay_fd_to_tun write tunnel fds", __FILE__, __LINE__ );
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
                dprintf(2, "Error[%s:%d]: merge socket fd %d, errno=%d %s\n",
                            __FILE__, __LINE__,
                            evt_fd,
                            errno, strerror(errno));
                return -1;
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
                p->stat = P_STAT_ENDING1;
	    }
	    else {
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
	// 这时表示 merge fd 已 closed，
	// 需要向 Tunnel 对端发送 FIN packet。
        printf("warning[%s:%d]: buffer中存有数据待发\n", __FILE__, __LINE__ );
	if ( _set_tun_out_listen( p, ep, TRUE, TRUE ) ) {
	    return -1;
	}
    }
    else if ( hasDataToTun( p ) ) {
        printf("warning[%s:%d]: buffer中存有数据待发\n", __FILE__, __LINE__ );
	if ( _set_tun_out_listen( p, ep, TRUE, TRUE ) ) {
	    return -1;
	}
    }
    else if ( p->tun_list.sending_count > 0 ) {
        // 尝试为哪些碰到write block的fd设置EPOLLOUT
	if ( _set_tun_out_listen( p, ep, TRUE, FALSE ) ) {
	    return -1;
	}
    }
    else {
	if ( _set_tun_out_listen( p, ep, TRUE, FALSE ) ) {
	    return -1;
	}
    }
}

static int _relay_tun_to_fd( Pipe * p, int evt_fd, ForEpoll * ep, char rw ) {
    char wblock = 'n';
    char rblock = 'n';
    char bblock = 'n';
    int  rt;

    assert( p != NULL );
    assert( ep != NULL );

    while ( 1 ) {
        //==== 从tunnels读
        rt = stream( P_STREAM_TUN2BUFF, p, evt_fd );
        switch ( rt ) {
            case -1: // errors
            case -66:
                dprintf(2, "Error[%s:%d]: tunnel socket fd %d, errno=%d %s\n", 
                		__FILE__, __LINE__, 
                		evt_fd,
                		errno, strerror(errno));
                return -1;

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
        if ( ! isBuffEmpty( &(p->tun2fd) ) ) {
            rt = stream( P_STREAM_BUFF2FD, p, p->fd );
            switch ( rt ) {
                case -1: // errors
                case -66:
                    dprintf(2, "Error[%s:%d]: merge socket fd %d, errno=%d %s\n", 
                    		__FILE__, __LINE__, 
                    		p->fd,
                    		errno, strerror(errno));
                    return -1;

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
	if ( _set_tun_out_listen( p, ep, TRUE, TRUE ) ) {
	    return -1;
	}
    }
    else {
	if ( _set_tun_out_listen( p, ep, TRUE, FALSE ) ) {
	    return -1;
	}
    }
}

int _del_fd( ForEpoll * ep, int fd ) {
    assert( ep != NULL );
    assert( fd >= 0 );

    if ( ( epoll_ctl( ep->epoll_fd, EPOLL_CTL_DEL, fd, &(ep->ev) ) ) < 0 ) {
        dprintf(2, "Error: epoll_ctl_del error, %s\n", strerror(errno));
        return -1;
    }
    --(ep->fd_count);
    
    return 0;
}

int _destroy_pipe( Pipe * p, ForEpoll * ep ) {
    int j;
    assert( p != NULL );

    delFd( &fd_list, p->fd );
    if ( _del_fd( ep, p->fd ) ) {
        return -1;
    }
    for ( j = 0; j < p->tun_list.len; j++ ) {
        if ( p->tun_list.tuns[j].fd != -1 ) {
            delFd( &fd_list, p->tun_list.tuns[j].fd );
            if ( _del_fd( ep, p->tun_list.tuns[j].fd ) ) {
	        return -1;
	    }
        }
    }
    
    destroyPipe( p );

    return 0;
}

void main_loop( ForEpoll ep, int listen_fd, struct sockaddr_in mapping_addr ) {
    int                i;
    int                rt;
    int                fd;
    int                ecode;
    FdNode           * fd_node;
    FdNode           * fn2;
    int                timeout = -1;
    unsigned int       flags = 0;

    while ( 1 ) {
	//==== 1) 阻塞在这里等待epoll事件
	printf("debug: epoll_wait...\n");
        if ( ( ep.wait_fds = epoll_wait( ep.epoll_fd, ep.evs, ep.fd_count, timeout ) ) == -1 ) {
            dprintf( 2, "Epoll Wait Error: %s\n", strerror( errno ) );
            exit( EXIT_FAILURE );
        }
 
	//==== 2) 碰到epoll事件就处理事件
	printf("debug: epoll events\n");
        for ( i = 0; i < ep.wait_fds; i++ ) {
	    // if listen_fd has events, accept
            if ( ep.evs[i].data.fd == listen_fd && ep.fd_count < MAXEPOLL ) {
                if ( _accept_a_client( listen_fd, &ep ) ) {
                    dprintf( 2, "accept Error: %s\n", strerror( errno ) );
                    exit( EXIT_FAILURE );
		}
		continue;
	    }

	    printf("debug[%s:%d]: event fd = %d\n", __FILE__, __LINE__, ep.evs[i].data.fd );
            
            fd_node = searchByFd( &fd_list, ep.evs[i].data.fd );
            if ( ! fd_node ) {
                dprintf(2, "Error: info of fd %d not found\n", ep.evs[i].data.fd );
		continue;
	    }

	    // authytication
            if ( fd_node->type == FD_TUN && doesNotAuthed( fd_node ) ) {
	        printf("debug[%s:%d]: auth => go\n", __FILE__, __LINE__);
                rt = authCli( fd_node, &plist, &ecode, mapping_addr, &fd_list );
		switch ( rt ) {
		    case  2:
		       dprintf(2, "Error: auth failed, errcode=%d\n", ecode);
		    case -3: // failed to allocate memory
		    case -2: // (read) peer closed fd
		    case -1: // read/write socket error
		       dprintf(2, "Error: auth error: %s\n", strerror(errno));
		       fd = fd_node->fd;
		       if ( _del_fd( &ep, fd ) ) {
		           dprintf(2, "Error: epoll_ctl_del error, %s\n", strerror(errno));
		       }
		       delFd( &fd_list, fd_node->fd );
		       printf("debug: fd %d has been deleted\n", fd);
		       break;
		    
		    case 0: // socket block
	               printf("debug[%s:%d]: auth => socket block\n", __FILE__, __LINE__);
		       break;

		    case 3: // auth successfully
	                printf("debug[%s:%d]: auth => ok\n", __FILE__, __LINE__);
                        fn2 = searchByFd( &fd_list, fd_node->p->fd );
                        if ( fn2 && fn2->type == FD_MERGE1 ) {
		            // 将 fd 加入 epoll 监听
                            ep.ev.events = EPOLLIN | EPOLLET;
                            ep.ev.data.fd = fn2->p->fd;
                            if ( epoll_ctl( ep.epoll_fd, EPOLL_CTL_ADD, fn2->p->fd, &(ep.ev) ) < 0 ) {
                                dprintf( 2, "Epoll Error: %s\n", strerror ( errno ) );
                                exit( EXIT_FAILURE );
                            }
                            ++ep.fd_count;
			    fn2->type = FD_MERGE;
	                    printf("debug[%s:%d]: add mapping fd %d to epoll\n", __FILE__, __LINE__, fn2->p->fd);
	                } else {
	                    printf("debug[%s:%d]: mapping fd %d already in epoll\n", __FILE__, __LINE__, fn2->p->fd);
			}
		       break;

		    default:
		        dprintf(2, "Error[%s:%d]: unprobablely error rutern: %d, please check codes\n", __FILE__, __LINE__, rt);
		}
		continue;
	    }

            // transffer data from one edge to the other eage of pipe
            if ( fd_node->type == FD_MERGE ) {
		if ( ep.evs[i].events & EPOLLIN ) {
	            printf("debug[%s:%d]: merge fd IN\n", __FILE__, __LINE__);
		    rt = _relay_fd_to_tun( fd_node->p, ep.evs[i].data.fd, &ep, 'r' );
		}
		if ( ep.evs[i].events & EPOLLOUT ) {
	            printf("debug[%s:%d]: merge fd OUT\n", __FILE__, __LINE__);
		    rt = _relay_tun_to_fd( fd_node->p, ep.evs[i].data.fd, &ep, 'w' );
		}
	    }
	    else if ( fd_node->type == FD_TUN ) {
		if ( ep.evs[i].events & EPOLLIN ) {
	            printf("debug[%s:%d]: tunnel fd IN\n", __FILE__, __LINE__);
		    rt = _relay_tun_to_fd( fd_node->p, ep.evs[i].data.fd, &ep, 'r' );
		}
		if ( ep.evs[i].events & EPOLLOUT ) {
	            printf("debug[%s:%d]: tunnel fd OUT\n", __FILE__, __LINE__);
		    rt = _relay_fd_to_tun( fd_node->p, ep.evs[i].data.fd, &ep, 'w' );
		}
	    }

	    if ( rt == -1 && _destroy_pipe( fd_node->p, &ep ) ) {
		// destroy fd_nodes
		// destroy epoll
	        // destroy pipe 
		break;
	    }
        }
    }
}


int main(int argc, char *argv[]) {
    int                listen_fd;
    struct sockaddr_in mapping_addr;
    ForEpoll           ep;

    // init
    initPipeList( &plist );
    initFdList( &fd_list );

    memset(&mapping_addr, 0, sizeof(mapping_addr));                      //每个字节都用0填充
    mapping_addr.sin_family = AF_INET;                                   //使用IPv4地址
    mapping_addr.sin_addr.s_addr = inet_addr(serv_conf.mapping_addr); //具体的IP地址
    mapping_addr.sin_port = htons(serv_conf.mapping_port);            //端口

    // 1. listen
    listen_fd = local_listen( serv_conf.local_port, 1 );

    // 2. epoll
    if ( epoll_init( listen_fd, &ep ) ) {
        printf("debug[%s:%d]: add listen_fd to epoll failed\n", __FILE__, __LINE__);
    }
    else {
        // 3. main loop
        main_loop( ep, listen_fd, mapping_addr );
    }

    // destroy
    close( listen_fd );
    close( ep.epoll_fd );
    destroyFdList( &fd_list );
    destroyPipeList( &plist );

    return 0;
}
