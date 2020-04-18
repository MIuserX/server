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

#include "all.h"
#include "common.h"

#define MODE_C2T (1)
#define MODE_T2C (2)

#define PIPE_SZ (10240)

/* 
 *##### Param: 
 * mode:
 *     1 : client to tunnels
 *     2 : tunnels to client
 *
 *##### Return:
 * -1 : 获取不到可用的tunnel
 * -2 : pipe中没有数据待写
 *
 */ 
int one2one(int mode, int cli_fd, Pipe *p, TunList *tun_list) { 
    // 将buffer的数据发送给 tunnel 端,

    // 1.组织packet, 发送数据
    // 2.更新last_seq
    // 3.
    int      fd;
    char   * buff;
    size_t   sz;
    ssize_t  nwrite;

    if ( mode == MODE_C2T ) {
	while ( ! isEmpty(p) ) {
	    sz = 0;
            if ( (fd = getATun(tun_list)) == -1 ) {
                dprintf(2, "Error: no available tunnel\n");
	        return -1;
            }
	    if ( (buff = outPipe(p, &sz)) == NULL ) {
                dprintf(2, "Error: pipe has no data\n");
	        return -2;
	    }
            if ( (nwrite = send( fd, (void *)buff, sz, 0 )) == -1 ) {
                dprintf(2, "Error: send error: %s\n", strerror(errno));
	        return -3;
	    }
	}
    } else if ( mode == MODE_T2C ) {
	while ( ! isEmpty(p) ) {
	    sz = 0;
	    printf("Debug: will pop data from t2c pipe\n");
	    if ( (buff = outPipe(p, &sz)) == NULL ) {
                dprintf(2, "Error: pipe has no data\n");
	        return -2;
	    }
	    printf("Debug: %ld bytes poped from t2c pipe\n", sz);
	    printf("Debug: will send data to client\n");
            if ( (nwrite = send( cli_fd, (void *)buff, sz, 0 )) == -1 ) {
                dprintf(2, "Error: send error: %s\n", strerror(errno));
	        return -3;
	    }
	    printf("Debug: %ld bytes sent to client\n", nwrite);
	}
    }

    return 0;
}


/*
 *
 *
 * Return:
 *     -1 : 参数错误
 *     -2 : 创建 tunnel list 失败
 *     -3 : 申请缓冲区失败
 *     -4 : client fd down
 *
 * */
void * service_thread(void * arg) {
    int                rt;
    ForEpoll           ep;       // epoll struct
    int                i;
    long int           nread;    // 
    long int           nwrite;   // 
    struct sockaddr_in cli_addr;
    int                len = sizeof(cli_addr);

    int                cli_fd;
    uint8_t            status;
    Pipe               c2t_pipe;
    Pipe               t2c_pipe;
    TunList            tun_list;

 
    if ( !arg ) {
        dprintf(2, "Error: arg is NULL\n");
	rt = -1;
	pthread_exit((void *)&rt);
    }

    if ( initPipe( &c2t_pipe, PIPE_SZ ) || initPipe( &t2c_pipe, PIPE_SZ ) ) {
        dprintf(2, "Error: allocate buffer failed\n");
	rt = -3;
	pthread_exit((void *)&rt);
    }

    // get client fd
    cli_fd = *((int *)arg);
    printf("Debug: pthread cli_fd = %d\n", cli_fd);

    // init epoll
    epoll_init(cli_fd, &tun_list, &ep);

    // main loop
    while ( 1 ) {
	// 阻塞在这里等待epoll事件
	printf("Debug: epoll_wait...\n");
        if( ( ep.wait_fds = epoll_wait( ep.epoll_fd, ep.evs, ep.fd_count, -1 ) ) == -1 ) {
            printf( "Epoll Wait Error: %s\n", strerror(errno) );
            exit( EXIT_FAILURE );
        }
	printf("Debug: got epoll events\n");
 
	// 碰到epoll事件就处理事件
        for ( i = 0; i < ep.wait_fds; i++ ) {
	    if ( ep.evs[i].data.fd == cli_fd ) {
                // 如果是client来的数据，就往buff里读
	        //
	        printf("Debug: client has unread data\n");
	        inPipe(ep.evs[i].data.fd, &c2t_pipe, &nread);
	        printf("Debug: read %ld bytes to pipe\n", nread);
            } else {
                // 如果是tunnel端来的数据, 就
	        printf("Debug: tunnels(fd=%d) has unread data\n", ep.evs[i].data.fd);
	        inPipe(ep.evs[i].data.fd, &t2c_pipe, &nread);
	        printf("Debug: read %ld bytes to pipe\n", nread);
	    }
            if ( nread <= 0 ) {
                close( ep.evs[i].data.fd );
                epoll_ctl( ep.epoll_fd, EPOLL_CTL_DEL, ep.evs[i].data.fd, &(ep.ev) );
                --ep.fd_count;

		if ( ep.evs[i].data.fd == cli_fd ) {
		    dprintf(2, "Error: client fd has been down, pthread would exit\n");
                    rt = -4;
		    pthread_exit( (void *)(&rt) );
		}

                continue;
            }


	    if ( ep.evs[i].data.fd == cli_fd ) {
                /* 如果是client来的数据, 应该将数据发送到tunnel.
		 * 发送前需要看上一个packet是否被确认, 确认了才发这一个.
	         */

		// 如果上一个packet得到ack, 就发新的packet;
		// 如果没有ack, 就等待
                if ( c2t_pipe.last_send_ack == c2t_pipe.last_send_seq ) {
		    one2one( MODE_C2T, cli_fd, &c2t_pipe, &tun_list );
		}
	    } else {
                /* 如果是 tunnel 来的数据, 直接往 client 发就是
		 */
		one2one( MODE_T2C, cli_fd, &t2c_pipe, &tun_list );
            }
        }
    }

    close( cli_fd );
    destoryTunList( &tun_list );
    pthread_exit( (void *)0 );
}
