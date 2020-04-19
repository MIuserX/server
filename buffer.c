#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "buffer.h"

static Buff2Seg * createBuff2Seg( int b, int e ) {
    Buff2Seg * rt = ( Buff2Seg * )malloc( sizeof(Buff2Seg) );

    if ( rt ) {
        rt->begin = b;
        rt->end = e;
    }

    return rt;
}

static int endNext( Buffer * b ) {
    assert( b != NULL );

    return ( b->end + 1 ) % b->len;
}

static size_t lenOf( int begin, int end, size_t buff_len ) {
    // (1)
    // -------------------
    // | | |X|X|X|X|X|X| |
    // -------------------
    //      ^         ^
    //      begin     end
    //
    // (2) 
    // -------------------
    // |X|X|X| | | | |X|X|
    // -------------------
    //      ^         ^
    //      end       begin
    //
    // (3)
    //                begin
    //                v 
    // -------------------
    // | | | | | | | |X| |
    // -------------------
    //                ^
    //                end
    //

    // for case (2)
    if ( end < begin ) {
	// tail_len = buff_len - begin + 1
	// head_len = end + 1
        return buff_len - begin + end + 2;
    }

    // for cases (1) and (3)
    return end - begin + 1; 
}

static size_t tailWhiteLen( Buffer * b ) {
    assert( b );
    // tail white area.
    //
    // There are 2 caes:
    // (1) sz == 0
    //     begin is the first byte of white area,
    //     then tail_len = b->len - b->begin
    //
    // (2) sz > 0
    // (2.1) begin <= end
    //       then tail_len = b->len - b->end - 1
    // -------------------
    // | | |X|X|X|X|X|X| |
    // -------------------
    //      ^         ^
    //      begin     end
    //
    // (2.2) begin > end
    //       then tail_len has no sense.
    // -------------------
    // |X|X| | | | | |X|X|
    // -------------------
    //    ^           ^
    //    end         begin

    if ( b->sz == 0 ) {
        return b->len - b->begin;
    } else if ( b->begin <= b->end ) {
        return b->len - b->end - 1;
    }

    return 0;
}

static int nextWritePos( Buffer * b ) {
    assert( b != NULL );

    // tail white area.
    //
    // There are 3 cases:
    // (1) sz == b->len
    //     then, no white area
    // (2) sz == 0
    //     begin is the first byte of white area,
    //     next_pos = begin
    // (3) sz > 0 && sz < b->len
    //       next_pos = ( end + 1 ) % b->len
    
    if ( isBuffFull( b ) ) {
        return -1;
    }

    if ( isBuffEmpty( b ) ) {
        return b->begin;
    }

    // codes runs here, sz > 0 && sz < b->len

    return ( b->end + 1 ) % b->len;
}

int hasUnAckData( Buffer * b ) {
    assert( b != NULL ); 

    if ( isLineEmpty( &(b->buff2segs) ) ) {
        return 1;
    }
    return 0;
}

/*
 * == return ==
 * -1 : failed to allocate memory
 *  0 : successfully
 */
int initBuff( Buffer * b, size_t sz, char mode ) {
    assert( b != NULL );
    assert( sz > 0 );
    assert( mode > '0' || mode < '3' );

    bzero( ( void *)b, sizeof( Buffer ) );
    if ( ( b->buff = ( char *)malloc( sz ) ) == NULL ) {
        return -1;
    }

    b->len = sz;
    b->mode = mode;
    b->ack_begin = -1;

    initLine( &(b->buff2segs) );
    
    return 0;
}

void dumpBuff( Buffer * b ) {
    assert( b != NULL );
    
    printf("\nBuffer:\n");
    printf("  buff %s= NULL, len=%lu\n  mode=", b->buff == NULL ? "=" : "!", b->len);
    if ( b->mode == BUFF_MD_ACK ) {
        printf("ACK\n");
    } else if ( b->mode == BUFF_MD_2FD ) {
        printf("2FD\n");
    } else {
        printf("%d\n", (int)b->mode);
    }
    printf("  sz=%lu, begin=%d, end=%d\n", b->sz, b->begin, b->end);
    printf("  active_sz=%lu, ack_begin=%d\n", b->active_sz, b->ack_begin);
    printf("  lenof(buff2segs)=%d\n", b->buff2segs.len);
}

void cleanBuff( Buffer * b ) {
    assert( b != NULL );
     
    bzero( (void *)b->buff, b->len );
    destroyLine( &(b->buff2segs) );
    b->sz = 0;
    b->begin = 0;
    b->active_sz = 0;
    b->ack_begin = -1;
}

void setBuffSize( Buffer * b, size_t sz ) {
    assert( b != NULL );
    assert( b->mode == BUFF_MD_2FD );
    assert( b->buff != NULL );
    assert( b->len > 0 );
    assert( b->sz == 0 );

    b->sz = sz;
    b->begin = 0;
    b->end = sz - 1;
}

