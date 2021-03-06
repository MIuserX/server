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
    //10087
    5432
};


/* 处理client accetp.
 *
 * == desc ==
 * 从listen_fd accept 一个 conn_fd，
 * 并为conn_fd建立：
 *   (1) FdNode
 *   (2) EPOLLIN
 *
 * == return ==
 *  0: ok
 * -1: errors
 * -2: accept error
 */
int _accept_a_client( int listen_fd, ForEpoll * ep ) {
    int                idx;
    int                conn_fd;
    struct sockaddr_in cliaddr;
    socklen_t          len = sizeof( struct sockaddr_in );

    // accept 客户端连接
    if ( ( conn_fd = accept( listen_fd, (struct sockaddr *)&cliaddr, &len ) ) == -1 ) {
        dprintf(2, "Error[%s:%d]: accept error - %s\n", 
			__FILE__, __LINE__, strerror(errno) );
        return -2;
    }
    printf( "Info[%s:%d]: fd=%d, client %s:%d \n", __FILE__, __LINE__, 
		    conn_fd, inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port );
    
    // fd 设置 non-blocking
    if ( setnonblocking( conn_fd ) ) {
        close(conn_fd);
        dprintf(2, "error[%s:%d]: set nonblocking failed, fd = %d\n", __FILE__, __LINE__, conn_fd );
        return -1;
    }

    // fd 加入 fd_list
    idx = addTunFd( &fd_list, conn_fd );
    switch ( idx ) {
        case -1:
            printf("Warning[%s:%d]: fd lis is full\n", __FILE__, __LINE__ );
	    return -1;
	case -2:
            printf("Warning[%s:%d]: cannot get memory\n", __FILE__, __LINE__ );
	    return -1;
	case -66:
	    return -66;
    }

    // 将 fd 加入 epoll 监听
    ep->ev.events = EPOLLIN | EPOLLET;
    ep->ev.data.fd = conn_fd;
    if ( epoll_ctl( ep->epoll_fd, EPOLL_CTL_ADD, conn_fd, &(ep->ev) ) < 0 ) {
        dprintf(2, "error[%s:%d]: add epoll error, fd = %d\n", __FILE__, __LINE__, conn_fd );
        delFd( &fd_list, fd_list.fds + idx );
	return -1;
    }
    (ep->fd_count)++;
    fd_list.fds[idx].flags |= FD_IS_EPOLLIN;

    return 0;
}

int _del_fd( ForEpoll * ep, int fd ) {
    assert( ep != NULL );
    assert( fd >= 0 );

    if ( ( epoll_ctl( ep->epoll_fd, EPOLL_CTL_DEL, fd, &(ep->ev) ) ) < 0 ) {
        dprintf(2, "Error[%s:%d]: epoll_ctl_del error, fd=%d errmsg=%s\n", __FILE__, __LINE__, fd, strerror(errno));
        return -1;
    }
    --(ep->fd_count);
    
    return 0;
}

/*
 * == desc ==
 * fdnode 的资源：
 *   (1) fd list 的一个位置
 *   (2) fd
 *   (3) memory: FdNode.bf
 *   (4) 关联的pipe的一个tunnel位置
 *   (5) 与pipe的关联关系
 *   (6) epoll
 */
int try_free_fn( FdNode * fn, ForEpoll * ep ) {
    assert( ep != NULL );
    assert( fn != NULL );

    if ( fn->fd >= 0 ) {
        
        if ( fn->p ) {
    	    // 这里不关心 exit 是否成功，
    	    // 成功了最好，不成功无非就是：
    	    //   (1) tun list is empty
    	    //   (2) not found
    	    // 正合我意。
    	    if ( isTunFd( fn ) ) {
                printf("debug[%s:%d]: exitTunList\n", __FILE__, __LINE__);
                exitTunList( &(fn->p->tun_list), fn->fd );
    	    }
    	    else {
    	        unsetPipeFd( fn->p );
    	    }
    
            fn->p = NULL;
    	    fn->flags &= ( ~FDNODE_AUTHED );
        }
        
        // epoll del
        if ( ( fn->flags & FD_IS_EPOLLIN ) || ( fn->flags & FD_IS_EPOLLOUT ) ) {
            printf("debug[%s:%d]: _del_fd\n", __FILE__, __LINE__);
            if ( _del_fd( ep, fn->fd ) ) {
                return -1;
            }
    	    fn->flags &= ( ~FD_IS_EPOLLIN );
    	    fn->flags &= ( ~FD_IS_EPOLLOUT );
        }
        
        close( fn->fd );
        fn->fd = -1;
    
        // 释放 fd list 位置
        // 释放bf
        delFd( &fd_list, fn );
    }

    return 0;
}

