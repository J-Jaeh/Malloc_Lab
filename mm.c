/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "6team",
    /* First member's full name */
    "JJGH",
    /* First member's email address */
    "1wo2gur@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

//-- Basic constants and macros  --//
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))
#define WSIZE 4 /*워드 크기*/

#define DSIZE 8 /*더블 워드 크기*/

#define CHUNKSIZE (1 << 6) /*초기 가용블럭과 힙확장을 위한 기본크기 1을 비트쉬프트로 12번이동 ~ =2^12승 = 4096=4KB*/

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc)) /* 크기와 할당 비트를 통합해서 헤더와 풋터에 저장할 수 있는 값리턴 (p816 참고)*/

#define GET(p) (*(unsigned int *)(p))              /* p가 가리키는 워드를 읽어서 리턴*/
#define PUT(p, val) (*(unsigned int *)(p) = (val)) /* 워드에 val 저장*/

#define GET_SIZE(p) (GET(p) & ~0x7)  /* 헤더 또는 풋터의 사이즈 리턴*/
#define GET_ALLOC(p) (GET(p) & 0x01) /* 헤더 또는 풋터의 할당비트 리턴 */

#define GET_HEAD_POINTER(bp) ((char *)(bp)-WSIZE)
#define GET_FOOT_POINTER(bp) ((char *)(bp) + GET_SIZE(GET_HEAD_POINTER(bp)) - DSIZE)

//
static char *heap_listp;
static char *init_successor;
// static char *init;

//

static void *extend_heap(size_t);
static void *coalesce(void *);
static char *find_fit(size_t);
static void place(void *, size_t);

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /*빈 가용 리스트를 만들기위헤 메모리 시스템에서 4워드를 가져온다*/
    heap_listp = mem_sbrk(4 * WSIZE);
    if ((heap_listp) == (void *)-1)
        return -1;

    PUT(heap_listp + (0 * WSIZE), PACK(12, 1));
    PUT(heap_listp + (1 * WSIZE), NULL); // successor 를 가리키는 포인터가 들어갈자리 ?Z
    PUT(heap_listp + (2 * WSIZE), PACK(12, 1));
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1));
    heap_listp += (WSIZE);
    init_successor = heap_listp; //
    // init = heap_listp;
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    printf("in init\n");
    return 0;
}

static void *extend_heap(size_t words)
{
    char *bp;
    size_t size;
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1)
        return NULL;

    PUT(GET_HEAD_POINTER(bp), PACK(size, 0));
    PUT(GET_FOOT_POINTER(bp), PACK(size, 0));
    PUT(GET_HEAD_POINTER(NEXT_BLKP(bp)), PACK(0, 1));

    return coalesce(bp);
}