void destroyBuff( Buffer * b ) {
    assert( b != NULL );
    
    destroyLine( &(b->buff2segs) );
    free( b->buff );
    bzero( (void *)b, sizeof( Buffer ) );
}

int isBuffEmpty( Buffer * b ) {
    assert( b != NULL );

    return b->sz == 0;
}

int hasActiveData( Buffer * b ) {
    assert( b != NULL );

    return b->active_sz > 0;
}

/* 从buffer中将数据写到socket。
 * 该函数在 BUFF_MD_2FD 模式下运行。
 *
 * == description == 
 * 该函数内有循环，会持续发数据。
 * 正常情况下，该函数会返回socket block 或 send success
 *
 * == param ==
 * b :
 * buff : buffer to store returned bytes
 * sz : (in param) expected number of bytes
 *      (out param) the number of returned bytes
 *
 * == return ==
 *  1: sending data successfully, buffer is empty
 *  0: send func got EAGAIN or EWOULDBLOCK
 * -1: socket error
 * -2: buffer is empty, no data can be sent
 * -3: buffer is not empty, send func returned 0
 */
int getBytesToFd( Buffer * b, int fd ) {
    size_t  tail_len;
    ssize_t nwrite;
    
    assert( b != NULL );
    assert( b->mode == BUFF_MD_2FD );

    if ( isBuffEmpty( b ) ) {
         return -2;
    } else {
	// 这里要用数据，主要关心的是数据区是否跨越尾部，
	// 跨越了就需要两段处理，
	// 没有跨越则是一段处理。
	// (1)
	// -------------------
	// | | |X|X|X|X|X|X| |
	// -------------------
        //      ^         ^
	//      begin     end
	//
	// (2) 
	// -------------------
	// |X|X|X| | | | |X|X|
	// -------------------
        //      ^         ^
	//      end       begin
	//
	if ( b->begin <= b->end ) {
            // 这个if里处理上述的(1)
	    while ( ( ! isBuffEmpty( b ) ) && nwrite > 0 ) {
		nwrite = send( fd, (void *)(b->buff + b->begin), b->sz , 0 );
		if ( nwrite == 0 ) {
		    return -3;
		}
		else if ( nwrite == -1 ) {
	            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
	                return 0;
	            } else {
	                return -1;
	            }
	        } else {
	            b->sz -= nwrite;
	            b->begin = ( b->begin + nwrite ) % b->len;
	        }
	    }
	} else { // b->begin > b->end
            // 这个else里处理上述的(2)，

	    // 会有以下3种情况：
	    // (1) 把begin到尾部的数据读了一部分
	    // (2) 把begin到尾部的数据读完了
	    // (3) 把begin到尾部的数据读完了都不够，还要读前面的
            
	    while ( ( ! isBuffEmpty( b ) ) && nwrite > 0 ) {
		if ( b->begin > b->end ) {
		    tail_len = b->len - b->begin;
	            nwrite = send( fd, (void *)(b->buff + b->begin), tail_len , 0 );
	            if ( 0 == nwrite ) {
		        return -3;
		    }
		    else if ( -1 == nwrite ) {
	                if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
	                    return 0;
	                } else {
	                    return -1;
	                }
	            } else {
	                b->sz -= nwrite;
	                b->begin = ( b->begin + nwrite ) % b->len;
	            }
		} else {    
	            nwrite = send( fd, (void *)( b->buff ), b->sz , 0 );
	            if ( 0 == nwrite ) {
			return -3;
		    }
		    else if ( -1 == nwrite ) {
	                if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
	                    return 0;
	                } else {
	                    return -1;
	                }
	            } else if ( nwrite > 0 ) {
	                b->sz -= nwrite;
	                b->begin = ( b->begin + nwrite ) % b->len;
	            }
		}
	    }
	}
    }

    return 1;
}

/* 从buffer中返回数据。
 * 该函数在 BUFF_MD_NON 模式下运行。
 *
 * == param ==
 * b :
 * buff : buffer to store returned bytes
 * sz : (in param) expected number of bytes
 *      (out param) the number of returned bytes
 *
 * == return ==
 *  0: successfully
 * -1: buffer is empty
 */
int discardBytes( Buffer * b, size_t * sz ) {
    size_t tail_len;
    size_t out_sz;

    assert( b != NULL );
    assert( sz != NULL );
    assert( b->mode == BUFF_MD_2FD );

    if ( isBuffEmpty( b ) ) {
        return -1;
    } else {
	if ( ( *sz ) == 0 ) {
	    *sz = b->sz;
        } else { // ( *sz ) > 0
            *sz = ( *sz ) <= b->sz ? ( *sz ) : b->sz;
	}

        b->begin = ( b->begin + ( *sz ) ) % b->len;
        b->sz -= ( *sz );
    }

    return 0;
}

/* 从buffer中返回数据。
 * 该函数在 BUFF_MD_NON 模式下运行。
 *
 * == param ==
 * b :
 * buff : buffer to store returned bytes
 * sz : (in param) expected number of bytes
 *      (out param) the number of returned bytes
 *
 * == return ==
 *  0 : successfully
 * -1 : this function cannot be applied for the buffer mode
 * -2 : buffer is empty
 */
