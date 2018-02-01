#include "io61.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <errno.h>
#include <sys/mman.h>
#include <string.h>
#include <stdbool.h>
#define CACHE_SIZE 65536 // 2^16  POWERS of 2

// Cache for file
typedef struct io61_cache
{
    // Used in Write
    size_t size;              // Size of cache currently
    size_t start_char;        // Where the first character is located in writing cache   Position of the first char in writing cache
    size_t end_char;          // Where the last character is located in writing cache
    off_t before_current_pos; // Postion before current_pos
    // Used in Read
    unsigned char *memory; // unsigned because range 0-255
    off_t start;           // Location of first character in reading cache
    off_t end;             // Location of last character in reading cache
    bool mmapp_bool;       // If mmap has been used (True or False)
    off_t current_pos;     // Current position to read in the cache
} io61_cache;

// io61_file
//    store structure for io61 file wrappers. Add your own stuff.

struct io61_file
{
    int fd;
    io61_cache *cache;
    off_t size;
    int mode;
};

//  Create cache and return cache
io61_cache *io61_create_cache(io61_file *f)
{

    // Allocate space for cache
    io61_cache *cache = malloc(sizeof(io61_cache));
    // Set ptr memory that if current mode being read is RDONLY, then to mmap. Else say map has failed
    unsigned char *memory;
    if (f->mode == O_RDONLY)
    {
        memory = mmap(NULL, f->size, PROT_READ, MAP_PRIVATE, f->fd, 0);
    }
    else
    {
        memory = MAP_FAILED;
    }

    // Mmap did not fail aka Mode is ReadONLY
    if (memory != MAP_FAILED)
    {
        // Flag map as true
        cache->mmapp_bool = true;
        // Set memory
        cache->memory = memory;
        // Set cache end as the file size
        cache->end = f->size;
    }
    else // Mmap failed
    {
        // Set mmap flag as false
        cache->mmapp_bool = false;
        // calloc(#elems to be allocated, size of elems)
        cache->memory = calloc(CACHE_SIZE, sizeof(char));
    }
    // reset entire cache to 0 except memory(memory needs to be freed)
    cache->start = 0;
    cache->current_pos = 0;
    cache->before_current_pos = 0;
    cache->end = 0;
    cache->size = 0;
    cache->start_char = 0;
    cache->end_char = 0;
    // Return updated cache
    return cache;
}

// io61_fdopen(fd, mode)
//    Return a new io61_file that reads from and/or writes to the given
//    file descriptor `fd`. `mode` is either O_RDONLY for a read-only file
//    or O_WRONLY for a write-only file. You need not support read/write
//    files.

io61_file *io61_fdopen(int fd, int mode)
{
    assert(fd >= 0);
    io61_file *f = (io61_file *)malloc(sizeof(io61_file)); // Allocate space for file
    f->fd = fd;                                            // Set file descriptor
    f->mode = mode;                                        // Update incoming mode
    f->size = io61_filesize(f);                            // Update file size
    f->cache = io61_create_cache(f);                       // Create cache
    return f;                                              // Return updated File
}

// io61_close(f)
//    Close the io61_file `f` and release all its resources, including
//    any buffers.

int io61_close(io61_file *f)
{
    io61_flush(f);
    int r = close(f->fd);
    // If mmap is flagged true
    if (f->cache->mmapp_bool)
    {
        // Sys call, delete mappings for specified address range. (address, length)
        munmap(f->cache->memory, f->size);
        // Free cache
        free(f->cache);
        // Free cache
        free(f);
    }
    else
    {
        // Free cache memory
        free(f->cache->memory);
        // Free cache
        free(f->cache);
        // Free file
        free(f);
    }
    return r;
}

// io61_readc(f)
//    Read a single (unsigned) character from `f` and return it. Returns EOF
//    (which is -1) on error or end-of-file.

