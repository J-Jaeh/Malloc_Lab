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
    "진재혁_이민형_권지현",
    /* First member's email address */
    "1wo2gur@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""};

//-- Basic constants and macros  --//

#define WSIZE 4            /*워드 크기*/
#define DSIZE 8            /*더블 워드 크기*/
#define CHUNKSIZE (1 << 8) /*초기 가용블럭과 힙확장을 위한 기본크기 1을 비트쉬프트로 12번이동 ~ =2^12승 = 4096=4KB*/
#define SEGREGATED_SZIE 12 // 12까지한이유는?  -> 20까지 생각해보기
// #define PAGE_REQUEST_SZIE 3 // 메모리 추가 요청시 요청하는 페이지수   --> 언제 사용하는지 확인해야함
//--//
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))                                         /* 크기와 할당 비트를 통합해서 헤더와 풋터에 저장할 수 있는 값리턴 (p816 참고)*/
#define GET(p) (*(unsigned int *)(p))                                                /* p가 가리키는 워드를 읽어서 리턴*/
#define PUT(p, val) (*(unsigned int *)(p) = (val))                                   /* 워드에 val 저장*/
#define GET_SIZE(p) (GET(p) & ~0x07)                                                 /* 헤더 또는 풋터의 사이즈 리턴*/
#define GET_ALLOC(p) (GET(p) & 0x01)                                                 /* 헤더 또는 풋터의 할당비트 리턴 */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp)-WSIZE)))                /*물리주소상 다음블럭*/
#define PREV_BLKP(bp) ((char *)(bp)-GET_SIZE(((char *)(bp)-DSIZE)))                  /*물리주소상 다음블럭*/
#define GET_HEAD_POINTER(bp) ((char *)(bp)-WSIZE)                                    /*블럭 해드를 가리키는 포인터*/
#define GET_FOOT_POINTER(bp) ((char *)(bp) + GET_SIZE(GET_HEAD_POINTER(bp)) - DSIZE) /*블럭 꼬리를 가리키는 포인터*/

#define GET_PRE_FREE_POINTER(bp) (*(void **)(bp))          /*이전 가용블럭*/
#define GET_NEXT_FREE_POINTER(bp) (*(void **)(bp + WSIZE)) /*다음 가용블럭*/
#define GET_ROOT(class) (*(void **)((char *)(heap_listp) + (WSIZE + class)))

// #define SET_NEXT_POINTER(bp, qp) (GET_NEXT_FREE_POINTER(bp) = qp)
// #define SET_PRE_POINTER(bp, qp) (GET_PRE_FREE_POINTER(bp) = qp)

static void *extend_heap(size_t);
static void *coalesce(void *);
static char *find_fit(size_t);
static void place(void *, size_t);

static void remove_in_free_list(void *bp); /*가용리스트에서 제거*/
static void put_front_free_list(void *bp); /*가용리스트에 추가*/
static int get_class(size_t);

//
static char *heap_listp;
//

/*
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    /*빈 가용 리스트를 만들기위헤 메모리 시스템에서 4워드를 가져온다*/
    heap_listp = mem_sbrk((SEGREGATED_SZIE + 4) * WSIZE);
    if ((heap_listp) == (void *)-1)
        return -1;
    PUT(heap_listp + (0 * WSIZE), PACK(0, 0));
    PUT(heap_listp + (1 * WSIZE), PACK((SEGREGATED_SZIE + 2) * WSIZE, 1));
    PUT(heap_listp + ((SEGREGATED_SZIE + 2) * WSIZE), PACK((SEGREGATED_SZIE + 2) * WSIZE, 1));
    PUT(heap_listp + ((SEGREGATED_SZIE + 3) * WSIZE), PACK(0, 1));

    /*리스트블럭 NULL 할당*/
    for (int i = 0; i < SEGREGATED_SZIE; i++)
    {
        PUT(heap_listp + ((2 + i) * WSIZE), NULL);
    }
    heap_listp += DSIZE;

    if (extend_heap(4) == NULL)
        return -1;
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL)
        return -1;

    return 0;
}

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

void mm_free(void *ptr)
{
    size_t size = GET_SIZE(GET_HEAD_POINTER(ptr));

    PUT(GET_HEAD_POINTER(ptr), PACK(size, 0));
    PUT(GET_FOOT_POINTER(ptr), PACK(size, 0));
    coalesce(ptr);
}

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

