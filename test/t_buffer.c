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

#include "../pipe.h"
#include "../common.h"
#include "../server.h"
#include "../buffer.h"

#define PORT 10085

//test1: 
void test1() {
    Buffer b;

    initBuff( &b, 16, BUFF_MD_ACK );
    dumpBuff( &b );
    destroyBuff( &b );
}

//test2:
void test2() {
    Buffer b;
    char               bf[1024];
    int                ret = 0;
    size_t             want_sz;
    int                i;
    struct sockaddr_in peer;
    struct sockaddr_in cliaddr;
    int                len = sizeof(peer);
    pthread_t          pid;

    int                listen_fd;
    int                conn_fd;
    ForEpoll           ep;
    int                rt = 0;

    initBuff( &b, 16, BUFF_MD_2FD );
    dumpBuff( &b );

    // 1. listen
    listen_fd = local_listen(PORT);
    printf("Debug: listen ok\n");

    // 2. epoll
    epoll_init( listen_fd, &ep );

    // 3. main loop
    while ( 1 ) {
        // 阻塞在这里等待epoll事件
	printf("=> epoll_wait....\n");
        if ( ( ep.wait_fds = epoll_wait( ep.epoll_fd, ep.evs, ep.fd_count, -1 ) ) == -1 ) {
            dprintf( 2, "Epoll Wait Error: %s\n", strerror( errno ) );
            exit( EXIT_FAILURE );
        }

	printf("=> epoll events\n");
        // 碰到epoll事件就处理事件
        for ( i = 0; i < ep.wait_fds; i++ ) {

            // if listen_fd has events, accept
            if ( ep.evs[i].data.fd == listen_fd && ep.fd_count < MAXEPOLL ) {
                if ( ( conn_fd = accept( listen_fd, (struct sockaddr *)&cliaddr, &len ) ) == -1 ) {
                    dprintf( 2, "Accept Error: %s\n", strerror( errno ) );
                    exit( EXIT_FAILURE );
                }
                printf("Server get from client !\n"/*,  inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port */);

                ep.ev.events = EPOLLIN | EPOLLET;  // accept Read!
                ep.ev.data.fd = conn_fd;           // 将conn_fd 加入
                if ( epoll_ctl( ep.epoll_fd, EPOLL_CTL_ADD, conn_fd, &(ep.ev) ) < 0 ) {
                    dprintf( 2, "Epoll Error: %s\n", strerror ( errno ) );
                    exit( EXIT_FAILURE );
                }
                ++ep.fd_count;
                continue;
            }

            printf("\n====测试isBuffFull\n");
	    printf("isBuffFull = %s\n", isBuffFull( &b ) ? "true" : "false" );
	    if ( ! isBuffFull( &b ) ) {
		rt = putBytesFromFd( &b, ep.evs[i].data.fd );
		printf("putBytesFromFd rt=%d\n", rt);
		dumpBuff( &b );
	    }
	    printf("isBuffFull = %s\n", isBuffFull( &b ) ? "true" : "false" );

            printf("\n====测试isBuffEmpty\n");
	    printf("isBuffEmpty = %s\n", isBuffEmpty( &b ) ? "true" : "false" );
	    if ( ! isBuffEmpty( &b ) ) {
		bzero( (void *)bf, 64 );

		// 测试mode检查是否可用
                printf("\n====测试mode检查是否可用\n");
                b.mode = BUFF_MD_ACK;
		want_sz = 3;
		rt = getBytes( &b, bf, &want_sz );
		printf("getBytes rt=%d, rt_sz=%lu\n", rt, want_sz);
		dumpBuff( &b );
                b.mode = BUFF_MD_2FD;

		// 测试读取 sz 小于buffer的bytes数
                printf("\n====测试读取 sz 小于buffer的bytes数\n");
		bzero( (void *)bf, 64 );
		want_sz = 3;
		rt = getBytes( &b, bf, &want_sz );
		printf("getBytes rt=%d, rt_sz=%lu, bytes=\"%s\"\n", rt, want_sz, bf);
		dumpBuff( &b );

		// 测试读取 sz 大于buffer的bytes数
		printf("\n====测试读取 sz 大于buffer的bytes数\n");
		bzero( (void *)bf, 64 );
		want_sz = 30;
		rt = getBytes( &b, bf, &want_sz );
		printf("getBytes rt=%d, rt_sz=%lu, bytes=\"%s\"\n", rt, want_sz, bf);
		dumpBuff( &b );
	    }
	    printf("isBuffEmpty = %s\n", isBuffEmpty( &b ) ? "true" : "false" );
	}
    }

    destroyBuff( &b );
}

