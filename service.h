#ifndef __CLIENT_H__
#define __CLIENT_H__

#include <sys/types.h>
#include <sys/socket.h>
#include "server.h"
#include "pipe.h"

// auth methods
int doesNotAuthed( FdNode * );
int authCli( FdNode *, PipeList *, int *, struct sockaddr_in, FdList * );
int authToServer( Pipe * );

void * client_pthread( void * );

#endif
