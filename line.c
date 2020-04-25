#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "line.h"

void initLine( Line * l ) {
    assert( l != NULL );

    bzero( (void *)l, sizeof(Line) );
}

void destroyLine( Line * l ) {
    assert( l != NULL );

    while ( ! isLineEmpty( l ) ) {
	justOutLine( l );
    }
}


/*
 * == return ==
 *  1: line is empty
 *  0: line is not empty
 */
int isLineEmpty( Line * l ) {
    assert( l != NULL );

    if ( l->len == 0 ) {
        return 1;
    }

    return 0;
}

void dumpLine( Line * l, void (*dumpNode)(void * node) ) {
    LiNode * tmp;
    int i = 1;

    assert( l != NULL );
    assert( dumpNode != NULL );

    printf("\ndumpLine:\n");
    if ( isLineEmpty( l ) ) {
         printf("=> line is empty\n");
    }
    else {
	 tmp = l->head;
	 while ( tmp ) {
             printf("=> node %d\n", i++);
	     dumpNode( tmp->data );
	     tmp = tmp->next;
	 }
    }
}

/* 
 * == description == 
 * 该函数：
 *   1) 申请两次内存
 *   2) memcpy 数据
 *
 * == return ==
 *  0: successfullly 
 * -1: failed to allocate memory
 */
int inLine( Line * l, void * data, size_t sz) {
    LiNode * ln;
   
    assert( l != NULL );
    assert( data != NULL );
   
    // allocate memory for node 
    ln =  ( LiNode * )malloc( sizeof(LiNode) ) ;
    if ( ! ln ) {
        return -1;
    }
    
    // clean allocated memory
    bzero( (void *)ln, sizeof(LiNode) );

    // allocate memory for data
    ln->data = malloc( sz );
    if ( ! ( ln->data ) ) {
	free( ln );
        return -1;
    }

    // copy data to node
    memcpy( ln->data, data, sz );
    ln->sz = sz;

    if ( isLineEmpty( l ) ) {
        l->head = ln;
	l->tail = ln;
	l->len = 1;
    } else {
	// ln 有了 prev
        ln->prev = l->tail;

	// l->tail 有了 next
	l->tail->next = ln;

	// ln 成了新的tail
	l->tail = ln;

	l->len += 1;
    }

    return 0;
}

int seqInLine( Line * l, void * data, size_t sz, int (*cmp)(void * a, void * b) ) {
    LiNode * ln;
    LiNode * tmp;

    assert( l != NULL );
    assert( data != NULL );
   
    // allocate memory for node 
    ln =  ( LiNode * )malloc( sizeof(LiNode) ) ;
    if ( ! ln ) {
        return -1;
    }
    
    // clean allocated memory
    bzero( (void *)ln, sizeof(LiNode) );

    // allocate memory for data
    ln->data = malloc( sz );
    if ( ! ( ln->data ) ) {
	free( ln );
        return -1;
    }

    // copy data to node
    memcpy( ln->data, data, sz );
    ln->sz = sz;

    if ( isLineEmpty( l ) ) {
        l->head = ln;
	l->tail = ln;
	l->len = 1;
    } else {
        tmp = l->head;
	while ( tmp ) {
	    //  1: data > tmp->data
	    //  0: data == tmp->data
	    // -1: data < tmp->data
	    if ( cmp( data, tmp->data ) == -1 ) { 
	        ln->next = tmp;
		ln->prev = tmp->prev;
		tmp->prev = ln;
		if ( ln->prev ) {
		    ln->prev->next = ln;
		}
		else {
		    // 只有队头元素没有prev
		    l->head = ln;
		}
	        l->len += 1;
		return 0;
	    }

	    tmp = tmp->next;
	}

	// ln 有了 prev
        ln->prev = l->tail;

	// l->tail 有了 next
	l->tail->next = ln;

	// ln 成了新的tail
	l->tail = ln;

	l->len += 1;
    }

    return 0;
}

/*
 *
 * == return ==
 *  0: removed
 * -1: line isn't empty, not found
 * -2: line is empty, not found
 */
