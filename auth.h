#ifndef __AUTH_H__
#define __AUTH_H__

#include "common.h"

#define AUTH_NEW  (1) // c->s: create a pipe
#define AUTH_JOIN (2) // c->s: join a pipe
#define AUTH_OK   (3) // s->c: create or join a pipe OK
#define AUTH_NG   (4) // s->c: create or join a pipe FAILED
#define AUTH_BAD_CODE (5)  // s->c: 无法识别的code
#define AUTH_KEY_USED (6)  // s->c: 要create的key已存在
#define AUTH_NO_KEY   (7)  // s->c: 要join的key不存在 
#define AUTH_USR_FULL (8)  // s->c: too many users
#define AUTH_TUN_FULL (9)  // s->c: tunnel list is full
#define AUTH_SERV_ERR (10) // s->c: server internal error

#define AUTH_PL_FULL  (11) // s: pipe list is full
#define AUTH_FD_FULL  (12) // s: fd list is full

typedef struct auth_packet {
    unsigned int code;
    char         key[P_KEY_SZ];
} AuthPacket;

#endif