int getBytes( Buffer * b, char * buff, size_t * sz ) {
    size_t tail_len;
    size_t out_sz;

    assert( b != NULL );
    assert( buff != NULL );
    assert( sz != NULL );

    if ( b->mode != BUFF_MD_2FD ) {
         return -1;
    } else if ( isBuffEmpty( b ) ) {
         return -2;
    } else {
	// not empty, so:
	//   (1) b->sz > 0
	//   (2) values of b->begin and b->end are valid 
	if ( ( *sz ) == 0 ) {
	    *sz = b->sz;
        } else { // ( *sz ) > 0
            *sz = ( *sz ) <= b->sz ? ( *sz ) : b->sz;
	}

	// (1)
	// -------------------
	// | | |X|X|X|X|X|X| |
	// -------------------
        //      ^         ^
	//      begin     end
	//
	// (2) 
	// -------------------
	// |X|X|X| | | | |X|X|
	// -------------------
        //      ^         ^
	//      begin     end
	//
	// (3)
	//                begin
	//                v 
	// -------------------
	// | | | | | | | |X| |
	// -------------------
        //                ^
	//                end
	//
	if ( b->begin <= b->end ) {
            // 这个if里处理上述的(1)和(3)
	    memcpy( (void *)buff, b->buff + b->begin, *sz );
	} else { // b->begin > b->end
            // 这个else里处理上述的(2)，

	    // 会有以下3种情况：
	    // (1) 把begin到尾部的数据读了一部分
	    // (2) 把begin到尾部的数据读完了
	    // (3) 把begin到尾部的数据读完了都不够，还要读前面的
            tail_len = b->len - b->begin;

	    if ( ( *sz ) <= tail_len  ) {
	        memcpy( (void *)buff, b->buff + b->begin, ( *sz ) );
	    } else { // ( *sz ) > tail_len
	        memcpy( (void *)buff, b->buff + b->begin, tail_len );
		memcpy( (void *)(buff + tail_len), b->buff, ( *sz ) - tail_len );
	    }
	}

        b->begin = ( b->begin + ( *sz ) ) % b->len;
	b->sz -= ( *sz );
    }

    return 0;
}

/* 从buffer中返回数据，返回的数据并不
 *
 *
 * == param ==
 * b :
 * buff : buffer to store returned bytes
 * sz : (in param) expected number of bytes
 *      (out param) the number of returned bytes
 *
 * == return ==
 *  0 : successfully
 * -2 : buffer is empty
 * -3 : inLine failed
 */
int preGetBytes( Buffer * b, char * buff, size_t * sz, unsigned int seq ) {
    int      rt;
    int      tail_len;
    Buff2Seg buff2seg;

    assert( b != NULL );
    assert( buff != NULL );
    assert( sz != NULL );
    assert( b->mode == BUFF_MD_ACK );

    if ( isBuffEmpty( b ) ) {
	// buffer is empty, do nothing
        return -2;
    } else {
	// not empty, so:
	//   (1) b->sz > 0
	//   (2) values of b->begin and b->end are valid
	if ( ( *sz ) == 0 ) {
	    *sz = b->active_sz;
        } else { // ( *sz ) > 0
            *sz = ( *sz ) <= b->active_sz ? ( *sz ) : b->active_sz;
	}

	// (1)
	// -------------------
	// | | |X|X|X|X|X|X| |
	// -------------------
        //      ^         ^
	//      begin     end
	//
	// (2) 
	// -------------------
	// |X|X|X| | | | |X|X|
	// -------------------
        //      ^         ^
	//      begin     end
	//
	// (3)
	//                begin
	//                v 
	// -------------------
	// | | | | | | | |X| |
	// -------------------
        //                ^
	//                end
	//
	if ( b->begin <= b->end ) {
            // 这个if里处理上述的(1)和(3)
	    memcpy( (void *)buff, b->buff + b->begin, *sz );
	} else { // b->begin > b->end
            // 这个else里处理上述的(2)，

	    // 会有以下3种情况：
	    // (1) 把begin到尾部的数据读了一部分
	    // (2) 把begin到尾部的数据读完了
	    // (3) 把begin到尾部的数据读完了都不够，还要读前面的
            tail_len = b->len - b->begin;

	    if ( ( *sz ) <= tail_len  ) {
	        memcpy( (void *)buff, b->buff + b->begin, ( *sz ) );
	    } else { // ( *sz ) > tail_len
	        memcpy( (void *)buff, b->buff + b->begin, tail_len );
		memcpy( (void *)(buff + tail_len), b->buff, ( *sz ) - tail_len );
	    }
	}

	// no data needed ack => has data needed ack
	if ( b->ack_begin == -1 ) {
            b->ack_begin = b->begin;
	}
        
        // 加入ack队列
	buff2seg.begin = b->begin;
	buff2seg.end = ( b->begin + ( *sz ) - 1 ) % b->len;
        buff2seg.seq = seq;
        if ( rt = inLine( &(b->buff2segs), (void *)(&buff2seg) , sizeof(Buff2Seg) ) != 0 ) {
	    dprintf(2, "Error(%s:%d): inLine failed, rt=%d\n", __FILE__, __LINE__, rt);
	    return -3;
	}

	// 调整begin和active_sz，新的数据取走了(被发送了)，
	// active data 的起始标识begin需要向后移动，
	// active_sz 也减少了。
        b->begin = ( b->begin + ( *sz ) ) % b->len;

	if ( b->mode == BUFF_MD_ACK ) {
	    b->active_sz -= ( *sz );
	}
    }

    return 0;
}

