#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

team_t team = {
    /* Team name */
    "6team",
    /* First member's full name */
    "JJH",
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

#define CHUNKSIZE (1 << 8) /*초기 가용블럭과 힙확장을 위한 기본크기 1을 비트쉬프트로 12번이동 ~ =2^12승 = 4096=4KB*/

#define MAX(x, y) ((x) > (y) ? (x) : (y))

#define PACK(size, alloc) ((size) | (alloc)) /* 크기와 할당 비트를 통합해서 헤더와 풋터에 저장할 수 있는 값리턴 (p816 참고)*/

#define GET(p) (*(unsigned int *)(p))              /* p가 가리키는 워드를 읽어서 리턴*/
#define PUT(p, val) (*(unsigned int *)(p) = (val)) /* 워드에 val 저장*/

#define GET_SIZE(p) (GET(p) & ~0x07) /* 헤더 또는 풋터의 사이즈 리턴*/
#define GET_ALLOC(p) (GET(p) & 0x01) /* 헤더 또는 풋터의 할당비트 리턴 */

#define GET_HEAD_POINTER(bp) ((char *)(bp)-WSIZE)
#define GET_FOOT_POINTER(bp) ((char *)(bp) + GET_SIZE(GET_HEAD_POINTER(bp)) - DSIZE)

#define GET_PRE_FREE_POINTER(bp) (*(void **)(bp))
#define GET_NEXT_FREE_POINTER(bp) (*(void **)(bp + WSIZE))

#define SET_NEXT_POINTER(bp, qp) (GET_NEXT_FREE_POINTER(bp) = qp)
#define SET_PRE_POINTER(bp, qp) (GET_PRE_FREE_POINTER(bp) = qp)

static void *extend_heap(size_t);
static void *coalesce(void *);
static char *find_fit(size_t);
static void place(void *, size_t);

static void remove_in_free_list(void *bp);
static void put_front_free_list(void *bp);

//
static char *heap_listp;
static char *free_listp;
//

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /*빈 가용 리스트를 만들기위헤 메모리 시스템에서 4워드를 가져온다*/
    heap_listp = mem_sbrk(6 * WSIZE);
    if ((heap_listp) == (void *)-1)
        return -1;
    PUT(heap_listp + (0 * WSIZE), PACK(0, 0));
    PUT(heap_listp + (1 * WSIZE), PACK(2 * DSIZE, 1));
    PUT(heap_listp + (2 * WSIZE), NULL); // successor 를 가리키는 포인터가 들어갈자리 ?
    PUT(heap_listp + (3 * WSIZE), NULL);
    PUT(heap_listp + (4 * WSIZE), PACK(2 * DSIZE, 1));
    PUT(heap_listp + (5 * WSIZE), PACK(0, 1));

    free_listp = heap_listp + (DSIZE);
    if (extend_heap(4) == NULL)
        return -1;

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;
    // printf("in init\n");
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
    if (ptr == NULL)
        return;
    size_t size = GET_SIZE(GET_HEAD_POINTER(ptr));

    PUT(GET_HEAD_POINTER(ptr), PACK(size, 0));
    PUT(GET_FOOT_POINTER(ptr), PACK(size, 0));
    coalesce(ptr);
}

static void *coalesce(void *bp)
{
    // printf("%d\n", PREV_BLKP(bp) == bp ? 1 : 0);
    size_t prev_alloc = GET_ALLOC(GET_FOOT_POINTER(PREV_BLKP(bp)));
    // GET_HEAD_POINTER 지만 -WSIZE 만큼 가는거 기억해야함
    size_t next_alloc = GET_ALLOC(GET_HEAD_POINTER(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(GET_HEAD_POINTER(bp));

    if (prev_alloc && next_alloc)
    {
        put_front_free_list(bp);
        return bp;
    }
    else if (prev_alloc && !next_alloc)
    {
        /*
         * 다음블록이랑 병합이 가능한경우 다음블럭의 이전과 다음을 연결
         *  init -> new -> ori_init ->,,,->a->Y->c,, 여기서 a->c로 연결해야함
         */
        // a->Y->c 에서 a->c 과정
        // 통합
        size += GET_SIZE(GET_HEAD_POINTER(NEXT_BLKP(bp)));
        remove_in_free_list(NEXT_BLKP(bp));
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

        size += GET_SIZE(GET_HEAD_POINTER(PREV_BLKP(bp)));
        remove_in_free_list(PREV_BLKP(bp));
        PUT(GET_FOOT_POINTER(bp), PACK(size, 0));
        PUT(GET_HEAD_POINTER(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else
    { /*
       * 양쪽에 넣는과정 ......next 블럭 처리 -> 통합 -> 이전꺼처리
       */
        // Y를 가리키는 이전 블럭 포인터 의 넥스트에 Y다음껄 넣어주면됨
        //
        size += GET_SIZE(GET_HEAD_POINTER(PREV_BLKP(bp))) + GET_SIZE(GET_FOOT_POINTER(NEXT_BLKP(bp)));
        remove_in_free_list(NEXT_BLKP(bp));
        remove_in_free_list(PREV_BLKP(bp));
        PUT(GET_HEAD_POINTER(PREV_BLKP(bp)), PACK(size, 0));
        PUT(GET_FOOT_POINTER(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    put_front_free_list(bp);
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

    // for (bp = NEXT_BLKP(bp); GET_SIZE(GET_HEAD_POINTER(bp)) > 0; bp = NEXT_BLKP(bp))
    // {
    //     if (!(GET_ALLOC(GET_HEAD_POINTER(bp))) && (asize <= GET_SIZE(GET_HEAD_POINTER(bp))))
    //     {
    //         return bp;
    //     }
    // }

    for (bp = free_listp; !GET_ALLOC(GET_HEAD_POINTER(bp)); bp = GET_NEXT_FREE_POINTER(bp))
    {
        if (asize <= (size_t)GET_SIZE(GET_HEAD_POINTER(bp)))
            return bp;
    }

    return NULL; /* No fit*/
}

static void place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(GET_HEAD_POINTER(bp));

    if ((csize - asize) >= (2 * DSIZE))
    {
        PUT(GET_HEAD_POINTER(bp), PACK(asize, 1));
        PUT(GET_FOOT_POINTER(bp), PACK(asize, 1));
        remove_in_free_list(bp);
        bp = NEXT_BLKP(bp);
        PUT(GET_HEAD_POINTER(bp), PACK(csize - asize, 0));
        PUT(GET_FOOT_POINTER(bp), PACK(csize - asize, 0));
        coalesce(bp);
    }
    else
    {
        PUT(GET_HEAD_POINTER(bp), PACK(csize, 1));
        PUT(GET_FOOT_POINTER(bp), PACK(csize, 1));
        remove_in_free_list(bp);
    }
}

void remove_in_free_list(void *bp)
{
    if (GET_PRE_FREE_POINTER(bp))
        SET_NEXT_POINTER(GET_PRE_FREE_POINTER(bp), GET_NEXT_FREE_POINTER(bp));
    else
        free_listp = GET_NEXT_FREE_POINTER(bp);
    SET_PRE_POINTER(GET_NEXT_FREE_POINTER(bp), GET_PRE_FREE_POINTER(bp));
}

void put_front_free_list(void *bp)
{
    SET_NEXT_POINTER(bp, free_listp);
    SET_PRE_POINTER(free_listp, bp);
    SET_PRE_POINTER(bp, NULL);
    free_listp = bp;
}