/**
 *
 *  Copyright (C) 2026 mxvisor
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy of
 *  this software and associated documentation files (the "Software"), to deal in
 *  the Software without restriction, including without limitation the rights to
 *  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 *  the Software, and to permit persons to whom the Software is furnished to do so,
 *  subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 *  FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 *  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 *  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

/* The port of Comix Zone: the frame (main, the window, the audio) is
 * SR-SDK's Game_winmain.c; the game's own startup code (the entry point of
 * COMIX.EXE, the alias main_ of global_aliases.sci) is recompiled and runs in
 * the thread the event loop starts for it.  Here: the memory the game expects.
 *
 * The game draws through the WinG API (game/WinApi-wing.c), which owns the
 * window and the texture: the port itself keeps no picture of its own, and
 * Game_Display_Flip() has nothing to do. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <inttypes.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <ucontext.h>
#include <sys/mman.h>
#if defined(__ANDROID__)
#include <dlfcn.h>
#endif
#endif

#include <SDL.h>

#include "Game_defs.h"
#include "Game_memory.h"
#include "Game_hooks.h"
#include "Game_runtime.h"
#include "Game_thread.h"
#include "main.h"
#include "input.h"
#include "audio.h"
#include "WinApi.h"
#include "WinApi-kernel32.h"

extern uint64_t pointer_offset;

#ifndef _WIN32
#if defined(__ANDROID__)
static char comix_crash_path[1024];

static void Comix_CrashSymbol(FILE *f, uintptr_t value)
{
    Dl_info info;

    if (value != 0 && dladdr((void *)value, &info) != 0 && info.dli_fbase != NULL)
    {
        fprintf(f, "  %p %s+0x%" PRIxPTR "\n", (void *)value, info.dli_fname,
                value - (uintptr_t)info.dli_fbase);
    }
    else
    {
        fprintf(f, "  %p ?\n", (void *)value);
    }
}
#endif

static void Comix_SegvHandler(int sig, siginfo_t *si, void *uc)
{
    uintptr_t addr = (uintptr_t)si->si_addr;
    ucontext_t *context = (ucontext_t *)uc;

    /* a jump to an address outside of the code is not a read of zeros */
#if defined(__x86_64__)
    uintptr_t pc = (uintptr_t)context->uc_mcontext.gregs[REG_RIP];
#elif defined(__aarch64__)
    uintptr_t pc = (uintptr_t)context->uc_mcontext.pc;
#else
    uintptr_t pc = 0;
#endif
    if (pc == addr)
    {
        fprintf(stderr, GAME_NAME ": the guest jumped to %" PRIxPTR " (guest address %" PRIxPTR ")\n",
                addr, addr - (uintptr_t)pointer_offset);
    }

#if defined(__ANDROID__)
    if (comix_crash_path[0] != 0)
    {
        FILE *f = fopen(comix_crash_path, "a");

        if (f != NULL)
        {
            fprintf(f, "signal %d at %p pc %p pointer_offset %p\n",
                    sig, (void *)addr, (void *)pc, (void *)(uintptr_t)pointer_offset);
            Comix_CrashSymbol(f, (uintptr_t)&Comix_SegvHandler);
            Comix_CrashSymbol(f, pc);
#if defined(__aarch64__)
            {
                int i;

                for (i = 0; i < 31; i++)
                    fprintf(f, "x%02d %016" PRIx64 "\n", i, (uint64_t)context->uc_mcontext.regs[i]);
                fprintf(f, "sp  %016" PRIx64 "\n", (uint64_t)context->uc_mcontext.sp);
                fprintf(f, "pc  %016" PRIx64 "\n", (uint64_t)context->uc_mcontext.pc);
                Comix_CrashSymbol(f, (uintptr_t)context->uc_mcontext.regs[30]);
            }
#endif
            fclose(f);
        }
    }
#endif

    signal(sig, SIG_DFL);
    raise(sig);
}
#endif

/* The game reaches its data at the addresses the data had in Windows 95:
 * llasm puts the data in the guest's memory (llasm_copy_guest_data), so the
 * port maps the range the data needs (llasm_guest_data_range) before the
 * guest starts. */
extern "C" {
extern uint32_t llasm_resolve_guest_code(uint32_t address, uint64_t ptrofs);
extern void llasm_guest_data_range(uint32_t *range);
extern void llasm_copy_guest_data(uint64_t ptrofs);
}

/* "jmp reg" with an address of the original program's code (the lists of
 * game/recomp/.../guest_address_jumps.sci): the generated code's pointers are
 * not the addresses of the original program, llasm tabulates them. */
