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

#define FD_UNKNOWN ('0')
#define FD_MERGE   ('1')
#define FD_TUN     ('2')
#define FD_MERGE1  ('3')


typedef struct fd_node {
    int        use; // if this node is use: 
                    //    0     -> not used
		    //    non-0 -> used
                    // only if use != 0, the following attrs are effort

    int        fd;       // file descripter
    char       type;     // fd type
    unsigned int flags;    

    Buffer     bf;
    int        auth_status;
    int        auth_ok; // whether authticated 

    time_t     t;       // the birth time of fd

    Pipe * p;       // if has, is the pointer to asociated pipe
} FdNode;

typedef struct fd_list {
    FdNode fds[MAX_FDS];
    int    last;         // index of the last used node
    int    sz;           // acount of used nodes
} FdList;

void initFdList( FdList * fl );
void destroyFdList( FdList * fl );

int isFdListEmpty( FdList * fl );
int isFdListFull( FdList * fl );

int getAEmptyFn( FdList * );
int addFd( FdList * , FdNode * );
int delFd( FdList * , int fd );

int cleanAuthTimeout( FdList * fl, int );
int typeOfFd( FdList * fl, int fd );
FdNode * searchByFd( FdList * fl, int fd );

int epoll_init( int fd, ForEpoll * ep );
void * service_thread(void * arg);

#endif
