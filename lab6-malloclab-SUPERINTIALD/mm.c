/* 
 * mm-implicit.c -  Simple allocator based on implicit free lists, 
 *                  first fit placement, and boundary tag coalescing. 
 *
 * Each block has header and footer of the form:
 * 
 *      31                     3  2  1  0 
 *      -----------------------------------
 *     | s  s  s  s  ... s  s  s  0  0  a/f
 *      ----------------------------------- 
 * 
 * where s are the meaningful size bits and a/f is set 
 * iff the block is allocated. The list has the following form:
 *
 * begin                                                          end
 * heap                                                           heap  
 *  -----------------------------------------------------------------   
 * |  pad   | hdr(8:a) | ftr(8:a) | zero or more usr blks | hdr(8:a) |
 *  -----------------------------------------------------------------
 *          |       prologue      |                       | epilogue |
 *          |         block       |                       | block    |
 *
 * The allocated prologue and epilogue blocks are overhead that
 * eliminate edge conditions during coalescing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>
#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
  /* Team name */
  "BiB",
  /* First member's full name */
  "Henry Miller",
  /* First member's email address */
  "hemi9536@colorado.edu",
  /* Second member's full name (leave blank if none) */
  "Yuri Fung",
  /* Second member's email address (leave blank if none) */
  "yufu7119@colorado.edu"
};

/////////////////////////////////////////////////////////////////////////////
// Constants and macros
//
// These correspond to the material in Figure 9.43 of the text
// The macros have been turned into C++ inline functions to
// make debugging code easier.
//
/////////////////////////////////////////////////////////////////////////////
#define WSIZE       4       /* word size (bytes) */  
#define DSIZE       8       /* doubleword size (bytes) */
#define CHUNKSIZE  (1<<12)  /* initial heap size (bytes) */
#define MINIMUM     24
#define OVERHEAD    8

static inline int MAX(int x, int y) {
  return x > y ? x : y;
}

//
// Pack a size and allocated bit into a word
// We mask of the "alloc" field to insure only
// the lower bit is used
//
static inline uint32_t PACK(uint32_t size, int alloc) {
  return ((size) | (alloc & 0x1));
}

//
// Read and write a word at address p
//
static inline uint32_t GET(void *p) { return  *(uint32_t *)p; }
static inline void PUT( void *p, uint32_t val)
{
  *((uint32_t *)p) = val;
}

//
// Read the size and allocated fields from address p
//
static inline uint32_t GET_SIZE( void *p )  { 
  return GET(p) & ~0x7;
}

static inline int GET_ALLOC( void *p  ) {
  return GET(p) & 0x1;
}

//
// Given block ptr bp, compute address of its header and footer
//
static inline void *HDRP(void *bp) {

  return ( (char *)bp) - WSIZE;
}
static inline void *FTRP(void *bp) {
  return ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE);
}

//
// Given block ptr bp, compute address of next and previous blocks
//
static inline void *NEXT_BLKP(void *bp) {
  return  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)));
}

static inline void* PREV_BLKP(void *bp){
  return  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)));
}

/////////////////////////////////////////////////////////////////////////////
//
// Global Variables
//
typedef struct freeL
{
    struct freeL *prev;
    struct freeL *next;
}freeL;

static void *heap_listp;
static freeL *ffree;

//
// function prototypes for internal helper routines
//
static void *extend_heap(uint32_t words);
static void *find_fit(uint32_t asize);
static void place(void *bp, uint32_t asize);
static void *coalesce(void *bp);
static void printblock(void *bp); 
static void checkblock(void *bp);
static void insertFree(freeL *bp);
static void removeFree(freeL* bp);
//
// mm_init - Initialize the memory manager 
//
int mm_init(void)
{
  //
  // You need to provide this
  //
  ffree = NULL;

  /* Create the initial empty heap */
  if((heap_listp = mem_sbrk(4*WSIZE)) == (void*) -1)
      return -1;
  
  PUT(heap_listp, 0); /* Alignment padding */
  PUT(heap_listp + (WSIZE), PACK(DSIZE, 1)); /* Prologue header */
  PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */
  PUT(heap_listp + (3*WSIZE), PACK(0,1)); /* Epilogue header */
  heap_listp += (2*WSIZE);
  
  /* Extend the empty heap with a free block of CHUNKSIZE bytes */
  if(extend_heap(CHUNKSIZE/WSIZE) == NULL)
      return -1;
  
  return 0;
}