extern "C" uint32_t x86_resolve_guest_code(uint32_t address)
{
    uint32_t target = llasm_resolve_guest_code(address, pointer_offset);

    if (target == 0)
    {
        fprintf(stderr, GAME_NAME ": no code at the guest address %" PRIx32 "\n", address);
        abort();
    }

    return target;
}

/* "add reg, offset" with a code pointer in the register (the lists of
 * guest_address_jumps.sci): the program starts a task at the address of a
 * procedure plus the size of the procedure's head.  The code pointers are not
 * the addresses of the original program, so the pointer is turned back into
 * the address, the offset is added, and the code at the new address is the
 * result (the address is a "target" of the list). */
typedef struct { uint32_t pointer; uint32_t address; } Comix_CodePointer;

static Comix_CodePointer *comix_code_pointers = NULL;
static uint32_t comix_code_pointer_count = 0;

static int Comix_CompareCodePointers(const void *a, const void *b)
{
    uint32_t pa = ((const Comix_CodePointer *)a)->pointer, pb = ((const Comix_CodePointer *)b)->pointer;

    return (pa > pb) - (pa < pb);
}

extern "C" uint32_t llasm_guest_code_count(void);
extern "C" uint32_t llasm_guest_code_va(uint32_t index);

extern "C" uint32_t x86_guest_code_add(uint32_t pointer, uint32_t offset)
{
    Comix_CodePointer key, *found;
    uint32_t target;

    if (comix_code_pointers == NULL)
    {
        uint32_t i, count = llasm_guest_code_count();

        comix_code_pointers = (Comix_CodePointer *)malloc(count * sizeof(Comix_CodePointer));
        for (i = 0; i < count; i++)
        {
            comix_code_pointers[i].address = llasm_guest_code_va(i);
            comix_code_pointers[i].pointer = llasm_resolve_guest_code(comix_code_pointers[i].address, pointer_offset);
        }
        qsort(comix_code_pointers, count, sizeof(Comix_CodePointer), Comix_CompareCodePointers);
        comix_code_pointer_count = count;
    }

    key.pointer = pointer;
    found = (Comix_CodePointer *)bsearch(&key, comix_code_pointers, comix_code_pointer_count,
                                         sizeof(Comix_CodePointer), Comix_CompareCodePointers);
    if (found == NULL)
    {
        fprintf(stderr, GAME_NAME ": no procedure at the code pointer %" PRIx32 "\n", pointer);
        abort();
    }

    target = llasm_resolve_guest_code(found->address + offset, pointer_offset);
    if (target == 0)
    {
        fprintf(stderr, GAME_NAME ": no code at the guest address %" PRIx32 " + %" PRIx32 "\n", found->address, offset);
        abort();
    }

    return target;
}

/* Maps the guest's memory: zeros, over whatever the runtime has reserved there. */
static int Comix_MapGuestMemory(uintptr_t address, size_t size)
{
#ifdef _WIN32
    /* the part the runtime reserved is committed, the rest is reserved and committed */
    if (VirtualAlloc((void *)address, size, MEM_COMMIT, PAGE_READWRITE) != NULL)
    {
        return 0;
    }

    return (VirtualAlloc((void *)address, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE) != NULL) ? 0 : -1;
#else
    /* MAP_FIXED: the runtime reserves the area of the guest's memory
     * (PROT_NONE) for the guest's heap, and the game needs the low part of
     * it for its own data */
    void *mem = mmap((void *)address, size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);

    return (mem == MAP_FAILED) ? -1 : 0;
#endif
}

/* how much of the guest's memory the data of the game needs (Game_memory.c) */
extern "C" unsigned int Comix_GuestLowMemory(void)
{
    uint32_t range[2] = {0, 0};

    llasm_guest_data_range(range);

    return (unsigned int)((range[1] > 0x110000) ? range[1] : 0x110000) + 0x10000;
}

static void Comix_CopyGuestData(void)
{
    uint32_t range[2] = {0, 0};
    size_t size;

    llasm_guest_data_range(range);

    /* The game was a 16-bit program in a 32-bit executable: it uses addresses
     * of the low memory of Windows 95 as well, so the low area of the guest's
     * address space is mapped (with zeros) and the data of the game is
     * copied into it (llasm_copy_guest_data).  The whole gigabyte on Linux
     * (the memory is not committed); on Windows and Android only the data and
     * a margin: on Android the runtime has reserved the guest region and hands
     * its arenas out of it, so a gigabyte mapping here would be overwritten by
     * the first arenas. */
#if defined(_WIN32) || defined(__ANDROID__)
    size = (size_t)Comix_GuestLowMemory();
#else
    size = 0x40000000;
    if (range[1] > 0x40000000)
    {
        size = range[1];
    }
#endif

    if (Comix_MapGuestMemory((uintptr_t)pointer_offset, size) != 0)
    {
        fprintf(stderr, GAME_NAME ": could not map the guest's memory at %" PRIxPTR "\n", (uintptr_t)pointer_offset);
        return;
    }

#if defined(__ANDROID__)
    Game_MemorySetGuestNext((uint64_t)size);
#endif
    llasm_copy_guest_data(pointer_offset);
}

