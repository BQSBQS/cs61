#define M61_DISABLE 1
#include "m61.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <math.h>

// keep track of stats
struct m61_statistics global_stats;

struct m61_metadata
{
    unsigned long long size;        // number of bytes in allocation
    unsigned long long active_flag; // if equal to 1111 if allocation is not 'active'
    char *ptr_addr;                 // address of the pointer to the allocation
    const char *file;               // file in which allocation was called
    int line;                       // line in which allocation was called
    struct m61_metadata *prev;      // pointer to previous node in doubly linked list
    struct m61_metadata *next;      // pointer to next node in doubly linked list
    int padding;                    // padding to keep struct with 8-bit alignment
};

// To check for boundary write errors
typedef struct m61_overflow_buffer
{
    unsigned long long buffer; // overflow checker
} m61_overflow_buffer;

struct m61_metadata *head = NULL;

// create the struct in which we will store heavy hitter data
typedef struct m61_heavyhitter_node
{
    struct m61_heavyhitter_node *next;
    const char *fileName;
    int lineNumber;
    int HHNum;
    unsigned long long size;
} m61_heavyhitter_node;

// head of heavy hitter pointer linked list
m61_heavyhitter_node *HH_Head = NULL;

// a global variable to store the total number of allocated bytes of all nodes
// in the heavy hitter list
unsigned long long HH_total_bytes = 0;

// insert data for heavy hitter list
// if previous line == current line, add the size
// if not add info for the new size in the list
void update_HHList(const char *file, int line, unsigned long long sz)
{
    // set head to ptr
    m61_heavyhitter_node *ptr = HH_Head;
    // iterate through list
    while (ptr != NULL)
    {
        // if line is within the list
        if (ptr->fileName == file && ptr->lineNumber == line)
        {
            // update the fields
            ptr->size += sz;
            ptr->HHNum += 1;
            HH_total_bytes += sz;
            return;
        }
        // move to the next pointer
        ptr = ptr->next;
    }

    // if no data exists for the line and file add them to the front of the list
    m61_heavyhitter_node *new = malloc(sizeof(m61_heavyhitter_node));
    new->lineNumber = line;
    new->fileName = file;
    new->HHNum = 1;
    new->size = sz;
    new->next = HH_Head;
    HH_Head = new;
    HH_total_bytes += sz;
}

// void pointer gives us first address of this byte
// get byte of memory of sz
void *m61_malloc(size_t sz, const char *file, int line)
{
    (void)file, (void)line; // avoid uninitialized variable warnings

    // Prevent integer overflow: check to make sure sz not greater than 2^32-1
    // 2^32-1 is maximum value for 32-bit unsigned Int. The -1 is because integers start at 0 but counting starts at 1
    if (sz > (pow(2, 32) - 1) - sizeof(struct m61_statistics) - sizeof(m61_overflow_buffer))
    {
        global_stats.nfail++;
        global_stats.fail_size += sz;
        return NULL;
    }
    // Add extra space to check for errors
    m61_overflow_buffer buffer = {1111};

    // struct to hold metadata
    struct m61_metadata metadata = {sz, 0, NULL, file, line, NULL, NULL, 0};
    struct m61_metadata *ptr = NULL;
    // create extra space for pointer for metadata and overflow checker
    ptr = malloc(sizeof(struct m61_metadata) + sz + sizeof(m61_overflow_buffer));

    // update pointer address
    metadata.ptr_addr = (char *)(ptr + 1);

    // put data into metadata
    *ptr = metadata;

    // track stats
    global_stats.nactive++;
    global_stats.ntotal++;
    global_stats.active_size += sz;
    global_stats.total_size += sz;

    // min points to the beginning of the allocated data
    // max points to the end.
    char *heap_min = (char *)ptr;
    char *heap_max = (char *)ptr + sz + sizeof(struct m61_metadata);

    // update heap_min if there is only a new minimum
    // update heap_max if there is only a new max
    if (!global_stats.heap_min || global_stats.heap_min >= heap_min)
    {
        global_stats.heap_min = heap_min;
    }
    if (!global_stats.heap_max || global_stats.heap_max <= heap_max)
    {
        global_stats.heap_max = heap_max;
    }

    // if head exists set the next element in ptr to head
    if (head)
    {
        ptr->next = head;
        head->prev = ptr;
    }
    head = ptr;

    //  Store buffer at the end of allocated pointer
    m61_overflow_buffer *buffer_ptr = (m61_overflow_buffer *)((char *)(ptr + 1) + sz);
    *buffer_ptr = buffer;

    // add to heavy hitter list
    if (rand() > 0.7)
    {
        update_HHList(file, line, sz);
    }
    //  magic
    //  increment by the size of the pointer value in this case char?
    return ptr + 1;
}

