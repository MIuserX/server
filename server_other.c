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
    int i;

    assert( fl != NULL );
    bzero( (void *)fl, sizeof(fl) );
    
    bzero( (void *)(fl->fds), sizeof(FdNode) * MAX_FDS );

    // 大部分成员变量初始值都是0，
    // 下面初始化哪些不为0的。
    for ( i = 0; i < MAX_FDS; i++ ) {
        fl->fds[i].fd = -1;
	fl->fds[i].idx = i;
    }
}

int notAuthed( FdNode * fn ) {
    assert( fn != NULL );
    return !( fn->flags & FDNODE_AUTHED );
}

int isMergeFd( FdNode * fn ) {
    assert( fn != NULL );
    return !( fn->flags & FDNODE_IS_TUNFD );
}

int isTunFd( FdNode * fn ) {
    assert( fn != NULL );
    return fn->flags & FDNODE_IS_TUNFD;
}

void cleanFdNode( FdNode * fn ) {
    int idx;
    assert( fn != NULL );

    idx = fn->idx;

    if ( fn->flags & FDNODE_BUFF ) {
        destroyBuff( &(fn->bf) );
    }

    bzero( (void *)fn, sizeof(FdNode) );

    // fd 只是在这记录，
    // 并不是 addFd 函数打开的，
    // 所以不负责关闭。
    fn->fd = -1;

    fn->idx = idx;
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
 * >=0: ok
 *  -1: fd list is full
 * -66: data error
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
	fl->fds[i].use = 1;
	fl->fds[i].idx = i;
	fl->sz++;
        return i;
    }

    printf("Fatal[%s:%d]: check codes\n", __FILE__, __LINE__);
    return -66;
}

/*
 *
 * == return ==
 * >=0: 在fd_list中的下标
 *  -1: service errors: fd_list已满
 *  -2: cannot get memory
 * -66: 
 */
static int _add_fd( FdList * fl, int conn_fd, int is_merge ) {
    FdNode * fn;
    int      idx;

    assert( fl != NULL );

    idx = getAEmptyFn( fl );
    if ( idx < 0 ) {
        return idx;
    }
    
    fn = fl->fds + idx;

    if ( ! is_merge ) {
        if ( initBuff( &(fn->bf), sizeof(AuthPacket), BUFF_MD_2FD ) ) {
            cleanFdNode( fn );
            return -2;
        }
        fn->flags = FDNODE_BUFF;
    }

    if ( ! is_merge ) { 
        fn->flags |= FDNODE_IS_TUNFD;
    }

    fn->fd = conn_fd;
    fn->flags = 0;
    fn->t = time( NULL );
    fn->auth_status = TUN_ACTIVE;
    fn->p = NULL;

    return idx;
}

/* 
 * #### Param
 *
 * == return ==
 * -66: 
 *  -1: fd list 已经满了
 * >=0: 添加成功
 */
int addMergeFd( FdList * fl, int fd ) {
    assert( fl != NULL );
    return _add_fd( fl, fd, 1 );
}

/* 
 * #### Param
 *
 * == return ==
 * -66: 
 *  -2: cannot get memory
 *  -1: fd list 已经满了
 * >=0:
 */
int addTunFd( FdList * fl, int fd ) {
    assert( fl != NULL );
    return _add_fd( fl, fd, 0 );
}


/* 
 * #### Param
 *
 * #### Return
 * -1: 未找到fd
 *  0: 删除成功
 *
 */
void delFd( FdList * fl, FdNode * fn ) {
    assert( fl != NULL );
    assert( fn != NULL );

    cleanFdNode( fn );
    fl->sz -= 1;
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