// test3: test BUFF_MD_ACK
void test3() {
    Buffer             b;
    char               bf[1024];
    size_t             want_sz;
    int                i;
    struct sockaddr_in peer;
    struct sockaddr_in cliaddr;
    int                len = sizeof(peer);

    int                listen_fd;
    int                conn_fd;
    ForEpoll           ep;
    int                rt = 0;
    int                ack = 0;

    initBuff( &b, 16, BUFF_MD_ACK );
    dumpBuff( &b );

    // 1. listen
    listen_fd = local_listen(PORT);
    printf("Debug: listen ok\n");

    // 2. epoll
    epoll_init( listen_fd, &ep );

    // 3. main loop
    while ( 1 ) {
        // 阻塞在这里等待epoll事件
	printf("=> epoll_wait....\n");
        if ( ( ep.wait_fds = epoll_wait( ep.epoll_fd, ep.evs, ep.fd_count, -1 ) ) == -1 ) {
            dprintf( 2, "Epoll Wait Error: %s\n", strerror( errno ) );
            exit( EXIT_FAILURE );
        }

	printf("=> epoll events\n");
        // 碰到epoll事件就处理事件
        for ( i = 0; i < ep.wait_fds; i++ ) {

            // if listen_fd has events, accept
            if ( ep.evs[i].data.fd == listen_fd && ep.fd_count < MAXEPOLL ) {
                if ( ( conn_fd = accept( listen_fd, (struct sockaddr *)&cliaddr, &len ) ) == -1 ) {
                    dprintf( 2, "Accept Error: %s\n", strerror( errno ) );
                    exit( EXIT_FAILURE );
                }
                printf("Server get from client !\n"/*,  inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port */);

                ep.ev.events = EPOLLIN | EPOLLET;  // accept Read!
                ep.ev.data.fd = conn_fd;           // 将conn_fd 加入
                if ( epoll_ctl( ep.epoll_fd, EPOLL_CTL_ADD, conn_fd, &(ep.ev) ) < 0 ) {
                    dprintf( 2, "Epoll Error: %s\n", strerror ( errno ) );
                    exit( EXIT_FAILURE );
                }
                ++ep.fd_count;
                continue;
            }

            printf("\n====测试isBuffFull\n");
	    printf("isBuffFull = %s\n", isBuffFull( &b ) ? "true" : "false" );
	    if ( ! isBuffFull( &b ) ) {
		rt = putBytesFromFd( &b, ep.evs[i].data.fd );
		printf("putBytesFromFd rt=%d\n", rt);
		dumpBuff( &b );
	    }
	    printf("isBuffFull = %s\n", isBuffFull( &b ) ? "true" : "false" );

            printf("\n====测试isBuffEmpty\n");
	    printf("isBuffEmpty = %s\n", isBuffEmpty( &b ) ? "true" : "false" );
	    if ( ! isBuffEmpty( &b ) ) {
		bzero( (void *)bf, 64 );

		if ( ack == 0 ) {
		    // 测试mode检查是否可用
                    printf("\n====测试mode检查是否可用\n");
                    b.mode = BUFF_MD_2FD;
		    want_sz = 3;
		    rt = preGetBytes( &b, bf, &want_sz, 1 );
		    printf("preGetBytes rt=%d, rt_sz=%lu\n", rt, want_sz);
		    dumpBuff( &b );
                    b.mode = BUFF_MD_ACK;

		    printf("\n====测试未preGetBytes前ack\n");
		    rt = ackBytes( &b, 4 );
		    printf("ackBytes rt=%d\n", rt);
		    dumpBuff( &b );

		    // 测试读取 sz 小于buffer的bytes数
                    printf("\n====测试取 sz 小于buffer的bytes数\n");
		    bzero( (void *)bf, 64 );
		    want_sz = 3;
		    rt = preGetBytes( &b, bf, &want_sz, 2 );
		    printf("preGetBytes rt=%d, rt_sz=%lu, bytes=\"%s\"\n", rt, want_sz, bf);
		    dumpBuff( &b );
                    
		    printf("\n====测试ack号错误\n");
		    rt = ackBytes( &b, 1 );
		    printf("ackBytes rt=%d\n", rt);
		    dumpBuff( &b );

		    printf("\n====测试ack号正确\n");
		    rt = ackBytes( &b, 2 );
		    printf("ackBytes rt=%d\n", rt);
		    dumpBuff( &b );

	        }
		
		if ( ack == 0 ) {
		    printf("\n====测试读取 sz 大于buffer的bytes数\n");
		} else if ( ack == 1 ) {
		    printf("\n====测试ack过的空间是否被用到\n");
		}
		bzero( (void *)bf, 64 );
		want_sz = 30;
		switch ( ack ) {
		    case 0:
		    rt = preGetBytes( &b, bf, &want_sz, 3 );
		    printf("preGetBytes seq=3 rt=%d, rt_sz=%lu, bytes=\"%s\"\n", rt, want_sz, bf);
		    break;
		    case 1:
		    rt = preGetBytes( &b, bf, &want_sz, 4 );
		    printf("preGetBytes seq=4 rt=%d, rt_sz=%lu, bytes=\"%s\"\n", rt, want_sz, bf);
		    break;
		    case 2:
		    rt = preGetBytes( &b, bf, &want_sz, 5 );
		    printf("preGetBytes seq=4 rt=%d, rt_sz=%lu, bytes=\"%s\"\n", rt, want_sz, bf);
		    break;
		}
		dumpBuff( &b );

		if ( ack == 1 ) {
		    printf("\n====测试ack号正确ack_seq=3\n");
		    rt = ackBytes( &b, 3 );
		    printf("ackBytes rt=%d\n", rt);
		    dumpBuff( &b );
		    
		    printf("\n====测试ack号正确ack_seq=4\n");
		    rt = ackBytes( &b, 4 );
		    printf("ackBytes rt=%d\n", rt);
		    dumpBuff( &b );
		}
		if ( ack == 2 ) {
		    printf("\n====测试ack号正确ack_seq=5\n");
		    rt = ackBytes( &b, 5 );
		    printf("ackBytes rt=%d\n", rt);
		    dumpBuff( &b );
		}

	        ack += 1;
	    }
	    printf("isBuffEmpty = %s\n", isBuffEmpty( &b ) ? "true" : "false" );
	}
    }

    destroyBuff( &b );
}

