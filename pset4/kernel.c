#include "kernel.h"
#include "lib.h"

// kernel.c
//
//    This is the kernel.

// INITIAL PHYSICAL MEMORY LAYOUT
//
//  +-------------- Base Memory --------------+
//  v                                         v
// +-----+--------------------+----------------+--------------------+---------/
// |     | Kernel      Kernel |       :    I/O | App 1        App 1 | App 2
// |     | Code + Data  Stack |  ...  : Memory | Code + Data  Stack | Code ...
// +-----+--------------------+----------------+--------------------+---------/
// 0  0x40000              0x80000 0xA0000 0x100000             0x140000
//                                             ^
//                                             | \___ PROC_SIZE ___/
//                                      PROC_START_ADDR

int8_t current_owner = 0;
// x86_64_pagetable *allocator(void);
x86_64_pagetable *find_free_page(void);

// Initiate copy pagetable
x86_64_pagetable *copy_pagetable(x86_64_pagetable *pagetable, int8_t owner);

#define PROC_SIZE 0x40000 // initial state only

static proc processes[NPROC]; // array of process descriptors
                              // Note that `processes[0]` is never used.
proc *current;                // pointer to currently executing proc

#define HZ 100         // timer interrupt frequency (interrupts/sec)
static unsigned ticks; // # timer interrupts so far

void schedule(void);
void run(proc *p) __attribute__((noreturn));

// PAGEINFO
//
//    The pageinfo[] array keeps track of information about each physical page.
//    There is one entry per physical page.
//    `pageinfo[pn]` holds the information for physical page number `pn`.
//    You can get a physical page number from a physical address `pa` using
//    `PAGENUMBER(pa)`. (This also works for page table entries.)
//    To change a physical page number `pn` into a physical address, use
//    `PAGEADDRESS(pn)`.
//
//    pageinfo[pn].refcount is the number of times physical page `pn` is
//      currently referenced. 0 means it's free.
//    pageinfo[pn].owner is a constant indicating who owns the page.
//      PO_KERNEL means the kernel, PO_RESERVED means reserved memory (such
//      as the console), and a number >=0 means that process ID.
//
//    pageinfo_init() sets up the initial pageinfo[] state.

typedef struct physical_pageinfo
{
    int8_t owner;
    int8_t refcount;
} physical_pageinfo;

static physical_pageinfo pageinfo[PAGENUMBER(MEMSIZE_PHYSICAL)];

typedef enum pageowner {
    PO_FREE = 0,      // this page is free
    PO_RESERVED = -1, // this page is reserved memory
    PO_KERNEL = -2    // this page is used by the kernel
} pageowner_t;

static void pageinfo_init(void);

// Memory functions

void check_virtual_memory(void);
void memshow_physical(void);
void memshow_virtual(x86_64_pagetable *pagetable, const char *name);
void memshow_virtual_animate(void);

// kernel(command)
//    Initialize the hardware and processes and start running. The `command`
//    string is an optional string passed from the boot loader.

static void process_setup(pid_t pid, int program_number);

// FOR DEBUG PURPOSES
// dumpPageTable(string, x86_64_pagetable, int)
//    Used to debug any issues regarding Pagetable loging to log.txt
//    Best used wrapped around an if you don't want to fail with an assert(0)
//    to stop the proc and check your logs
void dumpPageTable(char *who, x86_64_pagetable *page_tab, int owner)
{
    if (page_tab == NULL)
    {
        log_printf("dumpPageTable: %s, no page table\n", who);
        return;
    }
    if (1)
    {
        log_printf("pagetable: %x\n", page_tab);
        assert(pageinfo[PAGENUMBER(0xB8000)].owner == PO_RESERVED);
        assert(pageinfo[PAGENUMBER(0xB8000)].refcount == 1);

        for (uintptr_t va = 0; va < MEMSIZE_VIRTUAL; va += PAGESIZE)
        {

            vamapping vam = virtual_memory_lookup(page_tab, va);

            if (vam.pn < 0)
                continue;

            log_printf("dumppagetable: %s: pid %d, va %x, page %d, %s%s%s %d %d\n",
                       who, owner, va, vam.pn,
                       (vam.perm & PTE_U) ? "U" : "",
                       (vam.perm & PTE_W) ? "W" : "",
                       (vam.perm & PTE_P) ? "P" : "",
                       pageinfo[vam.pn].owner,
                       pageinfo[vam.pn].refcount);
        }
    }
}