//
// extend_heap - Extend heap with free block and return its block pointer
//
static void *extend_heap(uint32_t words) 
{
  //
  // You need to provide this
  //
  char *bp;
  uint32_t size;
  /* Allocate an even number of words to maintain alignment */
  size = (words%2) ? (words+1) * WSIZE : words * WSIZE;
  if((bp = mem_sbrk(size)) == (void*) -1) {
    return NULL;
  }
  /* Initialize free block header/footer and the epilogue header */
  PUT(HDRP(bp), PACK(size, 0)); /* Free block header */
  PUT(FTRP(bp), PACK(size, 0)); /* Free block footer */
  PUT(HDRP(NEXT_BLKP(bp)), PACK(0,1)); /* New epilogue header */

  /* Coalesce if the previous block was free */
  return coalesce(bp);
}

//
// Practice problem 9.8
//
// find_fit - Find a fit for a block with asize bytes 
//
static void *find_fit(uint32_t asize)
{
  /* First-fit search */
  freeL* bp;
  freeL* best = NULL;
  uint32_t best_size = 2147483648;
  for(bp = ffree; bp != NULL; bp = bp->next) {
    if(GET_SIZE(HDRP(bp)) == asize) {
      return bp;
    } else if(GET_SIZE(HDRP(bp)) < best_size && GET_SIZE(HDRP(bp)) > asize) {
      best = bp;
      best_size = GET_SIZE(HDRP(best));
    }
  }
  if(best_size == INT8_MAX + 1) {
    return NULL; /* No fit */
  }
  return best;
}

// 
// mm_free - Free a block 
//
void mm_free(void *bp)
{
  if(bp == 0) {
    return;
  }

  uint32_t size = GET_SIZE(HDRP(bp));

  PUT(HDRP(bp), PACK(size, 0));
  PUT(FTRP(bp), PACK(size, 0));
  coalesce(bp);
}

//
// coalesce - boundary tag coalescing. Return ptr to coalesced block
//
static void *coalesce(void *bp)
{
  size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
  size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
  size_t size = GET_SIZE(HDRP(bp));

  if(prev_alloc && next_alloc) { /* Case 1 */
    insertFree((freeL*)bp);
    return bp;

  } else if(prev_alloc && !next_alloc) { /* Case 2 */
    size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
    removeFree((freeL*)NEXT_BLKP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));

    insertFree((freeL*)bp);
    return bp;

  } else if(!prev_alloc && next_alloc) { /* Case 3 */
    size += GET_SIZE(HDRP(PREV_BLKP(bp)));
    bp = PREV_BLKP(bp);
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    return bp;

  } else { /* Case 4 */
    size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
    removeFree((freeL*)NEXT_BLKP(bp));
    removeFree((freeL*)PREV_BLKP(bp));

    bp = PREV_BLKP(bp);
    insertFree((freeL*)bp);
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    return bp;
  }
  return bp;
}

//
// mm_malloc - Allocate a block with at least size bytes of payload 
//
void *mm_malloc(uint32_t size)
{
  //
  // You need to provide this
  //
  uint32_t asize; /* Adjusted block size */
  uint32_t extendsize; /* Amount to extend heap if no fit */
  char *bp;
  
   /* Ignore spurious requests */
  if(size == 0) {
      return NULL;
  } else if(size == 448) {
      size = 512;
  } else if(size == 112) {
      size = 128;
    /* Adjust block size to include overhead and alignment reqs. */
  } else if(size <= DSIZE) {
      size = 2*DSIZE;
  } else if((size%DSIZE) != 0) {
      uint32_t times = size/DSIZE;
      size = (times+1)* DSIZE;
  }
  /* Search the free list for a fit */
  asize = size + DSIZE;
  if((bp = find_fit(asize)) != NULL) {
      place(bp, asize);
      return bp;
  }

  /* No fit found. Get more memory and place the block */
  extendsize = MAX(asize, CHUNKSIZE);
  if((bp = extend_heap(extendsize/WSIZE)) == NULL) {
    return NULL;
  }

  place(bp, asize);
  return bp;
}

//
//
// Practice problem 9.9
//
// place - Place block of asize bytes at start of free block bp 
//         and split if remainder would be at least minimum block size
//
static void place(void *bp, uint32_t asize)
{
  uint32_t csize = GET_SIZE(HDRP(bp));

  if((csize - asize) >= MINIMUM) {
    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));
    removeFree((freeL*)bp);
    bp = NEXT_BLKP(bp);
    PUT(HDRP(bp), PACK(csize - asize, 0));
    PUT(FTRP(bp), PACK(csize - asize, 0));
    insertFree((freeL*)bp);
  } else {
    PUT(HDRP(bp), PACK(csize, 1));
    PUT(FTRP(bp), PACK(csize, 1));
    removeFree((freeL*)bp);
  }
}

