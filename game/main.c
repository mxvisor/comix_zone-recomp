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
 * the thread the event loop starts for it.  Here: the report of a crash.
 * (The data at its original addresses is SR-SDK's Game_guestdata.c.)
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

static void Comix_SetupCrashReport(void)
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
    Comix_SetupCrashReport();
#if defined(__ANDROID__)
    {
        const char *external = SDL_AndroidGetExternalStoragePath();

        if (external != NULL)
        {
            snprintf(comix_crash_path, sizeof(comix_crash_path), "%s/crash-native.txt", external);
        }
    }
#endif
}