// FOR DEBUG PURPOSES
// dumpProcesses(void)
//    Used to log any processes that are not free. Best used to debug in an if statement
void dumpProcesses(void)
{
    for (int i = 0; i < NPROC; i++)
    {
        if (processes[i].p_state != P_FREE)
        {
            log_printf("process %d is %d\n", i, processes[i].p_state);
        }
    }
}

// release_page(PAGENUMBER, pid)
//    Decreases refcount and IF EITHER page is not referenced/FREE OR owner === pid THEN set owner as FREE
void release_page(int pageNum, pid_t pid)
{
    pageinfo[pageNum].refcount--;
    if (pageinfo[pageNum].refcount == 0 || pageinfo[pageNum].owner == pid)
        pageinfo[pageNum].owner = PO_FREE;
}

// FOR DEBUG PURPOSES
// should fail on occurence of problem
int check_process(char *who, x86_64_pagetable *pagetable, pid_t pid)
{
    // lookup
    if (pagetable == NULL)
        return 0;
    for (uintptr_t va = PROC_START_ADDR; va < MEMSIZE_VIRTUAL; va += PAGESIZE)
    {
        vamapping vam = virtual_memory_lookup(pagetable, va);
        if (vam.pn >= 0)
        {
            pid_t owner = pageinfo[vam.pn].owner;

            if (pageinfo[vam.pn].refcount == 0)
            {
                log_printf("found zero refcount page in virtual mapping 1\n");
                dumpPageTable("addr space lookup pt:", pagetable, pid);
                log_printf("found zero refcount page in virtual mapping 2\n");
                log_printf("pid=%d,%d\n", pid, processes[pid].p_pid);
                log_printf("pagetable=%x\n", pagetable);
                log_printf("addr_space_lookup: %s: va=%x, page#=%d\n",
                           who, va, vam.pn);
                log_printf("addr_space_lookup: %s: o=%d, pid=%d\n",
                           who, owner, pid);
                return -1;
            }
            else if (pageinfo[vam.pn].refcount == 1)
            {

                if ((vam.perm & (PTE_U | PTE_W)) == (PTE_U | PTE_W))
                {
                    // current owner != incoming owner && &&owner is proc ID
                    if (owner != pid && owner != PO_FREE && owner >= 0)
                    {
                        log_printf("cross linked page 1 \n");
                        dumpPageTable("addr space lookup pt:", pagetable, pid);
                        log_printf("cross linked page 2\n");
                        log_printf("pid=%d,%d\n", pid, processes[pid].p_pid);
                        log_printf("pagetable=%x\n", pagetable);
                        log_printf("addr_space_lookup: %s: va=%x, page#=%d\n",
                                   who, va, vam.pn);
                        log_printf("addr_space_lookup: %s: o=%d, pid=%d\n",
                                   who, owner, pid);
                        return -1;
                    }
                }
            }
        }
    }
    return 0;
}

//
int check_all_processes(char *who)
{
    for (int i = 0; i < NPROC; i++)
    {
        if (processes[i].p_state == P_FREE)
            continue;
        //log_printf("%s: %d\n", who, i);
        if (check_process(who, processes[i].p_pagetable, i) < 0)
            return -1;
    }
    return 0;
}

