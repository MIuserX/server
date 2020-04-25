#ifndef __BUFFER_H__
#define __BUFFER_H__

#include "list.h"

#define BUFF_MD_ACK  ('1')
#define BUFF_MD_2FD  ('2')


typedef struct buff_block {
    unsigned int begin; // index of the first byte in buffer
    unsigned int sz;    // 
} BuffBlock;

// == init ==
// buff = NULL
// len = _sz
// mode = _mode
// sz = 0
// begin = 0
// end = 0
// active_sz = 0
// ack_begin = -1
//
typedef struct buffer {
    BuffBlock  * blks;
    char       * buff;     // 
    unsigned int len;      // the length of buff
    char         mode;

    size_t       sz;       // the number of bytes in buff
    int          begin;    // 活跃数据的第一个字节的下标
    int          end;      // 活跃数据的最后一个字节的下标
                          // 当head == tail时，看sz是否为0，
			  // 若为0，则head 和 tail都无效,
			  // 若不为0, 则head 和 tail 有效

    // 当 mode == BUFF_MD_ACK 时，
    // 以下5个参数才会被启用。
    size_t active_sz;     // is the length of [begin, end]
    int    ack_begin;     // is the index of first byte of data that need ack.
                          // its initial value is -1 .
			  // ack_begin is effective when more than -1 . 
    SendingList snd_list; //
} Buffer;

/* ==== BUFF_MD_ACK ====
 * In such mode, 
 *   (1) if sz == 0:
 *          begin is the first byte's index of white area, 
 *          end has no sense.
 *   (2) if sz > 0:
 *          begin is the first byte's index of data area,
 *          end is the first byte's index of data area.
 *   (3) if active_sz == 0:
 *          ack_begin is not effective.
 *   (4) if active_sz > 0:
 *          ack_begin is the first byte's index of .
 *
 * putBytesFromFd can:
 *   (1) increase value of sz
 *   (2) increase value of active_sz
 *   (3) set or move end
 *
 * preGetBytes can:
 *   (1) set ack_begin
 *   (2) reduce value of active_sz
 *   (3) move begin
 *
 * ackBytes can:
 *   (1) move or cancel ack_begin
 *   (2) reduce value of sz
 *
 * backTo can:
 *   (1) move begin
 *   (2) increase value of active_sz
 *   (3) cancel ack_begin
 *
 *
 * ==== BUFF_MD_2FD ====
 * In such mode,
 *   (1) active_sz, ack_begin, buff2segs
 *   (2) if sz == 0:
 *          begin is the first byte's index of white area, 
 *          end is not effective.
 *   (3) if sz > 0:
 *          begin is the first byte's index of data area,
 *          end is the first byte's index of data area.
 *
 * getBytes can:
 *   (1) reduce value of sz
 *   (2) move begin
 */


int initBuff( Buffer *, unsigned int, char );
void dumpBuff( Buffer * );
void cleanBuff( Buffer * );
void destroyBuff( Buffer * );

int isBuffEmpty( Buffer * );
int isBuffFull( Buffer * );

int putBytes( Buffer *, char *, size_t * );

// BUFF_MD_2FD
int getBytes( Buffer *, char *, size_t * );
int putBytesFromFd( Buffer *, int, size_t * );
int getBytesToFd( Buffer *, int );
void setBuffSize( Buffer *, size_t );
void discardBytes( Buffer *, size_t );

// BUFF_MD_ACK
int hasActiveData( Buffer * );
int putBytesFromBuff( Buffer *, Buffer *, size_t * );
int preGetBytesV2( Buffer *, char *, size_t *, int, unsigned int, unsigned int );
int delBytes( Buffer *, unsigned int );
void moveHead( Buffer *, unsigned int );
//int backTo( Buffer *, unsigned int );
//int cancelLastPreGet( Buffer * );

#endif
