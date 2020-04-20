#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include "common.h"
#include "tunnel.h"
#include "packet.h"


int initTunnel( Tunnel * t ) {
    assert( t != NULL );

    bzero( (void *)(t->if_name), 36 );
    t->fd = -1;
    t->flags = 0;
    t->status = TUN_INIT;
    t->r_stat = TUN_R_INIT;
    t->w_stat = TUN_W_INIT;
    if ( initBuff( &(t->r_seg), sizeof(Packet), BUFF_MD_2FD ) ||
            initBuff( &(t->w_seg), sizeof(Packet), BUFF_MD_2FD ) ) {
        return -1;
    }
    return 0;
}

/* destroy only free the memory, 
 * don't clean data.
 *
 */
void destroyTunnel( Tunnel * t ) {
    assert( t != NULL );

    if ( t->fd != -1 ) {
        close( t->fd );
    }

    destroyBuff( &(t->r_seg) );
    destroyBuff( &(t->w_seg) );
}

/*
 *
 * == return ==
 *  0: success
 * -1: failed to allocate memory
 */
int initTunList( TunList * tun_list, size_t len ) {
    int i;
    int j;

    assert( tun_list != NULL );
    assert( len > 0 );

    tun_list->tuns = ( Tunnel *)malloc( sizeof(Tunnel) * len );
    if ( ! (tun_list->tuns) ) { 
        return -1; 
    }

    tun_list->len = len;
    tun_list->sz = 0;
    tun_list->cur_idx = 0;
    tun_list->sending_count = 0;
    for ( i = 0; i < len; i++ ) {
	if ( initTunnel( tun_list->tuns + i ) ) {
	    for ( j = 0; j < i; j++ ) {
	        destroyTunnel( tun_list->tuns + i );
	    }
	    free( tun_list->tuns );
	    return -1;
	}
    }

    return 0;
}

void destroyTunList( TunList * tun_list ) {
    int i;

    assert( tun_list != NULL );

    for ( i = 0; i < tun_list->sz; i++ ) {
	destroyTunnel( tun_list->tuns + i );
    }

    free(tun_list->tuns);

    bzero( (void *)tun_list, sizeof(TunList) );
}

int isTunListEmpty( TunList * tun_list ) {
    assert( tun_list != NULL );
    return tun_list->sz == 0;
}

int isTunListFull( TunList * tun_list ) {
    assert( tun_list != NULL );
    return tun_list->sz >= tun_list->len;
}

/*
 * == return ==
 *  0: success
 * -1: tunnel list is empty
 * -2: not found
 */
int exitTunList( TunList * tun_list, int fd ) {
    int i;
    
    assert( tun_list != NULL );
    assert( fd >= 0 );

    //printf("===> tun_list.sz=%d tun_list.len=%d\n", tun_list->sz, tun_list->len);
    if ( isTunListEmpty( tun_list ) ) {
        return -1;
    }

    for ( i = 0; i < tun_list->len; i++ ) {
        if ( tun_list->tuns[i].fd == fd ) {
	    tun_list->tuns[i].fd = -1;
	    tun_list->tuns[i].status = TUN_INIT;
	    tun_list->sz -= 1;
	    return 0;
	}
    }

    return -2;
}

/*
 * == return ==
 *  0: success
 * -1: tunnel list is full
 */
int joinTunList( TunList * tun_list, int fd ) {
    int i;
    
    assert( tun_list != NULL );
    assert( fd >= 0 );

    //printf("===> tun_list.sz=%d tun_list.len=%d\n", tun_list->sz, tun_list->len);
    if ( isTunListFull( tun_list ) ) {
        return -1;
    }

    for ( i = 0; i < tun_list->len; i++ ) {
        if ( tun_list->tuns[i].status == TUN_INIT ) {
	    tun_list->tuns[i].fd = fd;
	    tun_list->tuns[i].status = TUN_AUTHED;
	    tun_list->sz += 1;
	    break;
	}
    }

    return 0;
}

/*
 *
 * #### Param
 * tun_list: TunList 结构体指针，是返回参数 
 * serv_addr: 合并流量的server端地址
 *
 * ==== return ====
 *  0: all 
 * -1: 
 * -2:  
 */
int 
activeTunnels( TunList * tun_list, struct sockaddr_in serv_addr, char ** if_list ) {
    int i;
    int actived_len = 0;
    
    assert( tun_list != NULL );
    assert( if_list != NULL );

    for ( i = 0; i < tun_list->len; i++ ) {
        if ( ( createSocket( &(tun_list->tuns[i].fd), if_list[i], if_list[i] ? strlen(if_list[i]) : 0) ) < 0 ) {
            continue;
	}

	if ( ( connect( tun_list->tuns[i].fd, (struct sockaddr *)(&serv_addr), sizeof(serv_addr) ) ) == -1 ) {
            close( tun_list->tuns[i].fd );
	    continue;
	}

	if ( setnonblocking( tun_list->tuns[i].fd ) ) {
            close( tun_list->tuns[i].fd );
	    continue;
	}
    
	actived_len += 1;

        tun_list->tuns[i].status = TUN_ACTIVE;
        tun_list->sz += 1;
    }

    if ( 0 == actived_len ) {
        return -2;
    }
    else if ( actived_len < tun_list->len ) {
        return -1;
    }

    return 0;
}

/*
 * == return ==
 *  rt >= 0: is the tunnel fd
 *       -1: no active tunnel
 */
int getATun( TunList * tun_list ) {
    int loop_cnt = 0;
    int idx;

    assert( tun_list != NULL );

    idx = (tun_list->cur_idx + 1) % tun_list->sz;
    while ( loop_cnt < tun_list->sz ) {
        if ( tun_list->tuns[idx].status == TUN_AUTHED ) {
            tun_list->cur_idx = idx;
            return tun_list->tuns[idx].fd;
	}
	loop_cnt++;
        idx = (idx + 1) % tun_list->sz;
    }
    
    dprintf(2, "Error: no available tunnel\n");
    return -1;
}