// Partial cleanup of processes from given length
// This will only ever be called directly outside of process_cleanup in INT_SYS_FORK
// this is so it does fully free all pagetables in a process
void process_cleanup_partial(pid_t pid, uintptr_t vaLast)
{
    // Set Process as free
    processes[pid].p_state = P_FREE;

    // If pagetable of pid does not exist
    if (processes[pid].p_pagetable != NULL)
    {
        for (uintptr_t va = PROC_START_ADDR; va < MEMSIZE_VIRTUAL; va += PAGESIZE)
        {
            // If it goes beyond passed in length
            if (va >= vaLast)
                break;

            // If lookup fails skip
            vamapping vam = virtual_memory_lookup(processes[pid].p_pagetable, va);
            if (vam.pn < 0)
            {
                continue;
            }

            // free page
            release_page(vam.pn, pid);
        }
        release_page(PAGENUMBER(processes[pid].p_pagetable), pid);
        processes[pid].p_pagetable = NULL;
    }

    for (int i = 0; i < PAGENUMBER(MEMSIZE_PHYSICAL); i++)
    {
        if (pageinfo[i].owner == pid)
            release_page(i, pid);
    }

    // marking a process as free:
    processes[pid].p_state = P_FREE;
}
// Full cleanup of processes from beginning to end
// pass MEMSIZE VIRTUAL to ensure it cleans everything
void process_cleanup(pid_t pid)
{
    process_cleanup_partial(pid, MEMSIZE_VIRTUAL);
}

void kernel(const char *command)
{
    hardware_init();
    pageinfo_init();
    console_clear();
    timer_init(HZ);

    // Map kernel pages as only acessible to the kernel and not the user
    virtual_memory_map(kernel_pagetable, 0, 0, PROC_START_ADDR, PTE_P | PTE_W, NULL);

    // Map the console as acessible to both kernel and user processes
    virtual_memory_map(kernel_pagetable, 0xB8000, 0xB8000, PAGESIZE, PTE_P | PTE_W | PTE_U, NULL);

    // Set up process descriptors
    memset(processes, 0, sizeof(processes));
    for (pid_t i = 0; i < NPROC; i++)
    {
        processes[i].p_pid = i;
        processes[i].p_state = P_FREE;
    }

    if (command && strcmp(command, "fork") == 0)
    {
        process_setup(1, 4);
    }
    else if (command && strcmp(command, "forkexit") == 0)
    {
        process_setup(1, 5);
    }
    else
    {
        for (pid_t i = 1; i <= 4; ++i)
        {
            process_setup(i, i - 1);
        }
    }

    // Switch to the first process using run()
    run(&processes[1]);
}

// process_setup(pid, program_number)
//    Load application program `program_number` as process number `pid`.
//    This loads the application's code and data into memory, sets its
//    %rip and %rsp, gives it a stack page, and marks it as runnable.

void process_setup(pid_t pid, int program_number)
{
    // init process
    process_init(&processes[pid], 0);

    // copy pagetable
    processes[pid].p_pagetable = copy_pagetable((x86_64_pagetable *)kernel_pagetable, pid);

    current_owner = pid;
    log_printf("process setup: setting current_owner %d, pid %d\n", current_owner, pid);

    // map out the kernel as unreadible to processes

    virtual_memory_map(processes[pid].p_pagetable, 0, 0,
                       PROC_START_ADDR, PTE_P | PTE_W, find_free_page);
    virtual_memory_map(processes[pid].p_pagetable, 0xB8000, 0xB8000,
                       PAGESIZE, PTE_P | PTE_U | PTE_W, find_free_page);

    // map the left over memory as invalid
    virtual_memory_map(processes[pid].p_pagetable, PROC_START_ADDR, PROC_START_ADDR,
                       MEMSIZE_PHYSICAL - PROC_START_ADDR, 0, find_free_page);

    int r = program_load(&processes[pid], program_number, NULL);
    assert(r >= 0);

    x86_64_pagetable *stackPage = find_free_page();
    assert(stackPage != NULL);

    // put the bottom of the stack at the highest possible address
    processes[pid].p_registers.reg_rsp = MEMSIZE_VIRTUAL;

    // allocate the bottom of the stack
    assign_physical_page((uintptr_t)stackPage, pid);

    // map the stack page
    virtual_memory_map(processes[pid].p_pagetable, MEMSIZE_VIRTUAL - PAGESIZE, PAGEADDRESS(PAGENUMBER(stackPage)),
                       PAGESIZE, PTE_P | PTE_W | PTE_U, find_free_page);

    // mark the process as runnable
    processes[pid].p_state = P_RUNNABLE;
}

