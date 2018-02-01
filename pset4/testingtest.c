// virtual_memory_map(pagetable, va, pa, sz, perm, allocator)
//    Map virtual address range `[va, va+sz)` in `pagetable`.
//    When `X >= 0 && X < sz`, the new pagetable will map virtual address
//    `va+X` to physical address `pa+X` with permissions `perm`.
//
//    Precondition: `va`, `pa`, and `sz` must be multiples of PAGESIZE
//    (4096).
//
//    Typically `perm` is a combination of `PTE_P` (the memory is Present),
//    `PTE_W` (the memory is Writable), and `PTE_U` (the memory may be
//    accessed by User applications). If `!(perm & PTE_P)`, `pa` is ignored.
//
//    Sometimes mapping memory will require allocating new page tables. The
//    `allocator` function should return a newly allocated page, or NULL
//    on allocation failure.
//
//    Returns 0 if the map succeeds, -1 if it fails because a required
//    page table could not be allocated.
int virtual_memory_map(x86_64_pagetable* pagetable, uintptr_t va,
    uintptr_t pa, size_t sz, int perm,
    x86_64_pagetable* (*allocator)(void));



r = virtual_memory_map(current->p_pagetable,
    addr, PAGEADDRESS(PAGENUMBER(freePage)),
    PAGESIZE, PTE_P | PTE_W | PTE_U, find_free_page);


virtual_memory_map(processes[freeProc].p_pagetable, i,
(uintptr_t)freePage, PAGESIZE, map.perm, find_free_page);

virtual_memory_map(kernel_pagetable, 0, 0, PROC_START_ADDR, PTE_P | PTE_W, NULL);
                           
// Map the console as acessible to both kernel and user processes
virtual_memory_map(kernel_pagetable, 0xB8000, 0xB8000, PAGESIZE, PTE_P | PTE_W | PTE_U, NULL);


                                   // map out the kernel as unreadible to processes
    virtual_memory_map(processes[pid].p_pagetable, 0, 0,
        PROC_START_ADDR, PTE_P | PTE_W, find_free_page);

// map the left over memory as invalid
virtual_memory_map(processes[pid].p_pagetable, PROC_START_ADDR, PROC_START_ADDR,
        MEMSIZE_PHYSICAL - PROC_START_ADDR, 0, find_free_page);

            // map the stack page
    virtual_memory_map(processes[pid].p_pagetable, MEMSIZE_VIRTUAL - PAGESIZE, PAGEADDRESS(PAGENUMBER(stackPage)),
    PAGESIZE, PTE_P | PTE_W | PTE_U, find_free_page);


    virtual_memory_map(first_page_num, va, vam.pa, PAGESIZE, vam.perm, find_free_page);


    virt        phys    
A   1000000     256 r
    1001000     257 r
    2000000      30 r/w
    2001000      31 r/w

B   1000000     256 (shared with A)
    1001000     257 (shared with A)
    2000000     30 (perm = read/only lazy copy to 258 update perm to read/write, restart)
    2001000     259 (copied from 31)

C   1000000     300 (unshared with A or B)


    "hey" === 300000 -> 24(W)

    "get" ==== 30000 -> 24(RW)

user
    0  protectd
    .  kerenrl
100000 text     r/o
200000 heap     r/w
210000 high heap mark
211000
2F0000 stack

executale  image 
text -specifes constants intgruction(code), string literals char *p="hello world", tables
data -initialzed data (strings) char p [] = "hello world"; int x = 5;
bss  - int y,z; #

load image into memory
100000 text initialis from imageag make r/o (not present -> read fault -> allocate phys page/load from image)
102000 end of text
200000 data iniita from image r/w_coredump
203000 end of DT_EXTRATAGIDX
204000 bss initialized with all zeros r/w_coredump

A forks to B
fork, share r/o pages, share r/w pages (reducing permssions to r/o, on both A & B)
handle become shared

B exec image file name
....
C exec 


b8f00