int isBuffFull( Buffer * b ) {
    assert( b );

    if ( b->len == b->sz ) {
        return 1;
    }
    return 0;
}

static void memCopy( Buffer * b, int d, char * src, size_t sz ) {
    assert( b != NULL );

    memcpy( (void *)( b->buff + d ), src, sz );
    b->sz += sz;
    b->active_sz += ( b->mode == BUFF_MD_ACK ? sz : 0 );
    b->end = ( b->begin + b->sz - 1 ) % b->len;
}

/* 从给定到空间中向buffer中填充数据，
 * 该函数会尽量把buffer填满．
 *
 * == return ==
 *  0: successfully
 * -1: failed, buffer is full
 */
int putBytes( Buffer * b, char * src, size_t * sz ) {
    size_t copy_sz;
    size_t store_sz = 0;

    assert( b != NULL );
    assert( src != NULL );
    assert( sz != NULL );

    /* 这函数就是往buffer中复制数据，就关心两个点：
     * (1) 有没有空白区
     * (2) 空白区有一段还是两段
     *
     * 第(2)点是下面代码核心。
     */

    if ( ! isBuffFull( b ) ) {
        if ( isBuffEmpty( b ) ) {
            /* buffer为空，那么就一个空白区，
	     * 起始点就在第一个字节，
	     * 这里无需关心循环。
	     */
	    copy_sz = b->len <= ( *sz ) ? b->len : ( *sz ) ;
	    b->begin = 0;
	    memCopy( b, 0, src, copy_sz );

            *sz = copy_sz;
	} else { // sz > 0 && sz < b->len
            /*
	     * 如果buffer不空，有以下几种情况：
	     */
            // 
	    // (1)
	    // +-------------+
            // | | | |X|X| | |
            // +-------------+
            //        | |
            //        | end
            //        begin
	    //
	    // (2) b->begin == 0
	    // +-------------+
            // |X|X|X|X|X| | |
            // +-------------+
            //  |       |
            //  |       end
            //  begin
            //
            // (3) b->end == b->len - 1
            // +-------------+
            // | | | |X|X|X|X|
            // +-------------+
            //        |     |
            //        |     end
            //        begin
            // (4) b->end < b->bein
            // +-------------+
            // |X| | | |X|X|X|
            // +-------------+
            //  |       |    
            //  end     |    
            //          begin
           
	    if ( ( b->begin == 0 ) || ( b->end == b->len - 1 ) || ( b->end < b->begin ) ) {
	        copy_sz = ( b->len - b->sz ) <= ( *sz ) ? ( b->len - b->sz ) : ( *sz ) ;
	        memCopy( b, endNext( b ), src, copy_sz );

                *sz = copy_sz;
	    } else {
		// 写尾部空白区
	        copy_sz = ( b->len - b->end - 1 ) <= ( *sz ) ? ( b->len - b->end - 1 ) : ( *sz ) ;
	        memCopy( b, endNext( b ), src, copy_sz );
	        store_sz += copy_sz;
	        
                *sz -= copy_sz;
		if ( ( *sz ) > 0 ) {
		    // 写头部空白区
	            copy_sz = ( b->len - b->sz ) <= ( *sz ) ? ( b->len - b->sz ) : ( *sz ) ;
	            memCopy( b, endNext( b ), src + store_sz, copy_sz );
	            store_sz += copy_sz;
		}

                *sz = store_sz;
	    }
        }
    } else { // buffer is empty
        return -1;
    }

    return 0;
}

/* 从给定到空间中向buffer中填充数据，
 * 该函数会尽量把buffer填满．
 *
 * == return ==
 *  0: successfully
 * -1: failed, dest buff is full
 * -2: failed, src buff is empty
 */