// find free page
x86_64_pagetable *find_free_page(void)
{

    // Create a return var
    int freePage = 0;

    // Iterate over page info until you find free page
    while (pageinfo[freePage].refcount > 0 && freePage < PAGENUMBER(MEMSIZE_PHYSICAL))
        freePage++;

    // If free page is not in bounds of pageinfo array return NULL
    if (freePage >= PAGENUMBER(MEMSIZE_PHYSICAL))
        return NULL;

    // Update owner and refcount
    pageinfo[freePage].owner = current_owner;
    pageinfo[freePage].refcount++;

    // reset new available page to 0
    memset((void *)PAGEADDRESS(freePage), 0, PAGESIZE);

    // update reference

    // return page pointer
    return (x86_64_pagetable *)PAGEADDRESS(freePage);
}

// Copy pagetable
x86_64_pagetable *copy_pagetable(x86_64_pagetable *pagetable, int8_t owner)
{
    current_owner = owner;

    x86_64_pagetable *first_page_num = find_free_page();

    if (first_page_num == NULL)
    {
        return first_page_num;
    }

    for (uintptr_t va = 0; va < MEMSIZE_VIRTUAL; va += PAGESIZE)
    {
        vamapping vam = virtual_memory_lookup(pagetable, va);

        if (vam.pn > -1)
        {
            if (virtual_memory_map(first_page_num, va, vam.pa, PAGESIZE, vam.perm, find_free_page) < 0)
                return NULL;
        }
    }

    // return the new pagetable
    return first_page_num;
}

// assign_physical_page(addr, owner)
//    Allocates the page with physical address `addr` to the given owner.
//    Fails if physical page `addr` was already allocated. Returns 0 on
//    success and -1 on failure. Used by the program loader.

int assign_physical_page(uintptr_t addr, int8_t owner)
{
    // lazy on write and lazy on read
    // lazy on read:
    // map pages to undefined area that causes fault when you do read/
    //
    if ((addr & 0xFFF) != 0 || addr >= MEMSIZE_PHYSICAL)
    {
        log_printf("app fails, addr = 0x%x\n", addr);
        return -1;
    }

    if ((addr & 0xFFF) != 0 || addr >= MEMSIZE_PHYSICAL ||
        pageinfo[PAGENUMBER(addr)].refcount != 0)
    {
        log_printf("app fails, addr = 0x%x, rc = %d, owner = %d\n",
                   addr,
                   pageinfo[PAGENUMBER(addr)].refcount,
                   pageinfo[PAGENUMBER(addr)].owner);
        return -1;
    }
    else
    {
        log_printf("app succeeded!!\n");
        pageinfo[PAGENUMBER(addr)].refcount = 1;
        pageinfo[PAGENUMBER(addr)].owner = owner;
        if (PAGENUMBER(addr) == 30 || owner == 5)
        {
            log_printf("assign_physical_page: pid %d is being assigned page %d\n", owner, PAGENUMBER(addr));
        }
        return 0;
    }
}

// exception(reg)
//    Exception handler (for interrupts, traps, and faults).
//
//    The register values from exception time are stored in `reg`.
//    The processor responds to an exception by saving application state on
//    the kernel's stack, then jumping to kernel assembly code (in
//    k-exception.S). That code saves more registers on the kernel's stack,
//    then calls exception().
//
//    Note that hardware interrupts are disabled whenever the kernel is running.