#ifdef _WIN32
/* An exception in the guest's code (an access violation, an illegal
 * instruction) that nothing handles is written to comix-zone-crash.txt in the
 * data directory, and the process ends: without it Windows shows a dialog and
 * the window stays frozen.  It is the unhandled exception filter, not a
 * vectored handler: that one sees every first-chance exception too, also those
 * the libraries (and Wine) raise and handle by themselves. */
static LONG WINAPI Comix_ExceptionHandler(EXCEPTION_POINTERS *ep)
{
    FILE *f;
    CONTEXT *c = ep->ContextRecord;
    EXCEPTION_RECORD *r = ep->ExceptionRecord;

    if (r->ExceptionCode == DBG_PRINTEXCEPTION_C || r->ExceptionCode == 0x406D1388 ||
        r->ExceptionCode == DBG_PRINTEXCEPTION_WIDE_C || r->ExceptionCode < 0x80000000u ||
        r->ExceptionCode == EXCEPTION_BREAKPOINT || r->ExceptionCode == EXCEPTION_SINGLE_STEP)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    f = fopen("comix-zone-crash.txt", "w");
    if (f != NULL)
    {
        fprintf(f, "exception %08lx at %p (guest rip %" PRIxPTR "), pointer_offset %" PRIxPTR "\n",
                (unsigned long)r->ExceptionCode, r->ExceptionAddress,
                (uintptr_t)r->ExceptionAddress - (uintptr_t)pointer_offset, (uintptr_t)pointer_offset);
        if (r->NumberParameters >= 2)
        {
            fprintf(f, "access type %" PRIuPTR " address %" PRIxPTR " (guest %" PRIxPTR ")\n",
                    (uintptr_t)r->ExceptionInformation[0], (uintptr_t)r->ExceptionInformation[1],
                    (uintptr_t)r->ExceptionInformation[1] - (uintptr_t)pointer_offset);
        }
        fprintf(f, "rax %016llx rbx %016llx rcx %016llx rdx %016llx\n", c->Rax, c->Rbx, c->Rcx, c->Rdx);
        fprintf(f, "rsi %016llx rdi %016llx rbp %016llx rsp %016llx\n", c->Rsi, c->Rdi, c->Rbp, c->Rsp);
        fprintf(f, "r8  %016llx r9  %016llx r10 %016llx r11 %016llx\n", c->R8, c->R9, c->R10, c->R11);
        fprintf(f, "r12 %016llx r13 %016llx r14 %016llx r15 %016llx\n", c->R12, c->R13, c->R14, c->R15);

        /* the host code that was running: the words of the stack that point into
         * a module (a guess at the call chain) */
        {
            const uint64_t *sp = (const uint64_t *)c->Rsp;
            int i, found = 0;
            char name[MAX_PATH];

            fprintf(f, "stack:\n");
            for (i = 0; i < 512 && found < 24; i++)
            {
                MEMORY_BASIC_INFORMATION mbi;
                uint64_t value;

                if (VirtualQuery(sp + i, &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT ||
                    (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
                {
                    break;
                }
                value = sp[i];
                if (VirtualQuery((const void *)(uintptr_t)value, &mbi, sizeof(mbi)) == 0 || mbi.Type != MEM_IMAGE ||
                    (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0)
                {
                    continue;
                }
                if (GetModuleFileNameA((HMODULE)mbi.AllocationBase, name, sizeof(name)) == 0) strcpy(name, "?");
                fprintf(f, "  +%03x %s+%" PRIx64 "\n", i * 8, name, value - (uint64_t)(uintptr_t)mbi.AllocationBase);
                found++;
            }
        }
        fclose(f);
    }

    ExitProcess(3);
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static void Comix_SetupMemory(void)
{
#ifdef _WIN32
    SetUnhandledExceptionFilter(Comix_ExceptionHandler);
#else
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = Comix_SegvHandler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
#endif
}

/* GAME_WIN_START (Game_hooks.h): the winapi layer is up, the game is not
 * running yet */
void Comix_Start(void)
{
#if defined(__ANDROID__)
    setenv("CZ_MEM_LOG", "1", 1);
#endif
    Comix_SetupMemory();
#if defined(__ANDROID__)
    {
        const char *external = SDL_AndroidGetExternalStoragePath();

        if (external != NULL)
        {
            snprintf(comix_crash_path, sizeof(comix_crash_path), "%s/crash-native.txt", external);
        }
    }
#endif
    Comix_CopyGuestData();
}