int putBytesFromBuff( Buffer * b, Buffer * src, size_t * sz ) {
    size_t copy_sz;
    size_t store_sz = 0;
    int    rt;

    assert( b != NULL );
    assert( src != NULL );
    assert( sz != NULL );

    /* 这函数就是往buffer中复制数据，就关心两个点：
     * (1) 有没有空白区
     * (2) 空白区有一段还是两段
     *
     * 第(2)点是下面代码核心。
     */

    if ( isBuffFull( b ) ) {
        return -1;
    }
    if ( isBuffEmpty( src ) ) {
        return -2;
    }

    if ( (*sz) == 0 ) {
        (*sz) = src->sz;
    } else {
        (*sz) = (*sz) > src->sz ? src->sz : (*sz);
    }

    if ( isBuffEmpty( b ) ) {
        /* buffer为空，那么就一个空白区，
         * 起始点就在第一个字节，
         * 这里无需关心循环。
         */
        copy_sz = b->len <= ( *sz ) ? b->len : ( *sz ) ;
        getBytes( src, b->buff, &copy_sz );
	b->sz = copy_sz;
        b->begin = 0;
        b->end = copy_sz - 1;
        *sz = copy_sz;
    } else { // sz > 0 && sz < b->len
        /*
         * 如果buffer不空，有以下几种情况：
         */
        // 
        // (1)
        // +-------------+
        // | | | |X|X| | |
        // +-------------+
        //        | |
        //        | end
        //        begin
        //
        // (2) b->begin == 0
        // +-------------+
        // |X|X|X|X|X| | |
        // +-------------+
        //  |       |
        //  |       end
        //  begin
        //
        // (3) b->end == b->len - 1
        // +-------------+
        // | | | |X|X|X|X|
        // +-------------+
        //        |     |
        //        |     end
        //        begin
        // (4) b->end < b->bein
        // +-------------+
        // |X| | | |X|X|X|
        // +-------------+
        //  |       |    
        //  end     |    
        //          begin
       
        if ( ( b->begin == 0 ) || ( b->end == b->len - 1 ) || ( b->end < b->begin ) ) {
            copy_sz = ( b->len - b->sz ) <= ( *sz ) ? ( b->len - b->sz ) : ( *sz ) ;
            getBytes( src, b->buff + endNext( b ), &copy_sz );
	    b->sz += copy_sz;
            b->end = ( b->end + copy_sz ) % b->len;
            *sz = copy_sz;
        } else {
    	    // 写尾部空白区
            copy_sz = ( b->len - b->end - 1 ) <= ( *sz ) ? ( b->len - b->end - 1 ) : ( *sz ) ;
            getBytes( src, b->buff + endNext( b ), &copy_sz );
	    b->sz += copy_sz;
            b->end = ( b->end + copy_sz ) % b->len;
            store_sz += copy_sz;
            
            *sz -= copy_sz;
    	    if ( ( *sz ) > 0 ) {
    	        // 写头部空白区
                copy_sz = ( b->len - b->sz ) <= ( *sz ) ? ( b->len - b->sz ) : ( *sz ) ;
                getBytes( src, b->buff + endNext( b ), &copy_sz );
	        b->sz += copy_sz;
                b->end = ( b->end + copy_sz ) % b->len;
                store_sz += copy_sz;
    	    }

            *sz = store_sz;
        }
    }

    return 0;
}

/*
 * == param ==
 * buff: [in] 待写空间的地址
 * sz: [in] 需要写多少
 *     [out] 实际写了多少
 * fd: [in] 待读取的 fd
 *
 * == return ==
 *  1: buff is full
 *  0: socket block
 * -1: socket error
 * -2: socket was closed
 */
static int _readFd( char * buff, size_t * sz, int fd ) {
    ssize_t nread;
    size_t want_sz;

    assert( buff != NULL );
    assert( sz != NULL );
    assert( *sz > 0 );

    want_sz = ( *sz );
    *sz = 0;
    while ( want_sz > 0 ) { 
        nread = recv( fd, (void *)( buff + (*sz) ), want_sz, 0 );
        //printf("_readFd: *sz=%lu nread=%ld\n", *sz, nread);
	if ( nread == 0 ) {
            return -2;
        }
        if ( nread == -1 ) { 
            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
                return 0;
            } else {
                return -1;
            }
        }

	want_sz -= nread;
        *sz += nread;
    }

    return 1;
}

/* 从fd中读数据到buffer中。
 *
 * == description ==
 * 假设fd中的待读bytes数是：m
 * buffer剩余bytes空间是：n
 * 那么有如下3种关系：
 *   (1) m > n
 *   (2) m == n
 *   (3) m < n
 *
 * 如果不发生socket error或socket closed，
 * 对于(1)和(2)，该函数会返回1；
 * 对于(3)，该函数会返回0；
 *
 * == return ==
 *  1: read successfully, buffer is full now
 *  0: read successfully, socket block
 * -1: socket error
 * -2: socket was closed
 * -3: buffer is full, cannot be put bytes to
 */