void exception(x86_64_registers *reg)
{
    // Copy the saved registers into the `current` process descriptor
    // and always use the kernel's page table.KERNEL_START_ADDR
    current->p_registers = *reg;
    set_pagetable(kernel_pagetable);

    // It can be useful to log events using `log_printf`.
    // Events logged this way are stored in the host's `log.txt` file.
    /*log_printf("proc %d: exception %d\n", current->p_pid, reg->reg_intno);*/

    // Show the current cursor location and memory state
    // (unless this is a kernel fault).
    console_show_cursor(cursorpos);
    if (reg->reg_intno != INT_PAGEFAULT || (reg->reg_err & PFERR_USER))
    {
        check_virtual_memory();
        memshow_physical();
        memshow_virtual_animate();
    }

    // If Control-C was typed, exit the virtual machine.
    check_keyboard();
    // Actually handle the exception.
    switch (reg->reg_intno)
    {

    case INT_SYS_PANIC:
        panic(NULL);
        break; // will not be reached

    case INT_SYS_GETPID:
        current->p_registers.reg_rax = current->p_pid;
        break;

    case INT_SYS_YIELD:
        schedule();
        break; /* will not be reached */

    case INT_SYS_PAGE_ALLOC:
    {

        uintptr_t addr = current->p_registers.reg_rdi;

        // Make sure address is neither unalligned or already allocated
        if ((addr & 0xFFF) != 0)
        {
            log_printf("page alloc unalinged address 0x%x\n", addr);
            current->p_registers.reg_rax = -1;
            break;
        }

        vamapping vam = virtual_memory_lookup(current->p_pagetable, addr);
        if (vam.pn >= 0)
        {
            log_printf("virtual memory page %x already allocated in process %d\n",
                       addr, current->p_pid);
            current->p_registers.reg_rax = -1;
            break;
        }

        // update owner
        current_owner = current->p_pid;

        // Find an available free page
        x86_64_pagetable *freePage = find_free_page();

        // If there is no free page return -1
        if (freePage == NULL)
        {
            // RAX is the return register so indicate that this has failed to register
            current->p_registers.reg_rax = -1;
            break;
        }

        int r = virtual_memory_map(current->p_pagetable,
                                   addr, PAGEADDRESS(PAGENUMBER(freePage)),
                                   PAGESIZE, PTE_P | PTE_W | PTE_U, find_free_page);

        // Should R fail release anything uncorrectly mapped and return
        if (r < 0)
        {
            release_page(PAGENUMBER(freePage), current->p_pid);
            current->p_registers.reg_rax = -1;
            break;
        }

        // Set and return the return register RAX
        current->p_registers.reg_rax = 0;

        break;
    }

    case INT_TIMER:
        ++ticks;
        schedule();
        break; /* will not be reached */

    case INT_PAGEFAULT:
    {
        // Analyze faulting address and access type.
        uintptr_t addr = rcr2();
        const char *operation = reg->reg_err & PFERR_WRITE
                                    ? "write"
                                    : "read";
        const char *problem = reg->reg_err & PFERR_PRESENT
                                  ? "protection problem"
                                  : "missing page";

        if (!(reg->reg_err & PFERR_USER))
        {
            panic("Kernel page fault for %p (%s %s, rip=%p)!\n",
                  addr, operation, problem, reg->reg_rip);
        }
        // dumpPageTable("PROC_PAGEFAULT", current->p_pagetable, current->p_pid);

        console_printf(CPOS(24, 0), 0x0C00,
                       "Process %d page fault for %p (%s %s, rip=%p)!\n",
                       current->p_pid, addr, operation, problem, reg->reg_rip);
        current->p_state = P_BROKEN;
        break;
    }

    case INT_SYS_FORK:
    {
        int returnEarly = 0;
        // Find a free process and its index
        int freeProc = 1;
        while (processes[freeProc].p_state != P_FREE && freeProc < NPROC)
        {
            freeProc++;
        }
        // If index
        if (freeProc >= NPROC)
        {
            current->p_registers.reg_rax = -1;
            break;
        }

        // mark that free process as runnable
        processes[freeProc].p_state = P_RUNNABLE;
        processes[freeProc].p_pagetable = copy_pagetable(current->p_pagetable, processes[freeProc].p_pid);

        if (processes[freeProc].p_pagetable == NULL)
        {
            process_cleanup(freeProc);
            // process_cleanup(processes[freeProc].p_pid);
            current->p_registers.reg_rax = -1;
            break;
        }

        vamapping map;
        int counter = 0;
        // for each page between the bottom of the process heap and the bottom of the process stack
        for (uintptr_t i = 0; i < MEMSIZE_VIRTUAL; i += PAGESIZE)
        {

            map = virtual_memory_lookup(current->p_pagetable, i);

            if (map.pn > -1 && pageinfo[map.pn].owner >= 0) // >= 0 includes PO_FREE
            {
                // Permissions are read only, share space
                // can also be used as ((pageinfo[map.pn].owner != current_owner) && ((map.perm & PTE_U) == PTE_U) && ((map.perm & PTE_W) != PTE_W))
                if ((map.perm & (PTE_U | PTE_W)) == PTE_U)
                {
                    // update ref count
                    pageinfo[map.pn].refcount++;
                }
                // writable (not entirely true, we would need to actually change this for read_only write extra credit. RIP)
                else if ((map.perm & (PTE_U | PTE_W)) == (PTE_U | PTE_W))
                {
                    // Allocate page
                    x86_64_pagetable *freePage = find_free_page();

                    // if there is no free page return -1 to caller
                    if (freePage == NULL)
                    {
                        current->p_registers.reg_rax = -1;
                        process_cleanup_partial(processes[freeProc].p_pid, i);
                        returnEarly = 1;
                        break;
                    }
                    else
                    {

                        memcpy((void *)freePage, (void *)map.pa, PAGESIZE);
                        int r = virtual_memory_map(processes[freeProc].p_pagetable, i,
                                                   (uintptr_t)freePage, PAGESIZE, map.perm, find_free_page);
                        // check r for error and release the physical page and return error
                        if (r < 0)
                        {
                            current->p_registers.reg_rax = -1;
                            process_cleanup_partial(processes[freeProc].p_pid, i);
                            returnEarly = 1;
                            break;
                        }
                    }
                }
            }
        } // end of for loop

        if (returnEarly)
            break;
        // copy registers to forked process from parent
        memcpy((void *)&processes[freeProc].p_registers, (void *)&current->p_registers, sizeof(x86_64_registers));

        // parent returns child's pid
        current->p_registers.reg_rax = processes[freeProc].p_pid;

        // child process returns zero
        processes[freeProc].p_registers.reg_rax = 0;
        break;
    }
    // Step 7 Incomplete
    case INT_SYS_EXIT:
    {
        process_cleanup(current->p_pid);
        break;
    }

    default:
        panic("Unexpected exception %d!\n", reg->reg_intno);
        break; /* will not be reached */
    }

    // Return to the current process (or run something else).
    if (current->p_state == P_RUNNABLE)
    {
        run(current);
    }
    else
    {
        schedule();
    }
}