int io61_readc(io61_file *f)
{
    // This func should only run if the mode is Read Only aka File was not opened
    if (f->mode != O_RDONLY)
    {
        return -1;
    }

    // if cache has been mapped
    if (f->cache->mmapp_bool)
    {
        // Add onto the current position in the cache
        f->cache->current_pos++;

        //(bitwise) if (f->cache->current_pos > f->size) return EOF. ELSE return *(f->cache->memory + f->cache->current_pos - 1);
        return ((!(f->cache->current_pos > f->size) - 1) & EOF) | (~(!(f->cache->current_pos > f->size) - 1) & *(f->cache->memory + f->cache->current_pos - 1));
    }
    // Else if the caches current position is still less than the end of the cache
    else if (f->cache->current_pos < f->cache->end)
    {
        // Add onto the current position in the cache
        f->cache->current_pos++;
        // Return ptr to the upcoming character in the cache
        return *(f->cache->memory + f->cache->current_pos - f->cache->start - 1);
    }
    // If the current cache empty/not correct
    else
    {
        // Set the cache
        f->cache->start = f->cache->end;

        // Size that was read from file
        ssize_t read_size = read(f->fd, f->cache->memory, CACHE_SIZE);

        // If something was actually read
        if (read_size > 0)
        {
            // Update our cache end offset by the read size
            f->cache->end += read_size;
            // Update our cache position
            f->cache->current_pos++;
            // Contine to read the next character in the cache
            return *(f->cache->memory + f->cache->current_pos - f->cache->start - 1);
        }
        else
        {
            // Return end of file
            return EOF;
        }
    }
}

// io61_read(f, buf, sz)
//    Read up to `sz` characters from `f` into `buf`. Returns the number of
//    characters read on success; normally this is `sz`. Returns a short
//    count if the file ended before `sz` characters could be read. Returns
//    -1 an error occurred before any characters were read.

ssize_t io61_read(io61_file *f, char *buf, size_t sz)
{
    // This func should only run if the mode is Read Only aka File was not opened
    if (f->mode != O_RDONLY)
    {
        return -1;
    }

    // If files cache has been mmaped
    if (f->cache->mmapp_bool)
    {
        // Find which is smallest from mmap: file size - current position in cache OR blocksize passed in. Return smallest as the new size for cache to fill
        ssize_t size_read = (int *)(f->size - f->cache->current_pos) > (int *)sz ? sz : f->size - f->cache->current_pos;

        // Place a copy mmap (memory map) into our buffer
        memcpy(buf, f->cache->memory + f->cache->current_pos, size_read);

        // Update where we currently are in our cache
        f->cache->current_pos += size_read;

        // Return the amount of cache that was used/ amount that was read
        return size_read;
    }

    size_t nread = 0; // #Characters read so far

    while (nread != sz)
    {
        // Incoming cache size exists and If our cache does not go over the size of cache_left cache aka does not overflow
        if (f->cache->current_pos < f->cache->end)
        {
            // Size read from cache, depending on which is smallest, (size of cache - offset of where we are in cache) or (blocksize - how much we have already read)
            ssize_t read_from_cache = (int *)(f->cache->end - f->cache->current_pos) > (int *)(sz - nread) ? sz - nread : f->cache->end - f->cache->current_pos;

            // Place our copy from cache into the buffer
            memcpy(buf + nread, f->cache->memory + f->cache->current_pos - f->cache->start, read_from_cache);

            // Update our position in cache by we just currently read
            f->cache->current_pos += read_from_cache;

            // Return the amount of cache that was used/ amount that was read
            nread += read_from_cache;
        }
        // Else cache is either empty or not valid
        else
        {
            // Set start of cache to size of cache (to allign our cache and not overflow it)
            f->cache->start = f->cache->end;
            // Read directly from file
            ssize_t size = read(f->fd, f->cache->memory, CACHE_SIZE);
            // If what is read is more than 0 than update cache end offset
            if (size > 0)
            {
                f->cache->end += size;
            }
            else
            {
                // if nread exists than return nread, else return the size that was read from file
                return (ssize_t)nread ? (ssize_t)nread : size;
            }
        }
    }
    return nread;
}

// io61_writec(f)
//    Write a single character `ch` to `f`. Returns 0 on success or
//    -1 on error.