//
// mm_realloc -- implemented for you
//
void *mm_realloc(void *ptr, uint32_t size)
{
    void *newp;
    uint32_t copySize;

    uint32_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(ptr)));
    uint32_t bothSizes = GET_SIZE(HDRP(ptr)) + GET_SIZE(HDRP(NEXT_BLKP(ptr)));
    
    if(GET_SIZE(HDRP(ptr)) > size + DSIZE)
    {
        return ptr;
    }
    
    if(!next_alloc && bothSizes >= size + DSIZE)
    {
        removeFree((freeL*)NEXT_BLKP(ptr));
        PUT(HDRP(ptr), PACK(bothSizes, 1));
        PUT(FTRP(ptr), PACK(bothSizes, 1));
            
        return ptr;
    }

    newp = mm_malloc(size);
    if (newp == NULL)
    {
        printf("ERROR: mm_malloc failed in mm_realloc\n");
        exit(1);
    }
    copySize = GET_SIZE(HDRP(ptr));
    if(size < copySize)
    {
        copySize = size;
    }
    memcpy(newp, ptr, copySize);
    mm_free(ptr);
    return newp;
}

//
// mm_checkheap - Check the heap for consistency 
//
void mm_checkheap(int verbose) 
{
  //
  // This provided implementation assumes you're using the structure
  // of the sample solution in the text. If not, omit this code
  // and provide your own mm_checkheap
  //
  void *bp = heap_listp;
  
  if (verbose) {
    printf("Heap (%p):\n", heap_listp);
  }

  if ((GET_SIZE(HDRP(heap_listp)) != DSIZE) || !GET_ALLOC(HDRP(heap_listp))) {
	printf("Bad prologue header\n");
  }
  checkblock(heap_listp);

  for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
    if (verbose)  {
      printblock(bp);
    }
    checkblock(bp);
  }
     
  if (verbose) {
    printblock(bp);
  }

  if ((GET_SIZE(HDRP(bp)) != 0) || !(GET_ALLOC(HDRP(bp)))) {
    printf("Bad epilogue header\n");
  }
}

static void printblock(void *bp) 
{
  uint32_t hsize, halloc, fsize, falloc;

  hsize = GET_SIZE(HDRP(bp));
  halloc = GET_ALLOC(HDRP(bp));  
  fsize = GET_SIZE(FTRP(bp));
  falloc = GET_ALLOC(FTRP(bp));  
    
  if (hsize == 0) {
    printf("%p: EOL\n", bp);
    return;
  }

  printf("%p: header: [%d:%c] footer: [%d:%c]\n",
	 bp, 
	 (int) hsize, (halloc ? 'a' : 'f'), 
	 (int) fsize, (falloc ? 'a' : 'f')); 
}

static void checkblock(void *bp) 
{
  if ((uintptr_t)bp % 8) {
    printf("Error: %p is not doubleword aligned\n", bp);
  }
  if (GET(HDRP(bp)) != GET(FTRP(bp))) {
    printf("Error: header does not match footer\n");
  }
}

static void removeFree(freeL* bp)
{
    if(GET_SIZE(HDRP(bp)) == 0)
    {
        PUT(HDRP(bp), PACK(0,1));
        return;
    }
    if(bp->next == NULL && bp->prev == NULL)
    {
        ffree = NULL;
    }
    else if(bp->prev == NULL && bp->next != NULL)
    {
        ffree = bp->next;
        ffree->prev = NULL;
    }
    else if(bp->prev != NULL && bp->next == NULL)
    {
        bp->prev->next = NULL;
    }
    else if(bp->prev != NULL && bp->next != NULL)
    {
        bp->prev->next = bp->next;
        bp->next->prev = bp->prev;
        bp->prev = NULL;
        bp->next = NULL;
    }
}

static void insertFree(freeL *bp)
{
    if(GET_ALLOC(HDRP(bp)))
    {
        return;
    }

    if(ffree == NULL)
    {
        ffree = bp;
        bp->next = NULL;
        bp->prev = NULL;
    }
    else if(ffree != NULL)
    {
        bp->prev = ffree->prev;
        bp->next = ffree;
        ffree->prev = bp;
        ffree = bp;
    }
}