// schedule
//    Pick the next process to run and then run it.
//    If there are no runnable processes, spins forever.

void schedule(void)
{
    pid_t pid = current->p_pid;
    while (1)
    {
        pid = (pid + 1) % NPROC;
        if (processes[pid].p_state == P_RUNNABLE)
        {
            run(&processes[pid]);
        }
        // If Control-C was typed, exit the virtual machine.
        check_keyboard();
    }
}

// run(p)
//    Run process `p`. This means reloading all the registers from
//    `p->p_registers` using the `popal`, `popl`, and `iret` instructions.
//
//    As a side effect, sets `current = p`.

void run(proc *p)
{
    assert(p->p_state == P_RUNNABLE);
    current = p;

    // Load the process's current pagetable.
    set_pagetable(p->p_pagetable);

    // This function is defined in k-exception.S. It restores the process's
    // registers then jumps back to user mode.
    exception_return(&p->p_registers);

spinloop:
    goto spinloop; // should never get here
}

// pageinfo_init
//    Initialize the `pageinfo[]` array.

void pageinfo_init(void)
{
    extern char end[];

    for (uintptr_t addr = 0; addr < MEMSIZE_PHYSICAL; addr += PAGESIZE)
    {
        int owner;
        if (physical_memory_isreserved(addr))
        {
            owner = PO_RESERVED;
        }
        else if ((addr >= KERNEL_START_ADDR && addr < (uintptr_t)end) ||
                 addr == KERNEL_STACK_TOP - PAGESIZE)
        {
            owner = PO_KERNEL;
        }
        else
        {
            owner = PO_FREE;
        }
        pageinfo[PAGENUMBER(addr)].owner = owner;
        pageinfo[PAGENUMBER(addr)].refcount = (owner != PO_FREE);
    }
}

// check_page_table_mappings
//    Check operating system invariants about kernel mappings for page
//    table `pt`. Panic if any of the invariants are false.