/* 测试：
 *   getBytes
 *   putBytes
 *
 */
void test4() {
    char  bf[1024];
    char  rbf[1024];
    size_t             want_sz;
    int                conn_fd;
    ForEpoll           ep;
    int                rt = 0;
    Buffer    b;

    initBuff( &b, 8 , BUFF_MD_2FD );

    bzero( bf, 1024 );
    bzero( rbf, 1024 );
    sprintf(bf, "hello world.");
    if ( ! isBuffFull( &b ) ) {
	dumpBuff( &b );
	printf("putBytes sz=16\n");
	want_sz = 16;
	rt = putBytes( &b, bf, &want_sz );
	printf("putBytes rt=%d rtsz=%lu\n", rt, want_sz);
	dumpBuff( &b );
    }
    
    printf("\n====\n");
    // 测试读取小于buffer已有数据
    if ( ! isBuffEmpty( &b ) ) {
	printf("getBytes sz=1\n");
	want_sz = 1;
	rt = getBytes( &b, rbf, &want_sz );
	printf("getBytes rt=%d rtsz=%lu rt=%s\n", rt, want_sz, rbf);
	dumpBuff( &b );
    }
    printf("\n====\n");
    
    // 测试读取大于buffer已有数据
    if ( ! isBuffEmpty( &b ) ) {
	bzero(rbf, 1024);
	printf("getBytes sz=20\n");
	want_sz = 20;
	rt = getBytes( &b, rbf, &want_sz );
	printf("getBytes rt=%d rtsz=%lu rt=%s\n", rt, want_sz, rbf);
	dumpBuff( &b );
    }

    printf("\n====\n");
    // 测试尾部写完头部写
    // put 5
    if ( ! isBuffFull( &b ) ) {
	printf("putBytes sz=5\n");
	want_sz = 5;
	rt = putBytes( &b, bf, &want_sz );
	printf("putBytes rt=%d rtsz=%lu\n", rt, want_sz);
	dumpBuff( &b );
    }
    
    // get 3
    printf("\n====\n");
    if ( ! isBuffEmpty( &b ) ) {
	bzero( rbf, 1024 );
	printf("getBytes sz=3\n");
	want_sz = 3;
	rt = getBytes( &b, rbf, &want_sz );
	printf("getBytes rt=%d rtsz=%lu rt=%s\n", rt, want_sz, rbf);
	dumpBuff( &b );
    }
    
    printf("\n====\n");
    // put 5
    if ( ! isBuffFull( &b ) ) {
	printf("putBytes sz=5\n");
	want_sz = 5;
	rt = putBytes( &b, bf, &want_sz );
	printf("putBytes rt=%d rtsz=%lu\n", rt, want_sz);
	dumpBuff( &b );
    }
    
    printf("\n====\n");
    // get 0
    // 测试用 0 读取所有数据
    if ( ! isBuffEmpty( &b ) ) {
	bzero( rbf, 1024 );
	printf("getBytes sz=0\n");
	want_sz = 0;
	rt = getBytes( &b, rbf, &want_sz );
	printf("getBytes rt=%d rtsz=%lu rt=%s\n", rt, want_sz, rbf);
	dumpBuff( &b );
    }
    printf("\n====\n");

    // put 10
    if ( ! isBuffFull( &b ) ) {
	printf("putBytes sz=10\n");
	want_sz = 10;
	rt = putBytes( &b, bf, &want_sz );
	printf("putBytes rt=%d rtsz=%lu\n", rt, want_sz);
	dumpBuff( &b );
    }
}