/*
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;      /*Adjusted block size*/
    size_t extendsize; /*Amount to extend heap if no fit*/
    char *bp;

    if (size == 0)
        return NULL;

    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);

    if ((bp = find_fit(asize)) != NULL)
    {
        place(bp, asize);
        return bp;
    }

    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL)
        return NULL;
    place(bp, asize);
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(GET_HEAD_POINTER(ptr));

    PUT(GET_HEAD_POINTER(ptr), PACK(size, 0));
    PUT(GET_FOOT_POINTER(ptr), PACK(size, 0));
    coalesce(ptr);
}

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(GET_FOOT_POINTER(PREV_BLKP(bp)));
    // GET_HEAD_POINTER 지만 -WSIZE 만큼 가는거 기억해야함
    size_t next_alloc = GET_ALLOC(GET_HEAD_POINTER(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(GET_HEAD_POINTER(bp));

    // 앞 뒤 free블록이면 병합//
    if (prev_alloc && next_alloc)
    {
        if (GET(init_successor) == NULL)
        {
            PUT(bp, init_successor);
            PUT(bp + WSIZE, NULL);
            // 진입점 val 업데이트.
            PUT(init_successor, bp);
        }
        else
        {
            // 기존 블럭의 이전값 업데이트
            PUT(GET(init_successor), bp);
            // 새로들어가는애의 pre는 시작점이들어가야하고, next에는 기존pre가 지목하고있던애만 연결해주면되나 ?
            PUT(bp, init_successor);
            PUT(bp + WSIZE, GET(init_successor));
            // 진입점 val 업데이트
            PUT(init_successor, bp);
        }
        return bp;
    }
    else if (prev_alloc && !next_alloc)
    {
        /*
         * 다음블록이랑 병합이 가능한경우 다음블럭의 이전과 다음을 연결
         *  init -> new -> ori_init ->,,,->a->Y->c,, 여기서 a->c로 연결해야함
         */
        // a->Y->c 에서 a->c 과정
        char *next_free_block_next = GET(NEXT_BLKP(bp)); // Y를 가리키는 이전 블럭 포인터 의 넥스트에 Y다음껄 넣어주면됨 .
        PUT(next_free_block_next + WSIZE, GET(NEXT_BLKP(bp) + WSIZE));

        // 통합
        size += GET_SIZE(GET_HEAD_POINTER(NEXT_BLKP(bp)));
        PUT(GET_HEAD_POINTER(bp), PACK(size, 0));
        PUT(GET_FOOT_POINTER(bp), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc)
    {
        /*
         * 이전 블록과 통합 이전블록만 조작해주면됨
         * ori-init -> b ->.... -> a-> 이전블록->c
         * init -> 이전블록 -> ori_init -> ... ->a->c
         */
        // 통합후 기준 bp 변경
        size += GET_SIZE(GET_HEAD_POINTER(PREV_BLKP(bp)));
        PUT(GET_FOOT_POINTER(bp), PACK(size, 0));
        PUT(GET_HEAD_POINTER(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);

        char *pre_free_block_next = GET(PREV_BLKP(bp));
        PUT(pre_free_block_next + WSIZE, GET(PREV_BLKP(bp) + WSIZE));
    }
    else
    { /*
       * 양쪽에 넣는과정 ......next 블럭 처리 -> 통합 -> 이전꺼처리
       */
        // Y를 가리키는 이전 블럭 포인터 의 넥스트에 Y다음껄 넣어주면됨
        char *next_free_block_next = GET(NEXT_BLKP(bp));
        PUT(next_free_block_next + WSIZE, GET(NEXT_BLKP(bp) + WSIZE));
        //
        size += GET_SIZE(GET_HEAD_POINTER(PREV_BLKP(bp))) + GET_SIZE(GET_FOOT_POINTER(NEXT_BLKP(bp)));
        PUT(GET_HEAD_POINTER(PREV_BLKP(bp)), PACK(size, 0));
        PUT(GET_FOOT_POINTER(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
        //
        char *pre_free_block_next = GET(PREV_BLKP(bp));
        PUT(pre_free_block_next + WSIZE, GET(PREV_BLKP(bp) + WSIZE));
    }

    // 기존 블럭의 이전값 업데이트
    PUT(GET(init_successor), bp);
    // 새로들어가는애의 pre는 시작점이들어가야하고, next에는 기존pre가 지목하고있던애만 연결해주면되나 ?
    PUT(bp, init_successor);
    PUT(bp + WSIZE, GET(init_successor));
    // 진입점 val 업데이트
    PUT(init_successor, bp);

    return bp;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *bp, size_t size)
{
    if (bp == NULL)
        return mm_malloc(size);
    void *oldptr = bp;
    void *newptr;
    size_t copySize;
    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
    copySize = GET_SIZE(GET_HEAD_POINTER(oldptr));
    if (size < copySize)
        copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}

static char *find_fit(size_t asize)
{
    char *bp;
    // for (s,조건,증감) s => 시작위치 =처음 / 증가를 free 만검색하게
    // 시작 위치가 처음이면 heap_listp를 co에서 초기화 해주면 안됨!
    // --- -- --
    // 이거말고 이전에 할당한 free를 확인할 방법을 생각해야함 !
    // 위에나 아래나 프리를 모두 탐색이 베스트일거같은데
    for (bp = heap_listp; GET_SIZE(GET_HEAD_POINTER(bp)) > 0; bp = NEXT_BLKP(bp))
    {
        if ((!GET_ALLOC(GET_HEAD_POINTER(bp))) && (asize <= GET_SIZE(GET_HEAD_POINTER(bp))))
        {
            return bp;
        }
    }

    // for (bp = init; bp < heap_listp; bp = NEXT_BLKP(bp))
    // {
    //     if ((!GET_ALLOC(GET_HEAD_POINTER(bp))) && (asize <= GET_SIZE(GET_HEAD_POINTER(bp))))
    //     {
    //         return bp;
    //     }
    // }
    return NULL; /* No fit*/
}

static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(GET_HEAD_POINTER(bp));
    if ((csize - asize) >= (2 * DSIZE))
    {
        PUT(GET_HEAD_POINTER(bp), PACK(asize, 1));
        PUT(GET_FOOT_POINTER(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(GET_HEAD_POINTER(bp), PACK(csize - asize, 0));
        PUT(GET_FOOT_POINTER(bp), PACK(csize - asize, 0));
    }
    else
    {
        PUT(GET_HEAD_POINTER(bp), PACK(csize, 1));
        PUT(GET_FOOT_POINTER(bp), PACK(csize, 1));
    }
}