int io61_writec(io61_file *f, int ch)
{
    // This func should only work if mode is WRONLY
    if (f->mode != O_WRONLY)
    {
        return -1;
    }
    // If there is no space left in cache aka FULL
    if (!(CACHE_SIZE - f->cache->size))
    {
        // The cache we will write to file
        ssize_t write_cache = write(f->fd, f->cache->memory + f->cache->start_char, CACHE_SIZE - f->cache->start_char);

        // If we actually wrote cache to file
        if (write_cache >= 0)
        {
            f->cache->start_char += write_cache; // Set cache first to what was written in the file
            f->cache->size -= write_cache;       // Subtract cache size by what was written in the cache
            // (bitwise)if->cache->start_char = 0 if (CACHE_SIZE == f->cache->start_char) is TRUE. ELSE f->cache->start_char (equals itself)
            f->cache->start_char = ((!(CACHE_SIZE ^ f->cache->start_char) - 1) & 0) | (~(!(CACHE_SIZE == f->cache->start_char) - 1) & f->cache->start_char); // Check if cache first if it has reached cache_size, if it has reset. Else stay as is
            f->cache->end_char = ((!(CACHE_SIZE ^ f->cache->end_char) - 1) & 0) | (~(!(CACHE_SIZE == f->cache->end_char) - 1) & f->cache->end_char);         // Check cache last if it has reached cache_size, if it has reset. Else stay as is
        }
        // Else write not successful
        else
        {
            return -1;
        }
    }
    // Write our characters to cache
    const char *buffer = (const char *)&ch;
    *(f->cache->memory + f->cache->end_char) = *buffer;
    // Increase our position of the last character in the cache
    f->cache->end_char++;
    // Update our cache size
    f->cache->size++;
    return 0;
}

// io61_write(f, buf, sz)
//    Write `sz` characters from `buf` to `f`. Returns the number of
//    characters written on success; normally this is `sz`. Returns -1 if
//    an error occurred before any characters were written.

ssize_t io61_write(io61_file *f, const char *buf, size_t sz)
{
    // Only writes if mode is Write Only
    if (f->mode != O_WRONLY)
    {
        return -1;
    }

    size_t nwritten = 0;
    // Iterate until nwritten == block size(sz)
    while (nwritten != sz)
    {
        size_t cache_left = CACHE_SIZE - f->cache->size; // Get amount of cache space left

        // If position before current is not the same as current position in cache
        if (f->cache->before_current_pos != f->cache->current_pos)
        {
            // Seek the offset of our current position based on position in cache
            off_t offset_position = lseek(f->fd, f->cache->before_current_pos, SEEK_SET);

            if (offset_position != f->cache->before_current_pos)
            {
                return -1;
            }
            // The cache we will write to file
            ssize_t write_cache = write(f->fd, f->cache->memory + f->cache->start_char, f->cache->size);

            // If write cache is true returns >0. if it fails it returns -1
            if (write_cache >= 0)
            {
                // Update the previous position to current position, if all of the cache was read
                f->cache->before_current_pos = ((size_t)write_cache != f->cache->size) ? (f->cache->before_current_pos + write_cache) : f->cache->current_pos;
                // Increase the cache first size by what cache was written into file
                f->cache->start_char += write_cache;
                // Decrease the cache size by what cache was written into the file
                f->cache->size -= write_cache;
                // Check to see if last or first at the end of cache
                f->cache->start_char = (CACHE_SIZE == f->cache->start_char) ? 0 : f->cache->start_char;
                f->cache->end_char = (CACHE_SIZE == f->cache->end_char) ? 0 : f->cache->end_char;
                // Get new position in cache
                off_t offset_next_position = lseek(f->fd, f->cache->before_current_pos, SEEK_SET);
                // If error than return written bytes in while else return what was written(write_cache)
                if (offset_next_position != f->cache->before_current_pos)
                {
                    return (ssize_t)nwritten ? (ssize_t)nwritten : write_cache;
                }
            }
            // If write fails than return written bytes in while else return what was written(write_cache)
            else
            {
                return (ssize_t)nwritten ? (ssize_t)nwritten : write_cache;
            }
        }
        // If there is no cache space left
        else if (!cache_left)
        {
            // The cache we will write to file
            ssize_t write_cache = write(f->fd, f->cache->memory + f->cache->start_char, CACHE_SIZE - f->cache->start_char);
            // If write cache is true returns >0. if it fails it returns -1
            if (write_cache >= 0)
            {
                // Increase the cache first size by what cache was written into file
                f->cache->start_char += write_cache;
                // Decrease the cache size by what cache was written into the file
                f->cache->size -= write_cache;
                // Check to see if last or first at the end of cache
                f->cache->start_char = (CACHE_SIZE == f->cache->start_char) ? 0 : f->cache->start_char;
                f->cache->end_char = (CACHE_SIZE == f->cache->end_char) ? 0 : f->cache->end_char;
            }
            // If write fails than return written bytes in while else return what was written(write_cache)
            else
            {
                return (ssize_t)nwritten ? (ssize_t)nwritten : write_cache;
            }
        }
        // If there is actually space left in the cache
        else
        {
            // Find smallest of either cache space left or blocksize - cache we have already written
            ssize_t size = (int *)(sz - nwritten) > (int *)(cache_left) ? cache_left : sz - nwritten;

            // Write to the cache
            memcpy(f->cache->memory + f->cache->end_char, buf + nwritten, size);
            // Increase cache last by the size that was written to the cache
            f->cache->end_char += size;
            // Increase cache size by the size that was written to the cache
            f->cache->size += size;
            // Update the nwritten by the amount that was written into the cache
            nwritten += size;
        }
    }
    return nwritten;
}