void check_page_table_mappings(x86_64_pagetable *pt)
{
    extern char start_data[], end[];
    assert(PTE_ADDR(pt) == (uintptr_t)pt);

    // kernel memory is identity mapped; data is writable
    for (uintptr_t va = KERNEL_START_ADDR; va < (uintptr_t)end;
         va += PAGESIZE)
    {
        vamapping vam = virtual_memory_lookup(pt, va);
        if (vam.pa != va)
        {
            console_printf(CPOS(22, 0), 0xC000, "%p vs %p\n", va, vam.pa);
        }
        assert(vam.pa == va);
        if (va >= (uintptr_t)start_data)
        {
            assert(vam.perm & PTE_W);
        }
    }

    // kernel stack is identity mapped and writable
    uintptr_t kstack = KERNEL_STACK_TOP - PAGESIZE;
    vamapping vam = virtual_memory_lookup(pt, kstack);
    assert(vam.pa == kstack);
    assert(vam.perm & PTE_W);
}

// check_page_table_ownership
//    Check operating system invariants about ownership and reference
//    counts for page table `pt`. Panic if any of the invariants are false.

static void check_page_table_ownership_level(x86_64_pagetable *pt, int level,
                                             int owner, int refcount);

void check_page_table_ownership(x86_64_pagetable *pt, pid_t pid)
{
    // calculate expected reference count for page tables
    int owner = pid;
    int expected_refcount = 1;
    if (pt == kernel_pagetable)
    {
        owner = PO_KERNEL;
        for (int xpid = 0; xpid < NPROC; ++xpid)
        {
            if (processes[xpid].p_state != P_FREE && processes[xpid].p_pagetable == kernel_pagetable)
            {
                ++expected_refcount;
            }
        }
    }
    check_page_table_ownership_level(pt, 0, owner, expected_refcount);
}

static void check_page_table_ownership_level(x86_64_pagetable *pt, int level,
                                             int owner, int refcount)
{
    assert(PAGENUMBER(pt) < NPAGES);
    assert(pageinfo[PAGENUMBER(pt)].owner == owner);
    assert(pageinfo[PAGENUMBER(pt)].refcount == refcount);
    if (level < 3)
    {
        for (int index = 0; index < NPAGETABLEENTRIES; ++index)
        {
            if (pt->entry[index])
            {
                x86_64_pagetable *nextpt =
                    (x86_64_pagetable *)PTE_ADDR(pt->entry[index]);
                check_page_table_ownership_level(nextpt, level + 1, owner, 1);
            }
        }
    }
}

// check_virtual_memory
//    Check operating system invariants about virtual memory. Panic if any
//    of the invariants are false.

void check_virtual_memory(void)
{
    // Process 0 must never be used.
    assert(processes[0].p_state == P_FREE);

    // The kernel page table should be owned by the kernel;
    // its reference count should equal 1, plus the number of processes
    // that don't have their own page tables.
    // Active processes have their own page tables. A process page table
    // should be owned by that process and have reference count 1.
    // All level-2-4 page tables must have reference count 1.

    check_page_table_mappings(kernel_pagetable);
    check_page_table_ownership(kernel_pagetable, -1);

    for (int pid = 0; pid < NPROC; ++pid)
    {
        if (processes[pid].p_state != P_FREE && processes[pid].p_pagetable != kernel_pagetable)
        {
            check_page_table_mappings(processes[pid].p_pagetable);
            check_page_table_ownership(processes[pid].p_pagetable, pid);
        }
    }

    // Check that all referenced pages refer to active processes
    for (int pn = 0; pn < PAGENUMBER(MEMSIZE_PHYSICAL); ++pn)
    {
        if (pageinfo[pn].refcount > 0 && pageinfo[pn].owner >= 0)
        {
            if (processes[pageinfo[pn].owner].p_state == P_FREE)
            {
                log_printf("-- page # is %d\n--refcount is %d\n--owner is: %d\n--pstate is %d\n",
                           pn, pageinfo[pn].refcount, pageinfo[pn].owner,
                           processes[pageinfo[pn].owner].p_state);
            }
            assert(processes[pageinfo[pn].owner].p_state != P_FREE);
        }
    }
}