int putBytesFromFd( Buffer * b, int fd, size_t * sz ) {
    size_t want_sz;
    size_t len;
    int    rt;

    assert( b != NULL );
    assert( sz != NULL );

    /* 这函数就是往buffer中复制数据，就关心两个点：
     * (1) 有没有空白区
     * (2) 空白区有一段还是两段
     *
     * 第(2)点是下面代码核心。
     */

    if ( *sz == 0 ) {
        want_sz = b->len - b->sz;
    } else {
        want_sz = ( b->len - b->sz ) < (*sz) ? ( b->len - b->sz ) : (*sz);
        *sz = 0;
    }

    if ( ! isBuffFull( b ) ) {
        if ( isBuffEmpty( b ) ) {
            /* buffer为空，那么就一个空白区，
	     * 起始点就在第一个字节，
	     * 这里无需关心循环。
	     */
	    b->begin = 0;
            len = want_sz;
            rt = _readFd( b->buff, &len, fd );	    
            if ( rt >= 0 ) {
	        b->sz += len;
	        if ( b->mode == BUFF_MD_ACK ) {
	            b->active_sz += len;
	        }
	        b->end = ( b->begin + len - 1 ) % b->len;

		*sz = len;
	    }
	    return rt;
	} else { // sz > 0 && sz < b->len
            /*
	     * 如果buffer不空，有以下几种情况：
	     */
            // 
	    // (1)
	    // +-------------+
            // | | | |X|X| | |
            // +-------------+
            //        | |
            //        | end
            //        begin
	    //
	    // (2) b->begin == 0
	    // +-------------+
            // |X|X|X|X|X| | |
            // +-------------+
            //  |       |
            //  |       end
            //  begin
            //
            // (3) b->end == b->len - 1
            // +-------------+
            // | | | |X|X|X|X|
            // +-------------+
            //        |     |
            //        |     end
            //        begin
	    //
            // (4) b->end < b->bein
            // +-------------+
            // |X| | | |X|X|X|
            // +-------------+
            //  |       |    
            //  end     |    
            //          begin
           
	    if ( b->begin <= b->end && b->begin > 0 && b->end < b->len - 1 ) { // for (1)
		// 写尾部空白区
                len = ( b->len - b->end - 1 ) < want_sz ? ( b->len - b->end - 1 ) : want_sz ;
                rt = _readFd( b->buff + endNext( b ), &len, fd );	    
                if ( rt >= 0 ) {
	            b->sz += len;
	            if ( b->mode == BUFF_MD_ACK ) {
	                b->active_sz += len;
	            }
		    // 数据区非空,end值有效
	            b->end = ( b->end + len ) % b->len;

		    *sz += len;
		    want_sz -= len;
	        }
		if ( rt != 1 ) {
	            return rt;
		}
	        
		// 写头部空白区
		if ( want_sz > 0 ) {
                    len = want_sz;
                    rt = _readFd( b->buff + endNext( b ), &len, fd );	    
                    if ( rt >= 0 ) {
	                b->sz += len;
	                if ( b->mode == BUFF_MD_ACK ) {
	                    b->active_sz += len;
	                }
		        // 数据区非空,end值有效
	                b->end = ( b->end + len ) % b->len;

			*sz += len;
		        want_sz -= len;
	            }
	            return rt;
		}
	    }
	    else { // for (2), (3), (4)
                len = want_sz;
                rt = _readFd( b->buff + endNext( b ), &len, fd );	    
                if ( rt >= 0 ) {
	            b->sz += len;
	            if ( b->mode == BUFF_MD_ACK ) {
	                b->active_sz += len;
	            }
		    // 数据区非空,end值有效
	            b->end = ( b->end + len ) % b->len;
	        
		    *sz = len;
		}
	        return rt;
	    }
        }
    }

    // buffer is full
    return -3;
}

/* 从 fd 向buffer中填充数据，
 * 该函数会尽量 fd 读完，尽量把buffer填满．
 * 
 *
 *
 * == return ==
 *  1: every nread > 0, buffer is not full
 *  0: socket nodata: nread == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)
 * -1: socket error:  nread == -1 && errno != EAGAIN && errno != EWOULDBLOCK
 * -2: socket closed: nread == 0
 * -3: buffer is full, cannot be put bytes
 * -4: buffer is full, it's unknown if fd has unread data
 */
