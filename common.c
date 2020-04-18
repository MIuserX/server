#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

#include "common.h"
//#include "server.h"

void initForEpoll( ForEpoll * ep ) {
    assert( ep != NULL );

    ep->epoll_fd = -1;
    ep->fd_count = 0;
    ep->wait_fds = 0;
}

void destroyForEpoll( ForEpoll * ep ) {
    assert( ep != NULL );

    if ( ep->epoll_fd != -1 ) {
        close( ep->epoll_fd );
    }
}

// 设置非阻塞 
int setnonblocking( int fd ) {
    if ( fcntl( fd, F_SETFL, fcntl( fd, F_GETFD, 0 )|O_NONBLOCK ) == -1 ) {
        printf("Set blocking error : %d\n", errno);
        return -1;
    }
    return 0;
}


int local_listen( int listen_port, int non_blocking ) {
    struct sockaddr_in server_addr;
    int                listen_fd;

    // server 套接口
    bzero( &server_addr, sizeof( server_addr ) );
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl( INADDR_ANY );
    server_addr.sin_port = htons( listen_port );

    // 建立套接字
    if( ( listen_fd = socket( AF_INET, SOCK_STREAM, 0 ) ) == -1 ) {
        dprintf(2, "Socket Error: %s\n" , strerror(errno) );
        exit( EXIT_FAILURE );
    }
    // 设置非阻塞模式
    if ( non_blocking ) {
        if( setnonblocking( listen_fd ) == -1 ) {
            printf("Setnonblocking Error : %s\n", strerror(errno) );
            exit( EXIT_FAILURE );
        }
    }
    // 绑定
    if( bind( listen_fd, ( struct sockaddr *)( &server_addr), sizeof( struct sockaddr ) ) == -1 ) {
        printf("Bind Error : %s\n", strerror(errno) );
        exit( EXIT_FAILURE );
    }
    // 监听
    if( listen( listen_fd, MAXBACK ) == -1 ) {
        printf("Listen Error : %s\n", strerror(errno) );
        exit( EXIT_FAILURE );
    }

    return listen_fd;
}


int epoll_init( int fd, ForEpoll * ep ) {
    // 创建epoll
    ep->epoll_fd = epoll_create( MAXEPOLL );

    // 将listen_fd加入epoll
    ep->ev.events = EPOLLIN | EPOLLET;
    ep->ev.data.fd = fd;
    if ( epoll_ctl( ep->epoll_fd, EPOLL_CTL_ADD, fd, &(ep->ev) ) < 0 ) {
        dprintf(2, "Epoll Error : %s\n", strerror(errno) );
        return -1;
    }
    ep->fd_count = 1;

    return 0;
}

/*
 *
 * == return ==
 *  0: success
 * -1: calling socket error
 * -2: calling setsockopt error
 */
int createSocket(int * fd, char * if_name, size_t name_len) {
    int rt = 0;
    int new_sock = -1;

    assert( fd != NULL );

    // create a socket
    if ( (new_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
        return -1;
    }
    
    if ( if_name ) {
        // bind to specified interface
        if ( setsockopt(new_sock, SOL_SOCKET, SO_BINDTODEVICE, (void *)if_name, name_len) < 0 ) {
            close(new_sock);
            return -2;
        }
    }

    *fd = new_sock;
    return 0;
}


void createKey( char * key ) {
    time_t t = time( NULL );

    assert( key != NULL );
    sprintf(key, "%08X", *( (unsigned int *)(&t) ) );
    sprintf(key + 8, "%08X", rand() );
}
