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

#define WSIZE 4             /*워드 크기*/
#define DSIZE 8             /*더블 워드 크기*/
#define CHUNKSIZE (1 << 8)  /*초기 가용블럭과 힙확장을 위한 기본크기 1을 비트쉬프트로 12번이동 ~ =2^12승 = 4096=4KB*/
#define SEGREGATED_SZIE 12  // 12까지한이유는?  -> 20까지 생각해보기
#define PAGE_REQUEST_SZIE 3 // 메모리 추가 요청시 요청하는 페이지수   --> 언제 사용하는지 확인해야함
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
    /*힙 확장을 안하고 필요할때 한다는 건가 ? 좋은 방식인지는 모르겠다 차라리 initheap을정해서 하는것도 ?*/
    return 0;
}

static void *extend_heap(size_t words)
{
    char *bp;
    char *new_bp;
    size_t size;

    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(new_bp = mem_sbrk(size)) == -1)
        return NULL;

    bp = new_bp;
    size_t page_size = size / PAGE_REQUEST_SZIE; // 블록하나의 크기 ?  지금기준에서는 3등분?
    while (size >= page_size)
    {
        PUT(GET_HEAD_POINTER(bp), PACK(page_size, 0)); // 빈블록의 헤더 초기화 ? 풋터는 초기화 안시켜주는 이유는 ?
        put_front_free_list(bp);
        size -= page_size;
        bp += page_size;
    }
    /// 아? 병합이 필요없어지는건가 ?
    return new_bp;
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

    PUT(GET_HEAD_POINTER(ptr), PACK(size, 0)); /*foot은? 헤더만으로 가능한가 ?*/
    put_front_free_list(ptr);
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
    PUT(GET_HEAD_POINTER(bp), PACK(GET_SIZE(GET_HEAD_POINTER(bp)), 1));
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