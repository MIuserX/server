#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
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


void main_loop( ForEpoll ep, int listen_fd, struct sockaddr_in mapping_addr ) {
    struct sockaddr_in cliaddr;
    socklen_t          len = sizeof( struct sockaddr_in );
    int                conn_fd;
    int                i;
    int                rt;
    int                fd;
    char               rw;
    int                ecode;
    FdNode             fn;
    FdNode           * fd_node;
    FdNode           * fn2;
    int                timeout = -1;
    unsigned int       flags = 0;
    char               rblock = 'n';
    char               wblock = 'n';
    char               loop = 'y';



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
                // accept 客户端连接
    		if ( ( conn_fd = accept( listen_fd, (struct sockaddr *)&cliaddr, &len ) ) == -1 ) {
                    dprintf( 2, "Accept Error: %s\n", strerror( errno ) );
                    exit( EXIT_FAILURE );
                }
                printf( "Info: fd=%d, client %s:%d \n", conn_fd, inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port );
                
                if ( setnonblocking( conn_fd ) ) {
                    close(conn_fd);
	            dprintf(2, "error[%s:%d]: set nonblocking failed, fd = %d\n", __FILE__, __LINE__, conn_fd );
		    continue;
		}

		// 将 fd 加入 fd_list
		fn.use = 1;
		fn.fd = conn_fd;
		fn.type = FD_TUN;
		fn.epollout = 'n';
		fn.t = time( NULL );
		fn.auth_status = TUN_ACTIVE;
		fn.auth_ok = 0;
		fn.p = NULL;
		initBuff( &(fn.bf), sizeof(AuthPacket), BUFF_MD_2FD );
		if ( addFd( &fd_list, &fn ) ) {
                    close(conn_fd);
                    dprintf( 2, "Error: too many users\n" );
		    continue;
		}

		// 将 fd 加入 epoll 监听
                ep.ev.events = EPOLLIN | EPOLLET;
                ep.ev.data.fd = conn_fd;
                if ( epoll_ctl( ep.epoll_fd, EPOLL_CTL_ADD, conn_fd, &(ep.ev) ) < 0 ) {
                    dprintf( 2, "Epoll Error: %s\n", strerror ( errno ) );
                    exit( EXIT_FAILURE );
                }
                ++ep.fd_count;
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
		       if ( ( epoll_ctl( ep.epoll_fd, EPOLL_CTL_DEL, fd_node->fd, &(ep.ev) ) ) < 0 ) {
		           dprintf(2, "Error: epoll_ctl_del error, %s\n", strerror(errno));
		       }
                       --ep.fd_count;
		       printf("debug: fd %d has been deleted from epoll\n", fd);
		       close( fd_node->fd );
		       delFd( &fd_list, fd_node->fd );
		       printf("debug: fd %d has been deleted from fd list\n", fd);
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
	            printf("debug[%s:%d]: mapping fd IN\n", __FILE__, __LINE__);
		    wblock = 'n';
		    rblock = 'n';
		    while ( 1 ) {
                        // 这里是 read 事件，
			// 要尽力做到的是：一直读，直到遇到socket block。
			// 
                        
			//==== 从fd读
                        if ( ! isBuffFull( &(fd_node->p->fd2tun) ) ) {
		            rt = stream( P_STREAM_FD2BUFF, fd_node->p, ep.evs[i].data.fd );
                            switch ( rt ) {
		                case 2: // service ending
		                    dprintf(2, "Error[%s:%d]: mapping fd %d, errno=%d %s\n", 
		                    		__FILE__, __LINE__, 
		                    		fd_node->fd,
		                    		errno, strerror(errno));
		                    fd_node->p->stat = P_STAT_ENDING;
		                    break;

		                case 1: // buffer is full
		                    fd_node->flags |= FD_HAS_UNREAD;
		                    break;

		                case 0: // socket block
		                    fd_node->flags &= ( ~FD_HAS_UNREAD );
		                    rblock = 'y';
			    	break;
		            }
			}
			// 正常情况下，到了这里，会有两种情况：
			// (1) rblock == 'y'
			// (2) buffer is full
			
			// 无论(1)或(2)，都要去尝试往对端写一下。
			// 如果能写出去一点，对于(2)，还能再读点。
                        // 所以，当wblock时，就不用执行下面的逻辑了。
			if ( wblock == 'y' ) {
			    break;
			}

                        //==== 往tunnels写
		        rt = stream( P_STREAM_BUFF2TUN, fd_node->p, ep.evs[i].data.fd  );
                        switch ( rt ) {
		            case 2: // service ending
		                dprintf(2, "Error[%s:%d]: client socket fd %d, errno=%d %s\n", 
		                		__FILE__, __LINE__, 
		                		fd_node->fd,
		                		errno, strerror(errno));
		                fd_node->p->stat = P_STAT_ENDING;
		                break;

		            case 41: // 消耗了bytes
		                fd_node->flags &= ( ~FD_HAS_UNWRITE );
		                break;

		            case 42: // 未消耗bytes
		                fd_node->flags |= FD_HAS_UNWRITE;
		                wblock = 'y';
				break;
		        }
			// 正常情况下，到了这里，会有3种情况：
			// (1) wblock == 'y' && buffer is full
			// (2) wblock == 'y' && buffer is not full
			// (3) wblock == 'n' && buffer is empty

			// 对于(2)和(3)，就要去尝试下再去读，消耗读事件。
			// 但如果读事件已经碰到rblock了，就不用再循环下去了。
			if ( rblock == 'y' || isBuffFull( &(fd_node->p->fd2tun) ) ) {
			    break;
			}
		    }
		}
		if ( ep.evs[i].events & EPOLLOUT ) {
	            printf("debug[%s:%d]: mapping fd OUT\n", __FILE__, __LINE__);
		    wblock = 'n';
		    rblock = 'n';
		    while ( 1 ) {
                        // 这里是 write 事件，
			// 要尽力做到的是：一直写，直到遇到socket block。
			// 

                        //==== 往fd写
			if ( ! isBuffEmpty( &(fd_node->p->tun2fd) ) ) {
		            rt = stream( P_STREAM_BUFF2FD, fd_node->p, ep.evs[i].data.fd );
                            switch ( rt ) {
		                case 2: // service ending
		                    dprintf(2, "Error[%s:%d]: client socket fd %d, errno=%d %s\n", 
		                    		__FILE__, __LINE__, 
		                    		fd_node->fd,
		                    		errno, strerror(errno));
		                    fd_node->p->stat = P_STAT_ENDING;
		                    break;

		                case 1: // send all
		                    fd_node->flags &= ( ~FD_HAS_UNWRITE );
		                    break;

		                case 0: // socket block
		                    fd_node->flags |= FD_HAS_UNWRITE;
		                    wblock = 'y';
			    	    break;
		            }
			}
			// 正常情况下，到了这里，会有两种情况：
			// (1) wblock == 'y'
			// (2) buffer is empty

                        if ( rblock == 'y' ) {
			    break;
			}

                        //==== 从tunnels读
                        if ( ! isBuffFull( &(fd_node->p->tun2fd) ) ) {
		            rt = stream( P_STREAM_TUN2BUFF, fd_node->p, ep.evs[i].data.fd );
                            switch ( rt ) {
		                case 2: // service ending
		                    dprintf(2, "Error[%s:%d]: client socket fd %d, errno=%d %s\n", 
		                    		__FILE__, __LINE__, 
		                    		fd_node->fd,
		                    		errno, strerror(errno));
		                    fd_node->p->stat = P_STAT_ENDING;
		                    break;

		                case 1: // send all
		                    fd_node->flags &= ( ~FD_HAS_UNWRITE );
		                    break;

		                case 0: // socket block
		                    fd_node->flags |= FD_HAS_UNWRITE;
		                    rblock = 'y';
			    	break;
		            }
			}
			// 正常情况下，到了这里，会有两种情况：
			// (1) rblock == 'n' && buffer is full
			// (2) rblock == 'y' && buffer is not full
			// (3) rblock == 'y' && buffer is empty
                        
			if ( wblock == 'y' && isBuffEmpty( &(fd_node->p->tun2fd) ) ) {
			    break;
			}
		    }
		}
	    } else if ( fd_node->type == FD_TUN ) {
		// if is tunnel fd
		// 
		if ( ep.evs[i].events & EPOLLIN ) {
	            printf("debug[%s:%d]: mapping fd OUT\n", __FILE__, __LINE__);
		    wblock = 'n';
		    rblock = 'n';
		    while ( 1 ) {
                        // 这里是 write 事件，
			// 要尽力做到的是：一直写，直到遇到socket block。
			// 
                        //==== 从tunnels读
                        if ( ! isBuffFull( &(fd_node->p->tun2fd) ) ) {
		            rt = stream( P_STREAM_TUN2BUFF, fd_node->p, ep.evs[i].data.fd );
                            switch ( rt ) {
		                case 2: // service ending
		                    dprintf(2, "Error[%s:%d]: client socket fd %d, errno=%d %s\n", 
		                    		__FILE__, __LINE__, 
		                    		fd_node->fd,
		                    		errno, strerror(errno));
		                    fd_node->p->stat = P_STAT_ENDING;
		                    break;

		                case 1: // send all
		                    fd_node->flags &= ( ~FD_HAS_UNWRITE );
		                    break;

		                case 0: // socket block
		                    fd_node->flags |= FD_HAS_UNWRITE;
		                    rblock = 'y';
			    	break;
		            }
			}
			// 正常情况下，到了这里，会有两种情况：
			// (1) rblock == 'n' && buffer is full
			// (2) rblock == 'y' && buffer is not full
			// (3) rblock == 'y' && buffer is empty
                        
			if ( wblock == 'y' && isBuffEmpty( &(fd_node->p->tun2fd) ) ) {
			    break;
			}

                        //==== 往fd写
			if ( ! isBuffEmpty( &(fd_node->p->tun2fd) ) ) {
		            rt = stream( P_STREAM_BUFF2FD, fd_node->p, ep.evs[i].data.fd );
                            switch ( rt ) {
		                case 2: // service ending
		                    dprintf(2, "Error[%s:%d]: client socket fd %d, errno=%d %s\n", 
		                    		__FILE__, __LINE__, 
		                    		fd_node->fd,
		                    		errno, strerror(errno));
		                    fd_node->p->stat = P_STAT_ENDING;
		                    break;

		                case 1: // send all
		                    fd_node->flags &= ( ~FD_HAS_UNWRITE );
		                    break;

		                case 0: // socket block
		                    fd_node->flags |= FD_HAS_UNWRITE;
		                    wblock = 'y';
			    	    break;
		            }
			}
			// 正常情况下，到了这里，会有两种情况：
			// (1) wblock == 'y'
			// (2) buffer is empty

                        if ( rblock == 'y' ) {
			    break;
			}
		    }
		}
		if ( ep.evs[i].events & EPOLLOUT ) {
	            printf("debug[%s:%d]: tunnel fd OUT\n", __FILE__, __LINE__);
		    wblock = 'n';
		    rblock = 'n';
		    while ( 1 ) {
                        // 这里是 write 事件，
			// 要尽力做到的是：一直写，写到写不动为止。
                        
			//==== 往tunnels写
		        rt = stream( P_STREAM_BUFF2TUN, fd_node->p, ep.evs[i].data.fd  );
                        switch ( rt ) {
		            case 2: // service ending
		                dprintf(2, "Error[%s:%d]: client socket fd %d, errno=%d %s\n", 
		                		__FILE__, __LINE__, 
		                		fd_node->fd,
		                		errno, strerror(errno));
		                fd_node->p->stat = P_STAT_ENDING;
		                break;

		            case 41: // 消耗了bytes
		                fd_node->flags &= ( ~FD_HAS_UNWRITE );
		                break;

		            case 42: // 未消耗bytes
		                fd_node->flags |= FD_HAS_UNWRITE;
		                wblock = 'y';
				break;
		        }
			// 正常情况下，到了这里，会有3种情况：
			// (1) wblock == 'y' && buffer is full
			// (2) wblock == 'y' && buffer is not full
			// (3) wblock == 'n' && buffer is empty

			// 对于(2)和(3)，就要去尝试下再去读，消耗读事件。
			// 但如果读事件已经碰到rblock了，就不用再循环下去了。
			if ( rblock == 'y' || isBuffFull( &(fd_node->p->fd2tun) ) ) {
			    break;
			}
                        
			//==== 从fd读
                        if ( ! isBuffFull( &(fd_node->p->fd2tun) ) ) {
		            rt = stream( P_STREAM_FD2BUFF, fd_node->p, ep.evs[i].data.fd );
                            switch ( rt ) {
		                case 2: // service ending
		                    dprintf(2, "Error[%s:%d]: mapping fd %d, errno=%d %s\n", 
		                    		__FILE__, __LINE__, 
		                    		fd_node->fd,
		                    		errno, strerror(errno));
		                    fd_node->p->stat = P_STAT_ENDING;
		                    break;

		                case 1: // buffer is full
		                    fd_node->flags |= FD_HAS_UNREAD;
		                    break;

		                case 0: // socket block
		                    fd_node->flags &= ( ~FD_HAS_UNREAD );
		                    rblock = 'y';
			    	break;
		            }
			}
			// 正常情况下，到了这里，会有两种情况：
			// (1) rblock == 'y'
			// (2) buffer is full
			
			// 无论(1)或(2)，都要去尝试往对端写一下。
			// 如果能写出去一点，对于(2)，还能再读点。
                        // 所以，当wblock时，就不用执行下面的逻辑了。
			if ( wblock == 'y' ) {
			    break;
			}
		    }
		}
	    }
        }

	//==== 3) 没有epoll事件就看是否有unread
	// 有未读尽的fd，timeout会不为-1，
	// 这个循环进入轮询的运行模式，保证不会遗漏数据。
	if ( flags & FD_HAS_UNREAD ) {
	    timeout = -1;
	    printf("warning[%s:%d]: exists unreadall socket\n", __FILE__, __LINE__ );
	    for ( i = 0; i < MAX_FDS; i++ ) {
	        if ( fd_list.fds[i].use != 0 && ( fd_list.fds[i].flags & FD_HAS_UNREAD ) ) {
		    if ( fd_node->type == FD_MERGE ) {
		        ;
			// epoll 监听 tunnels 的 EPOLLOUT 事件
		    } else if ( fd_node->type == FD_TUN ) {
		        ;
			// epoll 监听 mapping fd 的 EPOLLOUT 事件
		    }
		}
	        if ( fd_list.fds[i].use != 0 && ( fd_list.fds[i].flags & FD_HAS_UNWRITE ) ) {
		    if ( fd_node->type == FD_MERGE ) {
		        ;
			// epoll 监听 tunnels 的 EPOLLOUT 事件
		    }
		    else if ( fd_node->type == FD_TUN ) {
		        ;
			// epoll 监听 mapping fd 的 EPOLLOUT 事件
		    }
		}
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

    memset(&mapping_addr, 0, sizeof(mapping_addr));                      //每个字节都用0填充
    mapping_addr.sin_family = AF_INET;                                   //使用IPv4地址
    mapping_addr.sin_addr.s_addr = inet_addr(serv_conf.mapping_addr); //具体的IP地址
    mapping_addr.sin_port = htons(serv_conf.mapping_port);            //端口

    // 1. listen
    listen_fd = local_listen( serv_conf.local_port, 1);

    // 2. epoll
    epoll_init( listen_fd, &ep );

    // 3. main loop
    main_loop( ep, listen_fd, mapping_addr );


    // destroy
    destroyPipeList( &plist );

    return 0;
}
