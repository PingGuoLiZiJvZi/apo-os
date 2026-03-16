#include "proc.h"
#include "elf.h"
#include "../libc/stdio.h"
#include "../libc/string.h"

#define Elf_Ehdr Elf64_Ehdr
#define Elf_Phdr Elf64_Phdr
#define EXPECTED_TYPE EM_RISCV

// Simple assert — panics on failure
#define assert(cond) do { if (!(cond)) panic("assert fail: " #cond); } while(0)

static char program_buf[0x80000]; // 512 KB buffer for loading ELF

// Load an ELF binary from the filesystem into the process address space.
// Returns the entry point virtual address.
static uintptr_t loader(PCB *pcb, const char *filename) {
    printf("loader: Loading program '%s'...\n", filename);

    File *f = fs_open(filename);
    assert(f != 0);

    uint32_t file_size = fs_filesize(f);
    assert(file_size > 0 && file_size < sizeof(program_buf));

    fs_seek(f, 0);
    fs_read(f, program_buf, file_size);
    fs_close(f);

    Elf_Ehdr *ehdr = (Elf_Ehdr *)program_buf;
    assert(ehdr->e_ident[0] == ELFMAG0 &&
           ehdr->e_ident[1] == ELFMAG1 &&
           ehdr->e_ident[2] == ELFMAG2 &&
           ehdr->e_ident[3] == ELFMAG3);
    assert(ehdr->e_machine == EXPECTED_TYPE);

    uint64_t program_off = ehdr->e_phoff;
    uint16_t program_num = ehdr->e_phnum;
    assert(program_off > 0 && program_num > 0);

    uintptr_t max_brk = 0;
    for (uint16_t i = 0; i < program_num; i++) {
        Elf_Phdr *phdr = (Elf_Phdr *)(program_buf + program_off + i * sizeof(Elf_Phdr));
        if (phdr->p_type != PT_LOAD)
            continue;

        uintptr_t vaddr  = phdr->p_vaddr;
        uintptr_t paddr  = vaddr;
        uint64_t  memsz  = phdr->p_memsz;
        uint64_t  filesz = phdr->p_filesz;
        uint64_t  offset = phdr->p_offset;
        assert(memsz >= filesz);

        if (max_brk < vaddr + memsz)
            max_brk = vaddr + memsz;

        uintptr_t aligned_addr = paddr & ~((uintptr_t)PGSIZE - 1);
        uintptr_t zero_bytes   = paddr - aligned_addr;
        uintptr_t avail_bytes  = PGSIZE - zero_bytes;

        for (uintptr_t addr = aligned_addr; addr < paddr + memsz; addr += PGSIZE) {
            void *page = new_page(1);
            map(&pcb->as, (void *)addr, page, 0);

            if (zero_bytes) {
                if (filesz < avail_bytes)
                    avail_bytes = filesz;
                memcpy((char *)page + zero_bytes, program_buf + offset, avail_bytes);
                offset += avail_bytes;
                filesz -= avail_bytes;
                zero_bytes = 0;
                continue;
            }

            if (filesz >= PGSIZE) {
                memcpy(page, program_buf + offset, PGSIZE);
                filesz -= PGSIZE;
                offset += PGSIZE;
            } else if (filesz > 0) {
                memcpy(page, program_buf + offset, filesz);
                offset += filesz;
                filesz = 0;
            }
        }
    }

    pcb->max_brk = ROUNDUP(max_brk, PGSIZE);
    return ehdr->e_entry;
}


void naive_uload(PCB *pcb, const char *filename) {
    uintptr_t entry = loader(pcb, filename);
    printf("loader: Jump to entry = %p\n", (void *)entry);
    ((void (*)(void))entry)();
}


void context_uload(PCB *pcb, char *filename, const char *argv[], const char *envp[]) {
    protect(&pcb->as);

    int argc = 0;
    while (argv[argc] != 0) argc++;
    int envc = 0;
    while (envp[envc] != 0) envc++;

    int str_size = 0;
    for (int i = 0; i < argc; i++)
        str_size += strlen(argv[i]) + 1;
    for (int i = 0; i < envc; i++)
        str_size += strlen(envp[i]) + 1;

    // Total size needed for argc + argv[] + envp[] + strings on stack
    size_t args_total = sizeof(int)
                      + (argc + 1 + envc + 1) * sizeof(char *)
                      + str_size;
    if (args_total > PGSIZE)
        panic("context_uload: argc/argv/envp total size exceeds one page");

    // Allocate 8 pages for user stack (one at a time, no contiguity needed)
    void *stack_phys[8];
    uintptr_t v_stack_bottom = (uintptr_t)pcb->as.end;
    uintptr_t v_stack_top    = v_stack_bottom - 8 * PGSIZE;

    assert(v_stack_bottom % PGSIZE == 0);
    for (int i = 0; i < 8; i++) {
        stack_phys[i] = new_page(1);
        map(&pcb->as,
            (void *)(v_stack_top + i * PGSIZE),
            stack_phys[i], 0);
    }

    // Build argc/argv/envp on the last physical page
    // (stack grows downward from v_stack_bottom, so args land in page 7)
    char *last_page = (char *)stack_phys[7];
    char *sp = last_page + PGSIZE - args_total;

    // Corresponding user virtual address of sp
    uintptr_t v_sp = v_stack_bottom - args_total;

    *(int *)sp = argc;
    char *arg_area = sp + sizeof(int);
    char *strpos   = arg_area + (argc + 1 + envc + 1) * sizeof(char *);
    // Virtual address tracking for string pointers stored in argv/envp
    uintptr_t v_strpos = v_sp + sizeof(int)
                       + (argc + 1 + envc + 1) * sizeof(char *);

    for (int i = 0; i < argc; i++) {
        *(char **)(arg_area + i * sizeof(char *)) = (char *)v_strpos;
        strcpy(strpos, argv[i]);
        size_t len = strlen(argv[i]) + 1;
        strpos   += len;
        v_strpos += len;
    }
    *(char **)(arg_area + argc * sizeof(char *)) = 0;

    for (int i = 0; i < envc; i++) {
        *(char **)(arg_area + (argc + 1 + i) * sizeof(char *)) = (char *)v_strpos;
        strcpy(strpos, envp[i]);
        size_t len = strlen(envp[i]) + 1;
        strpos   += len;
        v_strpos += len;
    }
    *(char **)(arg_area + (argc + 1 + envc) * sizeof(char *)) = 0;

    // Load ELF
    uintptr_t entry = loader(pcb, filename);
    printf("loader: Program '%s' entry = %p\n", filename, (void *)entry);

    // Create user context
    Context *ctx = ucontext(&pcb->as,
        (Area){pcb->stack, pcb->stack + sizeof(pcb->stack)},
        (void (*)(void))entry);
    ctx->GPRx = v_sp;  // user SP (virtual)
    pcb->cp = ctx;
    pcb->proc_state = RUNNING_PROC;

    printf("loader: User context at %p, SP(virt)=%p\n",
           (void *)ctx, (void *)v_sp);
}

// Create a kernel-mode context with the given entry function and argument.
void context_kload(PCB *pcb, void (*entry)(void *), void *arg) {
    Context *ctx = kcontext(
        (Area){pcb->stack, pcb->stack + sizeof(pcb->stack)},
        entry, arg);
    pcb->cp = ctx;
    pcb->proc_state = RUNNING_PROC;
    printf("loader: Kernel context at %p, entry=%p\n", (void *)ctx, (void *)entry);
}