#ifndef __LINE_H__
#define __LINE_H__

// == init ==
// data = NULL
// sz = 0
// prev = NULL
// next = NULL
typedef struct line_node {
    void             * data;
    size_t             sz;
    struct line_node * prev;
    struct line_node * next;
} LiNode;

// == init ==
// head = NULL
// len = 0
typedef struct line {
    LiNode * head;
    LiNode * tail;
    int      len;  // 队列长度
} Line;

void initLine( Line * l );
void destroyLine ( Line * l );

void dumpLine( Line * l, void (*dumpNode)(void * node) );

int isLineEmpty( Line * l );

/* 该函数将会申请一片内存,将 data 指向的数据
 * 复制到这片内存中.
 * 
 * == param ==
 * l : 待操作的队列指针
 * data : 要存储的数据的指针 
 */
int inLine( Line * l, void * data, size_t sz );

int seqInLine( Line * l, void * data, size_t sz, int (*cmp)(void * a, void * b) );
int removeFromLine( Line * l, void * data, int (*cmp)(void * a, void * b) );

/*
 */
int getLineHead( Line * l, void * data );
void * getHeadPtr( Line * l );
int getNode( Line * l, void ** data, int (*match)(void * data, void * target), void * target );

/* 该函数将会把队头元素存储的数据
 * 复制到 data 这片内存中,
 * 并释放队头元素存储数据的空间.
 * 
 * == param ==
 * l : 待操作的队列指针
 * data : 要存储的数据的指针 
 */
int outLine( Line * l, void * data );


#endif