// io61_flush(f)
//    Forces a write of all buffered data written to `f`.
//    If `f` was opened read-only, io61_flush(f) may either drop all
//    data buffered for reading, or do nothing.

int io61_flush(io61_file *f)
{
    // Read Only
    if (f->mode == O_RDONLY)
    {
        return 0;
    }

    // Cache size exists
    while (f->cache->size)
    {
        // If start character in cache exists subtract the Cache size by the start character ELSE set it to last character in cache
        size_t size = f->cache->start_char ? CACHE_SIZE - f->cache->start_char : f->cache->end_char;
        // The cache we will write to file
        ssize_t write_cache = write(f->fd, f->cache->memory + f->cache->start_char, size);
        // If write cache is true returns >0. if it fails it returns -1
        if (write_cache)
        {
            // Update position of char in cache by what cache was written to file
            f->cache->start_char += write_cache;
            // Decrease size of cache by what cache was written to file
            f->cache->size -= write_cache;
        }
        // If nothing was written to cache set the starting char in cache to nothing as well
        else if (write_cache == 0)
        {
            f->cache->start_char = write_cache;
        }
        // If write fails, returns -1 and fail func
        else
        {
            return -1;
        }
    }
    return 0;
}

// io61_seek(f, pos)
//    Change the file pointer for file `f` to `pos` bytes into the file.
//    Returns 0 on success and -1 on failure.

int io61_seek(io61_file *f, off_t pos)
{
    // If position is greater than the current end of cache OR position is less than the the start of the cache
    if (pos > f->cache->end || pos < f->cache->start)
    {
        // Determine if the allignment has failed
        off_t failed_allignment = pos - (pos % CACHE_SIZE);
        // Return offset on success else -1
        off_t r = lseek(f->fd, failed_allignment, SEEK_SET);
        if (r != failed_allignment)
        {
            return -1;
        }
        f->cache->end = failed_allignment;
        f->cache->start = failed_allignment;
    }
    f->cache->before_current_pos = f->cache->current_pos;
    f->cache->current_pos = pos;
    return 0;
}

// You shouldn't need to change these functions.

// io61_open_check(filename, mode)
//    Open the file corresponding to `filename` and return its io61_file.
//    If `filename == NULL`, returns either the standard input or the
//    standard output, depending on `mode`. Exits with an error message if
//    `filename != NULL` and the named file cannot be opened.

io61_file *io61_open_check(const char *filename, int mode)
{
    int fd;
    if (filename)
    {
        fd = open(filename, mode, 0666);
    }
    else if ((mode & O_ACCMODE) == O_RDONLY)
    {
        fd = STDIN_FILENO;
    }
    else
    {
        fd = STDOUT_FILENO;
    }
    if (fd < 0)
    {
        fprintf(stderr, "%s: %s\n", filename, strerror(errno));
        exit(1);
    }
    return io61_fdopen(fd, mode & O_ACCMODE);
}

// io61_filesize(f)
//    Return the size of `f` in bytes. Returns -1 if `f` does not have a
//    well-defined size (for instance, if it is a pipe).

off_t io61_filesize(io61_file *f)
{
    struct stat s;
    int r = fstat(f->fd, &s);
    if (r >= 0 && S_ISREG(s.st_mode))
    {
        return s.st_size;
    }
    else
    {
        return -1;
    }
}

// io61_eof(f)
//    Test if readable file `f` is at end-of-file. Should only be called
//    immediately after a `read` call that returned 0 or -1.

int io61_eof(io61_file *f)
{
    char x;
    ssize_t nread = read(f->fd, &x, 1);
    if (nread == 1)
    {
        fprintf(stderr, "Error: io61_eof called improperly\n\
  (Only call immediately after a read() that returned 0 or -1.)\n");
        abort();
    }
    return nread == 0;
}