int putBytesFromFd_ver1( Buffer * b, int fd ) {
    ssize_t nread = 0;
    size_t  tail_len = 0;
    int     next_pos;

    assert( b != NULL );

    if ( ! isBuffFull( b ) ) {
	// We can put bytes to buffer when buffer is not full.
	// When we want put bytes to buffer, we should know where to write.
	// There are 3 cases:
	//
	// (1) sz = 0
	//     then the begin is the first byte of white area
	// (2) sz > 0 && sz < b->len
	//     (2.1) begin <= end
	//           then 
	//     (2.2) begin > end
	//           then 
	//
	// for (1):
	//   a. get tail white len: buffer->len - begin (len must be more than 0)
	//   b. write to tail white area
	//   c. if tail area is not enough, write to head white area
	// 
	// for (2.1):
	//   a. get tail white len: buffer->len - end - 1 (len may be 0)
	//   b. write to tail white area
	//   c. if tail area is not enough, write to head white area
	//
	// for (2.2):
	//   a. evalute next : ( end + 1 ) % buffer->len
	//   b. write

        if ( isBuffEmpty( b ) ) { // sz = 0
            // ==== write to tail white area
	    tail_len = tailWhiteLen( b );
	    next_pos = nextWritePos( b );
	    nread = recv( fd, (void *)( b->buff + next_pos ), tail_len, 0 );
	    if ( nread == 0 ) {
	        printf("Warning: putBytesFromFd nread=0, peer closed, %s\n", strerror(errno));
	        return -2;
	    }
	    if ( nread == -1 ) { 
		if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
	            return 0;
		} else {
		    dprintf(2, "Error: putBytesFromFd nread=-1, errno=%d, %s\n", errno, strerror(errno));
	            return -1;
		}
	    }

	    b->sz += nread;
	    if ( b->mode == BUFF_MD_ACK ) {
	        b->active_sz += nread;
	    }
	    b->end = next_pos + nread - 1;

	    // ==== write to head white area
	    // We don't know how many bytes can be read from fd.
	    // So if previous read filled the tail white area, 
	    // we try to read again.
	    if ( ( ! isBuffFull( b ) ) && ( nread == tail_len ) ) {
		// now, the buffer's state likes this:
		// +-------------+
		// | | | | |X|X|X|
		// +-------------+
		//          |   |
		//          |   end
		//          begin
		//
	        nread = recv( fd, (void *)(b->buff), b->len - b->sz, 0 );
	        if ( nread == 0 ) {
	            printf("Warning: putBytesFromFd nread=0, peer closed, %s\n", strerror(errno));
	            return -2;
	        }
	        if ( nread == -1 ) { 
	            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
	                return 0;
	            } else {
	                dprintf(2, "Error: putBytesFromFd nread=-1, errno=%d, %s\n", errno, strerror(errno));
	                return -1;
	            }
	        }

	        b->sz += nread;
	        if ( b->mode == BUFF_MD_ACK ) {
	            b->active_sz += nread;
	        }
	        b->end = ( b->end + nread ) % b->len;
		// here, the buffer's state is one of the both:
		//
		// (1) exists white area
		// +-------------+
		// |X|X| | |X|X|X|
		// +-------------+
		//    |     |   
		//    end   |   
		//          begin
		// 
		// (2) no white area
		// +-------------+
		// |X|X|X|X|X|X|X|
		// +-------------+
		//        | |   
		//        | begin  
		//        end
		//
		// whichever, we should do nothing.
		// for (1), fd has no bytes can be read.
		// for (2), fd 
	    }
	} else if ( b->begin <= b->end ) { // sz > 0 && sz < b->len && begin <= end
            // now, the buffer's state like this:
            // 
	    // (1)
	    // +-------------+
            // | | | |X|X| | |
            // +-------------+
            //        | |
            //        | end
            //        begin
	    //
	    // (2)
	    // +-------------+
            // |X|X|X|X|X| | |
            // +-------------+
            //  |       |
            //  |       end
            //  begin
            //
            // (3)
            // +-------------+
            // | | | |X|X|X|X|
            // +-------------+
            //        |     |
            //        |     end
            //        begin

            if ( b->end < b->len - 1 ) {
		// for case (1), (2)
	        tail_len = tailWhiteLen( b );
	        nread = recv( fd, (void *)( b->buff + nextWritePos( b ) ), tail_len, 0 );
	        if ( nread == 0 ) {
	            printf("Warning: putBytesFromFd nread=0, peer closed, %s\n", strerror(errno));
	            return -2;
	        }
	        if ( nread == -1 ) { 
	            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
	                return 0;
	            } else {
	                dprintf(2, "Error: putBytesFromFd nread=-1, errno=%d, %s\n", errno, strerror(errno));
	                return -1;
	            }
	        }
	    } else {
		// for case (3)
	        nread = recv( fd, (void *)( b->buff + nextWritePos( b ) ), b->len - b->sz, 0 );
	        if ( nread == 0 ) {
	            printf("Warning: putBytesFromFd nread=0, peer closed, %s\n", strerror(errno));
	            return -2;
	        }
	        if ( nread == -1 ) { 
	            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
	                return 0;
	            } else {
	                dprintf(2, "Error: putBytesFromFd nread=-1, errno=%d, %s\n", errno, strerror(errno));
	                return -1;
	            }
	        }
	    }

	    b->sz += nread;
	    if ( b->mode == BUFF_MD_ACK ) {
	        b->active_sz += nread;
	    }
	    b->end = ( b->end + nread ) % b->len;

	    // We don't know how many bytes can be read from fd.
	    // So if previous read filled the tail white area, 
	    // we try to read again.
	    if ( ( ! isBuffFull( b ) ) && ( tail_len > 0 && nread == tail_len ) ) {
		// now, the buffer's state like this:
		// +-------------+
		// | | | |X|X|X|X|
		// +-------------+
		//        |     |
		//        |     end
		//        begin
		//
	        nread = recv( fd, (void *)( b->buff + nextWritePos ( b ) ), b->len - b->sz, 0 );
	        if ( nread == 0 ) {
	            printf("Warning: putBytesFromFd nread=0, peer closed, %s\n", strerror(errno));
	            return -2;
	        }
	        if ( nread == -1 ) { 
	            if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
	                return 0;
	            } else {
	                dprintf(2, "Error: putBytesFromFd nread=-1, errno=%d, %s\n", errno, strerror(errno));
	                return -1;
	            }
	        }

	        b->sz += nread;
	        if ( b->mode == BUFF_MD_ACK ) {
	            b->active_sz += nread;
	        }
	        b->end += nread;
	    }
	} else { // sz > 0 && sz < b->len && begin > end
            // now, the buffer's state like this:
            // +-------------+
            // |X|X| | |X|X|X|
            // +-------------+
            //    |     |   
            //    end   |  
            //          begin
            //

	    nread = recv( fd, (void *)( b->buff + nextWritePos( b ) ), b->len - b->sz, 0 );
	    if ( nread == 0 ) {
	        printf("Warning: putBytesFromFd nread=0, peer closed, %s\n", strerror(errno));
	        return -2;
	    }
	    if ( nread == -1 ) { 
	        if ( errno == EAGAIN || errno == EWOULDBLOCK ) {
	            return 0;
	        } else {
	            dprintf(2, "Error: putBytesFromFd nread=-1, errno=%d, %s\n", errno, strerror(errno));
	            return -1;
	        }
	    }

	    b->sz += nread;
	    if ( b->mode == BUFF_MD_ACK ) {
	        b->active_sz += nread;
	    }
	    b->end += nread;
        }
    } else {
        return -3;
    }

    if ( isBuffFull( b ) ) {
        return -4;
    }

    return 1;
}

