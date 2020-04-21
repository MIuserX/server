#ifndef __SERVER_H__
#define __SERVER_H__

#include <netinet/in.h>            /* socket类定义需要*/
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>
#include <time.h>

#include "common.h"
#include "auth.h"
#include "pipe.h"

// server fd list max length
#define MAX_FDS (1024)

typedef struct fd_node {
    int          use; // if this node is use: 
                      //    0     -> not used
		      //    non-0 -> used
                      // only if use != 0, the following attrs are effort
    int          idx;

    int          fd;       // file descripter
    unsigned int flags; // FDNODE_IS_TUNFD
                        // FDNODE_BUFF
			// FD_IS_EPOLLIN
			// FD_IS_EPOLLOUT
			// FDNODE_AUTHED

    Buffer       bf;
    int          auth_status;

    time_t       t;       // the birth time of fd

    Pipe *       p;       // if has, is the pointer to asociated pipe
} FdNode;

void cleanFdNode( FdNode * );
int notAuthed( FdNode * );
int isMergeFd( FdNode * );
int isTunFd( FdNode * );

typedef struct fd_list {
    FdNode fds[MAX_FDS];
    int    sz;           // acount of used nodes
} FdList;

void initFdList( FdList * fl );
void destroyFdList( FdList * fl );

int isFdListEmpty( FdList * fl );
int isFdListFull( FdList * fl );

int addMergeFd( FdList * , int );
int addTunFd( FdList * , int );
void delFd( FdList * , FdNode * );

int cleanAuthTimeout( FdList * fl, int );
int typeOfFd( FdList * fl, int fd );
FdNode * searchByFd( FdList * fl, int fd );

int epoll_init( int fd, ForEpoll * ep );
void * service_thread(void * arg);

#endif
