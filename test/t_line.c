#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "../line.h"

int test1();
int test2();
int test3();

int main( int argc, char *argv) {
    test3();
    return 0;
}

int cmp(void * a, void * b ) {
    int * _a = (int *)a;
    int * _b = (int *)b;

    if ( *_a > *_b ) { 
	return 1;
    }
    else if ( *_a == *_b ) {
        return 0;
    }
    return -1;
}

void dumpNode( void * node ) {
    printf("%d\n", *((int *)node));
}

int test3() {
    int  i;
    int  datas[8];
    int  data;
    Line l;

    initLine( &l );
 
    srand( time( NULL ) );
    for ( i = 0; i < 8; i++ ) {
	data = random() % 10;
        printf("=> inLine( data ) data=%d\n", data);
        seqInLine( &l, (void *)&data, sizeof(data), cmp );
	datas[i] = data;
        printf("=> line len=%d\n", l.len);
    }
    printf("\n");
    for ( i = 0; i < 8; i++ ) {
        removeFromLine( &l, (void *)&datas[i], cmp );
        printf("=> removeFromLine( data ) datas[%d]=%d\n", i, datas[i]);
        printf("=> line len=%d\n", l.len);
	dumpLine( &l, dumpNode );
    }

    destroyLine( &l );

    return 0;
}

int test2() {
    int  i;
    int  j;
    int  data;
    Line l;

    initLine( &l );
 
    srand( time( NULL ) );
    for ( i = 0; i < 4; i++ ) {
	data = random() % 10;
        printf("=> inLine( data ) data=%d\n", data);
        seqInLine( &l, (void *)&data, sizeof(data), cmp );
    }

    for ( i = 0; i < 4; i++ ) {
        outLine( &l, (void *)&data );
        printf("=> outLine( data ) data=%d\n", data);
    }

    destroyLine( &l );

    return 0;
}

int test1() {
    Line l;
    int  data;
    int  i = 3;
    int  j;

    initLine( &l );

    printf(" : isLineEmpty = %s\n", isLineEmpty( &l ) ? "true" : "false" );
    printf(" : l.len=%d\n", l.len);

    printf("=> inLine( i ) i=%d\n", i);
    inLine( &l, (void *)&i, sizeof(i) );

    printf(" : isLineEmpty = %s\n", isLineEmpty( &l ) ? "true" : "false" );
    printf(" : l.len=%d\n", l.len);

    i = 0;
    outLine( &l, (void *)&i );
    printf("=> outLine( i ) i=%d\n", i);

    printf(" : isLineEmpty = %s\n", isLineEmpty( &l ) ? "true" : "false" );
    printf(" : l.len=%d\n", l.len);

    printf("\n");
    for ( i = 0; i < 3; i++ ) {
        printf(" : isLineEmpty = %s\n", isLineEmpty( &l ) ? "true" : "false" );
        printf(" : l.len=%d\n", l.len);

        printf("=> inLine( i ) i=%d\n", i);
        inLine( &l, (void *)&i, sizeof(i) );

        printf(" : isLineEmpty = %s\n", isLineEmpty( &l ) ? "true" : "false" );
        printf(" : l.len=%d\n", l.len);
    }

    printf("\n");
    for ( i = 0; i < 3; i++ ) {
        outLine( &l, (void *)&j );
        printf("=> outLine( j ) j=%d\n", j);

        printf(" : isLineEmpty = %s\n", isLineEmpty( &l ) ? "true" : "false" );
        printf(" : l.len=%d\n", l.len);
    }

    destroyLine( &l );

    return 0;
}
