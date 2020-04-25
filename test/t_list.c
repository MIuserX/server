#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

#include "../list.h"

SendingList snd_list;

// 测试isEmpty
// 测试isFull
// 测试addSeq
// 测试sending_cnt增长
void test1() {
    unsigned int i;

    initSndList( &snd_list );
    dumpSndList( &snd_list );
    printf("isEmpty: %s\n", isSndListEmpty( &snd_list ) ? "true" : "false" );
    printf("isFull: %s\n", isSndListFull( &snd_list ) ? "true" : "false" );

    addSeq( &snd_list, 1, 2, 3);
    dumpSndList( &snd_list );
    printf("isEmpty: %s\n", isSndListEmpty( &snd_list ) ? "true" : "false" );
    printf("isFull: %s\n", isSndListFull( &snd_list ) ? "true" : "false" );

    for ( i = 2; i <= 4; i++ ) {
        addSeq( &snd_list, i, 2, 3 );
        dumpSndList( &snd_list );
        printf("isEmpty: %s\n", isSndListEmpty( &snd_list ) ? "true" : "false" );
        printf("isFull: %s\n", isSndListFull( &snd_list ) ? "true" : "false" );
    }
}

// 测试删除head后面的元素，
//   isFull是否工作正常
//   sending_cnt 是否工作正常
//   delSeq 是否工作正常
void test2() {
    unsigned int i;

    initSndList( &snd_list );

    for ( i = 1; i <= 4; i++ ) {
        addSeq( &snd_list, i, 2, 3 );
    }
    dumpSndList( &snd_list );

    delSeq( &snd_list, 2 );

    dumpSndList( &snd_list );
    printf("isEmpty: %s\n", isSndListEmpty( &snd_list ) ? "true" : "false" );
    printf("isFull: %s\n", isSndListFull( &snd_list ) ? "true" : "false" );

    delSeq( &snd_list, 3 );
    dumpSndList( &snd_list );
    printf("isEmpty: %s\n", isSndListEmpty( &snd_list ) ? "true" : "false" );
    printf("isFull: %s\n", isSndListFull( &snd_list ) ? "true" : "false" );

    delSeq( &snd_list, 1 );
    dumpSndList( &snd_list );
    printf("isEmpty: %s\n", isSndListEmpty( &snd_list ) ? "true" : "false" );
    printf("isFull: %s\n", isSndListFull( &snd_list ) ? "true" : "false" );

    delSeq( &snd_list, 4 );
    dumpSndList( &snd_list );
    printf("isEmpty: %s\n", isSndListEmpty( &snd_list ) ? "true" : "false" );
    printf("isFull: %s\n", isSndListFull( &snd_list ) ? "true" : "false" );
}

int main(int argc, char *argv[]) {
    test2();
    return 0;
}