/*
 * == desc ==
 * fdnode 的资源：
 *   (1) fd 和 tunnel fds 关联的fdnode
 *   (3) pipe list 的一个位置
 *
 * == return ==
 * -66: check codes
 *  -1: epoll del error
 *   0: ok
 */ 
int try_free_pipe( Pipe * p, ForEpoll * ep ) {
    int      j;
    FdNode * fn;

    assert( p != NULL );
    assert( ep != NULL );
    
    printf("debug[%s:%d]: free pipe[%16s]\n", __FILE__, __LINE__, p->key);

    if ( hasPipeFd( p ) ) {
        printf("debug[%s:%d]: clean merge fd %d\n", __FILE__, __LINE__, p->fd);
	if ( try_free_fn( (FdNode *)(p->fd_fn), ep ) ) {
	    return -1;
	}
        unsetPipeFd( p );
    }

    for ( j = 0; j < p->tun_list.len; j++ ) {
        if ( hasTunFd( p->tun_list.tuns + j ) ) {
            printf("debug[%s:%d]: clean tunnel fd %d\n", 
			    __FILE__, __LINE__, 
			    p->tun_list.tuns[j].fd);
	    if ( try_free_fn( (FdNode *)(p->tun_list.tuns[j].fd_fn), ep ) ) {
	        return -1;
	    }
            unsetTunFd( p->tun_list.tuns + j );
        }
    }
    
    printf("debug[%s:%d]: delete pipe\n", __FILE__, __LINE__);
    delPipeByI( &plist, p->idx );

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
	printf("debug[%s:%d]: epoll waiting... \n", __FILE__, __LINE__ );
        if ( ( ep.wait_fds = epoll_wait( ep.epoll_fd, ep.evs, ep.fd_count, timeout ) ) == -1 ) {
            dprintf( 2, "Epoll Wait Error: %s\n", strerror( errno ) );
            exit( EXIT_FAILURE );
        }
 
	//==== 2) 碰到epoll事件就处理事件
	printf("debug[%s:%d]: do events\n", __FILE__, __LINE__ );
        for ( i = 0; i < ep.wait_fds; i++ ) {
	    /* 接收客户端连接，并：
	     *   (1)加入fd list
	     *   (2)epoll
	     */
            if ( ep.evs[i].data.fd == listen_fd && ep.fd_count < MAXEPOLL ) {
                rt = _accept_a_client( listen_fd, &ep );
		if ( rt == -2 ) {
		    // resource errors
                    exit( EXIT_FAILURE );
		}
		continue;
	    }

	    /* accept 的 conn_fd 都是 tunnel fd，
	     * 每个 conn_fd 来的时候都会加入 fd list，
	     * 如果找不到 fdnode，就是程序逻辑有问题。
	     *
	     */
	    printf("debug[%s:%d]: events loop - i=%d fd=%d\n", 
			    __FILE__, __LINE__, 
			    i, ep.evs[i].data.fd );
            fd_node = searchByFd( &fd_list, ep.evs[i].data.fd );
            if ( ! fd_node ) {
	        printf("Fatal[%s:%d]: check codes\n", __FILE__, __LINE__);
                exit( EXIT_FAILURE );
	    }

	    /* 这里进行服务鉴别工作。
	     * 看新来的conn_fd时要 new pipe 还是 join pipe。
	     *
	     * 
	     *
	     */
	    // authytication
            if ( isTunFd( fd_node ) && notAuthed( fd_node ) ) {
	        //printf("debug[%s:%d]: auth => go\n", __FILE__, __LINE__);
                rt = authCli( fd_node, &plist, &ecode, mapping_addr, &fd_list );
		switch ( rt ) {
		    case -66:
                        exit( EXIT_FAILURE );

	            case -2:
		        if ( try_free_pipe( fd_node->p, &ep ) ) {
		            dprintf(2, "Error[%s:%d]: try_free_pipe failed\n", 
		         		   __FILE__, __LINE__);
		        }
			break;

		    case -1: // resource errors(memory; socket)
		        printf("debug: fd %d has been deleted\n", fd_node->fd);
		        if ( try_free_fn( fd_node, &ep ) ) {
		            dprintf(2, "Error[%s:%d]: try_free_fn failed\n", 
		         		   __FILE__, __LINE__);
		        }
		        break;
		    
		    case 0: // socket block
	                //printf("debug[%s:%d]: auth => socket block\n", __FILE__, __LINE__);
		        
		        // 如果FdNode处于发送数据卡住，则监听EPOLLOUT事件
		        if ( fd_node->auth_status == TUN_REPLIED && 
					!(fd_node->flags & FD_IS_EPOLLOUT) ) {
			
                            ep.ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
                            ep.ev.data.fd = fd_node->fd;
                            if ( epoll_ctl( ep.epoll_fd, EPOLL_CTL_MOD, fd_node->fd, &(ep.ev) ) < 0 ) {
	                        printf("Error[%s:%d]: epoll error\n", __FILE__, __LINE__);
                                exit( EXIT_FAILURE );
                            }
			    fd_node->flags |= FD_IS_EPOLLOUT;
			}
		        break;

		    case 1: // auth successfully
		        if ( fd_node->flags & FD_IS_EPOLLOUT ) {
			
                            ep.ev.events = EPOLLIN | EPOLLET;
                            ep.ev.data.fd = fd_node->fd;
                            if ( epoll_ctl( ep.epoll_fd, EPOLL_CTL_MOD, fd_node->fd, &(ep.ev) ) < 0 ) {
	                        printf("Error[%s:%d]: epoll error\n", __FILE__, __LINE__);
                                exit( EXIT_FAILURE );
                            }
			    fd_node->flags &= ( ~FD_IS_EPOLLOUT );
			}

	                printf("debug[%s:%d]: auth => ok\n", __FILE__, __LINE__);
                        fn2 = searchByFd( &fd_list, fd_node->p->fd );
                        if ( fn2 ) {
			    if ( !( fn2->flags & FD_IS_EPOLLIN ) ) {
		                // 将 fd 加入 epoll 监听
                                ep.ev.events = EPOLLIN | EPOLLET;
                                ep.ev.data.fd = fn2->fd;
                                if ( epoll_ctl( ep.epoll_fd, EPOLL_CTL_ADD, fn2->fd, &(ep.ev) ) < 0 ) {
                                    dprintf( 2, "Epoll Error: %s\n", strerror ( errno ) );
                                    exit( EXIT_FAILURE );
                                }
                                ++ep.fd_count;
				fn2->flags |= FD_IS_EPOLLIN;
	                        //printf("debug[%s:%d]: add mapping fd %d to epoll\n", __FILE__, __LINE__, fn2->p->fd);
	                    }
			    else {
	                        ;//printf("debug[%s:%d]: mapping fd %d already in epoll\n", __FILE__, __LINE__, fn2->p->fd);
			    }
			    
			}
			else {
		            dprintf(2, "Fatal[%s:%d]: check codes\n", __FILE__, __LINE__);
			}
			fn2 = NULL;
		        break;

		    default:
		        dprintf(2, "Fatal[%s:%d]: unprobablely error rutern: %d, please check codes\n", __FILE__, __LINE__, rt);
		}
		continue;
	    }

            rt = relay( fd_node->p, &ep, i, fd_node );
	    if ( fd_node->p->stat == P_STAT_END || fd_node->p->stat == P_STAT_BAD ) {
	        try_free_pipe( fd_node->p, &ep );
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
    destroyFdList( &fd_list );
    destroyPipeList( &plist );

    return 0;
}
