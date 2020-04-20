#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <unistd.h>

#include "server.h"
#include "tunnel.h"

void initFdList( FdList * fl ) {
    assert( fl != NULL );
    bzero( (void *)fl, sizeof(fl) );
    bzero( (void *)(fl->fds), sizeof(FdNode) * MAX_FDS );
}

void destroyFdList( FdList * fl ) {
    int i;

    assert( fl != NULL );

    for ( i = 0; i < MAX_FDS; i++ ) {
        if ( fl->fds[i].use ) { 
	    if ( fl->fds[i].fd != -1 ) {
    	        close( fl->fds[i].fd );
	    }
	    fl->fds[i].use = 0;
	}
    }
}

/*
 *
 * == return ==
 * 
 *
 */
int getAEmptyFn( FdList * fl ) {
    int i = 0;

    assert( fl != NULL );

    if ( fl->sz >= MAX_FDS ) {
	return -1;
    }

    while ( i < MAX_FDS && fl->fds[i].use != 0 ) {
        ++i;
    }
    if ( i < MAX_FDS ) {
	fl->nodes[i].use = 1;
	fl->sz++;
        return i;
    }

    return -1;
}

/* 
 * #### Param
 *
 * #### Return
 * -1 : fd list 已经满了
 *  0 : 添加成功
 */
int addFd( FdList * fl, FdNode * fn ) {
    int i = 0;

    assert( fl != NULL );
    assert( fn != NULL );

    if ( fl->sz == MAX_FDS ) {
        dprintf(2, "Error: fd list is full\n");
	return -1;
    }

    while ( i < MAX_FDS && fl->fds[i].use != 0 ) {
        ++i;
    }
    if ( i < MAX_FDS ) {
        fl->fds[i] = ( *fn );
	fl->sz += 1;
    }

    return 0;
}

/* 
 * #### Param
 *
 * #### Return
 * -1 : fd list 是空的
 * -2 : 未找到fd
 *  0 : 删除成功
 *
 */
int delFd( FdList * fl, int fd ) {
    int i = 0;

    if ( fl->sz == 0 ) {
        dprintf(2, "Error: delFd failed, fd list is empty\n");
	return -1;
    }

    for ( i = 0; i < MAX_FDS; i++ ) {
        if ( fl->fds[i].fd == fd && fl->fds[i].use != 0 ) {
	    if ( fl->fds[i].fd >= 0 ) {
	        close( fd );
	    }
            bzero( (void *)(fl->fds + i), sizeof(FdNode) );
            fl->fds[i].fd = -1;
	    fl->sz -= 1;
	    return 0;
        }
    }

    dprintf(2, "Error: delFd failed, no valid node's fd equals to %d\n", fd);
    return -2;
}

/*
 *
 * == return ==
 *  1 : fd doesn't auth
 */
int doesNotAuth( FdNode * fn ) {
    return fn->auth_ok == 0;
}

/*
 *
 *
 */
int cleanAuthTimeout( FdList * fl, int timeout ) {
    int i = 0;

    for ( i = 0; i < MAX_FDS; i++ ) {
        ;
    } 
}

/* 根据 fd 查找 pipe
 * 
 * #### 
 *
 *
 * #### Return
 * 成功返回 fd 所属的 pipe 结构体指针
 *
 */
FdNode * searchByFd( FdList * fl, int fd ) {
    int i;

    for ( i = 0; i < MAX_FDS; i++ ) {
        if ( fl->fds[i].use != 0 && fl->fds[i].fd == fd ) {
            return fl->fds + i;
	}
    }

    return NULL;
}

int isFdListEmpty( FdList * fl ) {
    assert( fl != NULL );
    return fl->sz == 0;
}

int isFdListFull( FdList * fl ) {
    assert( fl != NULL );
    return fl->sz >= MAX_FDS;
}