int removeFromLine( Line * l, void * data, int (*cmp)(void * a, void * b) ) {
    LiNode * ln;
    LiNode * tmp;

    assert( l != NULL );
    assert( data != NULL );
    assert( cmp != NULL );

    if ( isLineEmpty( l ) ) {
	return -2;
    } else {
        tmp = l->head;
	while ( tmp ) {
	    //  1: data > tmp->data
	    //  0: data == tmp->data
	    // -1: data < tmp->data
	    if ( cmp( data, tmp->data ) == 0 ) { 
		if ( tmp->prev ) {
		    tmp->prev->next = tmp->next;
		}
		else {
		    // 只有队头元素没有prev
		    l->head = tmp->next;
		}
		if ( tmp->next ) {
		    tmp->next->prev = tmp->prev;
		}
		else {
		    // 只有队尾元素没有next
		    l->tail = tmp->prev;
		}
	        l->len -= 1;
		return 0;
	    }

	    tmp = tmp->next;
	}
    }

    return -1;
}

int getHeadDataSize( Line * l ) {
    assert( l != NULL );

    if ( isLineEmpty( l ) ) {
        return -1;
    }

    return l->head->sz;
}

/*
 * == return ==
 * -1 : line is empty
 *  0 : successfully
 */
int getLineHead( Line * l, void * data ) {
    assert( l != NULL );
    assert( data != NULL );

    if ( isLineEmpty( l ) ) {
        return -1;
    }

    memcpy( data, l->head->data, l->head->sz );

    return 0;
}

void * getHeadPtr( Line * l ) {
    assert( l != NULL );

    return l->head->data;
}

/*
unsigned int getMaxUnSendAck( Line * l ) {
    LiNode tmp;
    UnSendAck * cur;
    UnSendAck * prev;
    
    assert( l != NULL );

    if ( l->len == 1 ) {
        ((UnSendAck *)(l->head->data))->sending = 'y';
        return ((UnSendAck *)(l->head->data))->seq;
    }
    else {
        tmp = l->head->next;
        do {
	    prev = (UnSendAck *)(tmp->prev->data);
	    cur = (UnSendAck *)(tmp->data);
            if ( prev->seq + 1 == cur->seq ) {
		
                tmp = tmp->next;
	    }
        } while ( tmp );
	return cur->seq;
    }
}*/

int outLineMany( Line * l ) {
    return 0;
}

int justOutLine( Line * l ) {
    assert( l != NULL );

    if ( isLineEmpty( l ) ) {
        return -1;
    }

    free( l->head->data );

    if ( l->len == 1 ) {
	l->head = NULL;
	l->tail = NULL;
	l->len = 0;
    } else {
	l->head = l->head->next;
	free( l->head->prev );
	l->head->prev = NULL;
	l->len -= 1;
    }

    return 0;
}

/*
 * == return ==
 *  0: successfully
 * -1: line is empty
 */
int outLine( Line * l, void * data ) {
    assert( l != NULL );
    assert( data != NULL );

    if ( isLineEmpty( l ) ) {
        return -1;
    }

    memcpy( data, l->head->data, l->head->sz );
    free( l->head->data );

    if ( l->len == 1 ) {
	l->head = NULL;
	l->tail = NULL;
	l->len = 0;
    } else {
	l->head = l->head->next;
	free( l->head->prev );
	l->head->prev = NULL;
	l->len -= 1;
    }

    return 0;
}

/*
 *
 * == return ==
 *  0: found a match
 * -1: line isn't empty, not found a match
 * -2: line is empty, not found a match
 */
int getNode( Line * l, void ** data, int (*match)(void * data, void * target), void * target ) {
    LiNode * tmp;

    assert( l != NULL );
    assert( data != NULL );
    assert( match != NULL );

    if ( isLineEmpty( l ) ) {
	return -2;
    } else {
        tmp = l->head;
	while ( tmp ) {
	    //  1: data > tmp->data
	    //  0: data == tmp->data
	    // -1: data < tmp->data
	    if ( match( tmp->data, target ) ) { 
	        *data = tmp->data;
		return 0;
	    }

	    tmp = tmp->next;
	}
    }

    return -1;
}
