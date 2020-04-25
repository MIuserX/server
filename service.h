#ifndef __CLIENT_H__
#define __CLIENT_H__

#include <sys/types.h>
#include <sys/socket.h>
#include "server.h"
#include "pipe.h"

typedef struct client_thread_arg {
    Pipe p;
}CTArg;

// auth methods
int authCli( FdNode *, PipeList *, int *, struct sockaddr_in, FdList * );
//int authToServer( Pipe *, int );

void * client_pthread( void * );

int set_fd_out_listen( Pipe * p, ForEpoll * ep, BOOL x );
int set_tun_out_listen( Pipe * p, ForEpoll * ep, BOOL x, BOOL setall);

int _relay_fd_to_tun( Pipe * p, int evt_fd, ForEpoll * ep, char rw, FdNode * fn );
int _relay_tun_to_fd( Pipe * p, int evt_fd, ForEpoll * ep, char rw );

int relay( Pipe * p, ForEpoll * ep, int i, FdNode * fn);

#endif