void m61_free(void *ptr, const char *file, int line)
{
    (void)file, (void)line; // avoid uninitialized variable warnings
    if (!ptr)
    {
        return;
    }
    // if the heap > ptr force an abort
    if ((void *)global_stats.heap_min > ptr || (void *)global_stats.heap_max < ptr)
    {
        printf("MEMORY BUG: %s:%d: invalid free of pointer %p, not in heap\n", file, line, ptr);
        abort();
    }

    // grab the remaining space set to new pointer
    void *temp_ptr = (char *)ptr - sizeof(struct m61_metadata);
    struct m61_metadata *metadata_ptr = (struct m61_metadata *)temp_ptr;

    // node not freed but also does not equal the current pointer address
    if (metadata_ptr->active_flag != 1111)
    {
        if (metadata_ptr->ptr_addr != (char *)ptr)
        {
            printf("MEMORY BUG: %s:%d: invalid free of pointer %p, not allocated\n", file, line, ptr);
            abort();
        }
    }

    // test 28
    // check for overflow
    m61_overflow_buffer *buffer_ptr = (m61_overflow_buffer *)((char *)ptr + metadata_ptr->size);
    if (buffer_ptr->buffer != 1111)
    {

        printf("MEMORY BUG: %s:%d: detected wild write during free of pointer %p\n", file, line, ptr);
        abort();
    }

    // Remove node from double linked list
    if (metadata_ptr->prev)
        metadata_ptr->prev->next = metadata_ptr->next;
    else
        head = metadata_ptr->next;
    if (metadata_ptr->next)
        metadata_ptr->next->prev = metadata_ptr->prev;

    // if the freed pointer which was assigned a random int exists, print ERROR message
    // if pointer is not freed, free the pointer and assign it a random int (in our case 1111) so we know it has been freed and update global stats accordingly
    if (metadata_ptr && metadata_ptr->active_flag == 1111)
    {
        printf("MEMORY BUG: %s%d: invalid free of pointer %p\n", file, line, ptr);
    }
    else
    {
        global_stats.nactive--;
        global_stats.active_size -= metadata_ptr->size;
        // add flag to indicate node has been freed
        metadata_ptr->active_flag = 1111;
        free(temp_ptr);
    }
}

/// m61_realloc(ptr, sz, file, line)
///    Reallocate the dynamic memory pointed to by `ptr` to hold at least
///    `sz` bytes, returning a pointer to the new block. If `ptr` is NULL,
///    behaves like `m61_malloc(sz, file, line)`. If `sz` is 0, behaves
///    like `m61_free(ptr, file, line)`. The allocation request was at
///    location `file`:`line`.

void *m61_realloc(void *ptr, size_t sz, const char *file, int line)
{
    void *new_ptr = NULL;
    if (sz)
    {
        new_ptr = m61_malloc(sz, file, line);
    }
    if (ptr && new_ptr)
    {
        // Copy the data from `ptr` into `new_ptr`.
        // To do that, we must figure out the size of allocation `ptr`.
        struct m61_metadata *metadata = (struct m61_metadata *)ptr - 1;
        size_t old_sz = metadata->size;
        if (old_sz <= sz)
            memcpy(new_ptr, ptr, old_sz);
        else
            memcpy(new_ptr, ptr, sz);
    }
    m61_free(ptr, file, line);
    return new_ptr;
}

/// m61_calloc(nmemb, sz, file, line)
///    Return a pointer to newly-allocated dynamic memory big enough to
///    hold an array of `nmemb` elements of `sz` bytes each. The memory
///    is initialized to zero. If `sz == 0`, then m61_malloc may
///    either return NULL or a unique, newly-allocated pointer value.
///    The allocation request was at location `file`:`line`.

void *m61_calloc(size_t nmemb, size_t sz, const char *file, int line)
{
    // Your code here (to fix test016).
    // maximum number of a possible unsigned int
    unsigned int maximum_int = -1;
    // if nmemb * sz > maximum_int force a fail
    // because nmemb * sz is an int that means if that multiplication
    // happens to be bigger than an int then it will overflow and the result will wrap around 0.
    if (nmemb > maximum_int / sz || sz > maximum_int / nmemb)
    {
        global_stats.nfail++;
        return NULL;
    }
    void *ptr = m61_malloc(nmemb * sz, file, line);
    if (ptr)
    {
        memset(ptr, 0, nmemb * sz);
    }
    return ptr;
}

/// m61_getstatistics(stats)
///    Store the current memory statistics in `*stats`.

void m61_getstatistics(struct m61_statistics *stats)
{
    // clean stats
    // set global stats as stats
    memset(stats, 0, sizeof(struct m61_statistics));
    *stats = global_stats;
}

/// m61_printstatistics()
///    Print the current memory statistics.

void m61_printstatistics(void)
{
    struct m61_statistics stats;
    m61_getstatistics(&stats);

    printf("malloc count: active %10llu   total %10llu   fail %10llu\n",
           stats.nactive, stats.ntotal, stats.nfail);
    printf("malloc size:  active %10llu   total %10llu   fail %10llu\n",
           stats.active_size, stats.total_size, stats.fail_size);
}

/// m61_printleakreport()
///    Print a report of all currently-active allocated blocks of dynamic
///    memory.

void m61_printleakreport(void)
{
    for (struct m61_metadata *metadata = head; metadata != NULL; metadata = metadata->next)
    {
        printf("LEAK CHECK: %s:%d: allocated object %p with size %llu\n", metadata->file, metadata->line, metadata->ptr_addr, metadata->size);
    }
}

// prints heavy hitter report
// if total bytes of line > %10 print stats
void m61_heavyHitterTest()
{
    // get the first node in heavy hitter linked list
    m61_heavyhitter_node *ptr = HH_Head;
    // iterate over list
    while (ptr != NULL)
    {
        // if allocated bytes of file-line is greater than 10% of total bytes being used
        if ((float)ptr->size / (float)HH_total_bytes > .10)
        {
            printf("HEAVY HITTER: %s:%i: %llu bytes, (~%.1f)\n",
                   ptr->fileName, ptr->lineNumber, ptr->size,
                   (float)ptr->size / (float)HH_total_bytes * 100);
        }
        // go to the next one on the list until it reaches the end (NULL)
        ptr = ptr->next;
    }
}