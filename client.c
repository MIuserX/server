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

#include "service.h"
#include "server.h"

struct {
    int    local_port; // 本地监听端口
    char * nic_list[TUN_LIST_SZ];
    int    ntun; // tunnel 个数

    char * server_addr;
    int    server_port;
} cli_conf = {
    10010,
    { NULL, NULL },
    2,
    "111.229.45.108",
    10082
};


int main(int argc, char *argv[]) {
    int                listen_fd; // 本地监听socket fd
    int                cli_fd;    // accept 的client socket fd
    int                rt;
    struct sockaddr_in serv_addr; // server 地址结构
    struct sockaddr_in cliaddr;   // client 地址结构
    socklen_t          len = sizeof( struct sockaddr_in );
    pthread_t          tid;
    
    CTArg            * ctarg;

    //==== 设置server addr
    memset(&serv_addr, 0, sizeof(serv_addr)); //每个字节都用0填充
    serv_addr.sin_family = AF_INET;           //使用IPv4地址
    serv_addr.sin_addr.s_addr = inet_addr(cli_conf.server_addr);  //具体的IP地址
    serv_addr.sin_port = htons(cli_conf.server_port);             //端口

    //==== 本地监听
    listen_fd = local_listen( cli_conf.local_port, 0 );

    //==== main loop
    while ( 1 ) {
	//== 阻塞在这里等待epoll事件
	printf("debug[%s:%d]: accept...\n", __FILE__, __LINE__);
        if ( ( cli_fd = accept( listen_fd, (struct sockaddr *)(&cliaddr), &len ) ) == -1 ) {
            dprintf( 2, "Error: accept:%s\n", strerror( errno ) );
            exit( EXIT_FAILURE );
        }
	printf("info[%s:%d]: client: %s:%d\n", __FILE__, __LINE__, inet_ntoa(cliaddr.sin_addr), cliaddr.sin_port );

        ctarg = (CTArg *)malloc( sizeof(CTArg) );
	if ( ! ctarg ) {
            dprintf( 2, "Error: no memory:%s\n", strerror( errno ) );
            exit( EXIT_FAILURE );
	}

	//== 将 fd 加入 fd_list and create a pipe to server
	if ( initPipe( &(ctarg->p), P_BUFF_SZ, cli_conf.ntun ) ) {
            close(cli_fd);
            dprintf( 2, "Error: no memory: %s\n", strerror(errno) );
	    continue;
	}
	createKey( ctarg->p.key );
	ctarg->p.fd = cli_fd;
	ctarg->p.stat = P_STAT_ACTIVE;

	//== 
	printf("debug[%s:%d]: active tunnels\n", __FILE__, __LINE__);
        if ( activeTunnels( &(ctarg->p.tun_list) , serv_addr, cli_conf.nic_list ) ) {
            close(cli_fd);
            dprintf( 2, "Error: active tunnels failed: %s\n", strerror(errno) );
	    destroyPipe( &(ctarg->p) );
	    continue;
	}

	//== 
	printf("debug[%s:%d]: create pthread\n", __FILE__, __LINE__);
        if ( ( rt = pthread_create( &tid, NULL, client_pthread, ctarg ) ) != 0 ) {
            close(cli_fd);
            dprintf( 2, "Error: pthread_create failed, rt=%d\n", rt );
	    destroyPipe( &(ctarg->p) );
	    continue;
	}
    }

    return 0;
}