/*
 * == return ==
 *  0 : successfully
 * -2 : there are not unacknowdged bytes in buffer
 * -4 : the seq of line head is not equal to ack_seq
 */
int ackBytes( Buffer * b, unsigned int ack_seq ) {
    Buff2Seg buff2seg;
    int      rt;
    
    assert( b != NULL );
    assert( b->mode == BUFF_MD_ACK );

    if ( b->ack_begin == -1 ) {
        return -2;
    }

    // empty line is the only reason which make getLineHead failed,
    // now the line isn't empty, so this calling will be successful.
    getLineHead( &(b->buff2segs) , (void *)(&buff2seg) );

    // in expected situation, ack_seq should equal to seq of line heae, 
    // if not, there must be something wrong.
    if ( buff2seg.seq != ack_seq ) {
        return -4;
    }

    // b->sz is length of [ack_begin, end].
    // when buffer is in ACK mode, 
    // ack func is the only one who can reduce value of buffer->sz, 
    // putBytes func is the only one who can increase value of buffer.sz.
    b->sz -= lenOf( buff2seg.begin, buff2seg.end, b->len );
    
    // there are 2 cases:
    //   (1) has data need be ack
    //       => move ack_begin
    //   (2) no data need be ack
    //       => ack_begin = -1
    if ( b->ack_begin != -1 ) {
        b->ack_begin = ( buff2seg.end + 1 ) % b->len;
    }
    
    // line is not empty, so error will not occur
    outLine( &(b->buff2segs), (void *)(&buff2seg) );

    if ( isLineEmpty( &(b->buff2segs) ) ) {
        b->ack_begin = -1;
    }

    return 0;
}

/* this func undo preGetBytes's actions.
 *   (1) move begin
 *   (2) increase value of active_sz
 *   (3) cancel ack_begin
 *
 * == return ==
 *   0: successfully
 *  -1: no unack data
 */
int cancelLastPreGet( Buffer * b ) {
    Buff2Seg buff2seg;

    assert( b != NULL );
    assert( b->mode == BUFF_MD_ACK );

    if ( ! hasUnAckData( b ) ) {
        return -1;
    }

    outLine( &(b->buff2segs), &buff2seg );
    b->begin = buff2seg.begin;
    b->active_sz += lenOf( buff2seg.begin, buff2seg.end, b->len );

    if ( b->ack_begin == b->begin ) {
        b->ack_begin = -1;
    }

    return 0;
}

/* this func undo preGetBytes's actions.
 *   (1) move begin
 *   (2) increase value of active_sz
 *   (3) cancel ack_begin
 *
 * == return ==
 *   0: successfully
 *  -1: no unack data
 *  -2: error, line is empty and seq not found
 */
int backTo( Buffer * b, unsigned int seq ) {
    Buff2Seg buff2seg;

    assert( b != NULL );
    assert( b->mode == BUFF_MD_ACK );
    
    if ( ! hasUnAckData( b ) ) {
        return -1;
    }

    do {
        outLine( &(b->buff2segs), &buff2seg );
	b->begin = buff2seg.begin;
        b->active_sz += lenOf( buff2seg.begin, buff2seg.end, b->len );

	if ( b->ack_begin == b->begin ) {
	    b->ack_begin = -1;
	}
    } while ( buff2seg.seq != seq && hasUnAckData( b ) );

    if ( buff2seg.seq != seq ) {
        return -2;
    }

    return 0;
}