void test_loop() {
    Buffer b;
    char               bf[1024];
    int                ret = 0;
    size_t             want_sz;
    int                i;
    struct sockaddr_in peer;
    struct sockaddr_in cliaddr;
    int                len = sizeof(peer);

    int                listen_fd;
    int                conn_fd;
    ForEpoll           ep;
    int                rt = 0;

    initBuff( &b, 16, BUFF_MD_2FD );
    dumpBuff( &b );

    // 1. listen
    listen_fd = local_listen(PORT);
    printf("Debug: listen ok\n");

    // 2. epoll
    epoll_init( listen_fd, &ep );

    // 3. main loop
    while ( 1 ) {
        // 阻塞在这里等待epoll事件
	printf("=> epoll_wait....\n");
        if ( ( ep.wait_fds = epoll_wait( ep.epoll_fd, ep.evs, ep.fd_count, -1 ) ) == -1 ) {
            dprintf( 2, "Epoll Wait Error: %s\n", strerror( errno ) );
            exit( EXIT_FAILURE );
        }

	printf("=> epoll events\n");
        // 碰到epoll事件就处理事件
        for ( i = 0; i < ep.wait_fds; i++ ) {

            // if listen_fd has events, accept
            if ( ep.evs[i].data.fd == listen_fd && ep.fd_count < MAXEPOLL ) {
                if ( ( conn_fd = accept( listen_fd, (struct sockaddr *)&cliaddr, &len ) ) == -1 ) {
                    dprintf( 2, "Accept Error: %s\n", strerror( errno ) );
                    exit( EXIT_FAILURE );
                }
                printf("Server get from client !\n"/*,  inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port */);

                ep.ev.events = EPOLLIN | EPOLLET;  // accept Read!
                ep.ev.data.fd = conn_fd;           // 将conn_fd 加入
                if ( epoll_ctl( ep.epoll_fd, EPOLL_CTL_ADD, conn_fd, &(ep.ev) ) < 0 ) {
                    dprintf( 2, "Epoll Error: %s\n", strerror ( errno ) );
                    exit( EXIT_FAILURE );
                }
                ++ep.fd_count;
                continue;
            }

	}
    }

    destroyBuff( &b );
}

int main() {
    test4();

    return 0;
}