static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(GET_FOOT_POINTER(PREV_BLKP(bp))); /*이전 블록 할당 상태*/
    size_t next_alloc = GET_ALLOC(GET_HEAD_POINTER(PREV_BLKP(bp))); /*다음 블록 할당 상태*/
    size_t size = GET_SIZE(GET_HEAD_POINTER(bp));

    if (prev_alloc && next_alloc)
    {
        put_front_free_list(bp);
        return bp;
    }
    else if (prev_alloc && !next_alloc)
    {
        remove_in_free_list(NEXT_BLKP(bp));
        size += GET_SIZE(GET_HEAD_POINTER(NEXT_BLKP(bp)));
        PUT(GET_HEAD_POINTER(bp), PACK(size, 0));
        PUT(GET_FOOT_POINTER(bp), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc)
    {
        remove_in_free_list(PREV_BLKP(bp));
        size += GET_SIZE(GET_HEAD_POINTER(PREV_BLKP(bp)));
        PUT(GET_HEAD_POINTER(PREV_BLKP(bp)), PACK(size, 0));
        PUT(GET_FOOT_POINTER(bp), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else
    {
        remove_in_free_list(PREV_BLKP(bp));
        remove_in_free_list(NEXT_BLKP(bp));
        size += GET_SIZE(GET_HEAD_POINTER(PREV_BLKP(bp))) + GET_SIZE(GET_FOOT_POINTER(NEXT_BLKP(bp)));
        PUT(GET_HEAD_POINTER(PREV_BLKP(bp)), PACK(size, 0));
        PUT(GET_FOOT_POINTER(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    put_front_free_list(bp);
    return bp;
}

static char *find_fit(size_t asize)
{
    int class = get_class(asize);
    char *bp = GET_ROOT(class);

    while (class < SEGREGATED_SZIE)
    {
        bp = GET_ROOT(class);
        while (bp != NULL)
        {
            if ((asize <= GET_SIZE(GET_HEAD_POINTER(bp))))
                return bp;

            bp = GET_NEXT_FREE_POINTER(bp);
        }
        class ++;
    }

    return NULL; /* No fit*/
}

static void place(void *bp, size_t asize)
{
    remove_in_free_list(bp);
    size_t csize = GET_SIZE(GET_HEAD_POINTER(bp));
    if ((csize - asize) >= (2 * DSIZE))
    {
        PUT(GET_HEAD_POINTER(bp), PACK(asize, 1));
        PUT(GET_FOOT_POINTER(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);

        PUT(GET_HEAD_POINTER(bp), PACK((csize - asize), 0));
        PUT(GET_FOOT_POINTER(bp), PACK((csize - asize), 0));
        put_front_free_list(bp);
    }
    else
    {
        PUT(GET_HEAD_POINTER(bp), PACK(csize, 1));
        PUT(GET_FOOT_POINTER(bp), PACK(csize, 1));
    }
}

static void remove_in_free_list(void *bp)
{
    int class = get_class(GET_SIZE(GET_HEAD_POINTER(bp)));
    if (bp == GET_ROOT(class))
    {
        GET_ROOT(class) = GET_NEXT_FREE_POINTER(GET_ROOT(class));
        return;
    }
    GET_NEXT_FREE_POINTER(GET_PRE_FREE_POINTER(bp)) = GET_NEXT_FREE_POINTER(bp);

    if (GET_NEXT_FREE_POINTER(bp) != NULL)
        GET_PRE_FREE_POINTER(GET_NEXT_FREE_POINTER(bp)) = GET_PRE_FREE_POINTER(bp);
}

void put_front_free_list(void *bp)
{
    int class = get_class(GET_SIZE(GET_HEAD_POINTER(bp)));
    GET_NEXT_FREE_POINTER(bp) = GET_ROOT(class);
    if (GET_ROOT(class) != NULL)
        GET_PRE_FREE_POINTER(GET_ROOT(class)) = bp;
    GET_ROOT(class) = bp;
}

int get_class(size_t size)
{
    size_t class_sizes[SEGREGATED_SZIE];
    class_sizes[0] = 16;

    for (int i = 0; i < SEGREGATED_SZIE; i++)
    {
        if (i != 0)
            class_sizes[i] = class_sizes[i - 1] << 1;
        if (size <= class_sizes[i])
            return 1;
    }

    return SEGREGATED_SZIE - 1;
}