// memshow_physical
//    Draw a picture of physical memory on the CGA console.

static const uint16_t memstate_colors[] = {
    'K' | 0x0D00, 'R' | 0x0700, '.' | 0x0700, '1' | 0x0C00,
    '2' | 0x0A00, '3' | 0x0900, '4' | 0x0E00, '5' | 0x0F00,
    '6' | 0x0C00, '7' | 0x0A00, '8' | 0x0900, '9' | 0x0E00,
    'A' | 0x0F00, 'B' | 0x0C00, 'C' | 0x0A00, 'D' | 0x0900,
    'E' | 0x0E00, 'F' | 0x0F00};

void memshow_physical(void)
{
    console_printf(CPOS(0, 32), 0x0F00, "PHYSICAL MEMORY");
    for (int pn = 0; pn < PAGENUMBER(MEMSIZE_PHYSICAL); ++pn)
    {
        if (pn % 64 == 0)
        {
            console_printf(CPOS(1 + pn / 64, 3), 0x0F00, "0x%06X ", pn << 12);
        }

        int owner = pageinfo[pn].owner;
        if (pageinfo[pn].refcount == 0)
        {
            owner = PO_FREE;
        }
        uint16_t color = memstate_colors[owner - PO_KERNEL];
        // darker color for shared pages
        if (pageinfo[pn].refcount > 1)
        {
            color &= 0x77FF;
        }

        console[CPOS(1 + pn / 64, 12 + pn % 64)] = color;
    }
}

// memshow_virtual(pagetable, name)
//    Draw a picture of the virtual memory map `pagetable` (named `name`) on
//    the CGA console.

void memshow_virtual(x86_64_pagetable *pagetable, const char *name)
{
    assert((uintptr_t)pagetable == PTE_ADDR(pagetable));

    console_printf(CPOS(10, 26), 0x0F00, "VIRTUAL ADDRESS SPACE FOR %s", name);
    for (uintptr_t va = 0; va < MEMSIZE_VIRTUAL; va += PAGESIZE)
    {
        vamapping vam = virtual_memory_lookup(pagetable, va);
        uint16_t color;
        if (vam.pn < 0)
        {
            color = ' ';
        }
        else
        {
            assert(vam.pa < MEMSIZE_PHYSICAL);
            int owner = pageinfo[vam.pn].owner;
            if (pageinfo[vam.pn].refcount == 0)
            {
                owner = PO_FREE;
            }
            color = memstate_colors[owner - PO_KERNEL];
            // reverse video for user-accessible pages
            if (vam.perm & PTE_U)
            {
                color = ((color & 0x0F00) << 4) | ((color & 0xF000) >> 4) | (color & 0x00FF);
            }
            // darker color for shared pages
            if (pageinfo[vam.pn].refcount > 1)
            {
                color &= 0x77FF;
            }
        }
        uint32_t pn = PAGENUMBER(va);
        if (pn % 64 == 0)
        {
            console_printf(CPOS(11 + pn / 64, 3), 0x0F00, "0x%06X ", va);
        }
        console[CPOS(11 + pn / 64, 12 + pn % 64)] = color;
    }
}

// memshow_virtual_animate
//    Draw a picture of process virtual memory maps on the CGA console.
//    Starts with process 1, then switches to a new process every 0.25 sec.

void memshow_virtual_animate(void)
{
    static unsigned last_ticks = 0;
    static int showing = 1;

    // switch to a new process every 0.25 sec
    if (last_ticks == 0 || ticks - last_ticks >= HZ / 2)
    {
        last_ticks = ticks;
        ++showing;
    }

    // the current process may have died -- don't display it if so
    while (showing <= 2 * NPROC && processes[showing % NPROC].p_state == P_FREE)
    {
        ++showing;
    }
    showing = showing % NPROC;

    if (processes[showing].p_state != P_FREE)
    {
        char s[4];
        snprintf(s, 4, "%d ", showing);
        memshow_virtual(processes[showing].p_pagetable, s);
    }
}
