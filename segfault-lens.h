/**
 * segfault-lens.h — The Instant Crash Diagnostics & Panic Screen for C and C++
 *
 * Catches SIGSEGV, Access Violations (0xC0000005), Div-by-Zero, Stack Overflows,
 * and Aborts. Prints a colorized, human-readable Rust-like panic screen with
 * exact file names, source lines, function names, faulting address, and CPU registers.
 *
 * Usage:
 *   #define SEGFAULT_LENS_IMPLEMENTATION
 *   #include "segfault-lens.h"
 *
 *   int main(void) {
 *       segfault_lens_init(); // Installs crash handlers
 *       ...
 *   }
 *
 * Zero external dependencies.
 * Windows: Auto-links dbghelp.lib on MSVC
 * Linux/macOS: Standard POSIX / libc
 */

#ifndef SEGFAULT_LENS_H
#define SEGFAULT_LENS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializes the crash handler.
 * Safe to call multiple times (idempotent).
 */
void segfault_lens_init(void);

/**
 * Removes the crash handler and restores default system behavior.
 */
void segfault_lens_uninstall(void);

#ifdef __cplusplus
}
#endif

#endif /* SEGFAULT_LENS_H */

#ifdef SEGFAULT_LENS_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <dbghelp.h>
  #if defined(_MSC_VER)
    #pragma comment(lib, "dbghelp.lib")
  #endif
#else
  #define _GNU_SOURCE
  #include <signal.h>
  #include <unistd.h>
  #include <execinfo.h>
  #include <dlfcn.h>
  #include <ucontext.h>
  #include <sys/types.h>
#endif

/* Reentrancy guard to avoid recursive crashes inside the handler */
static volatile int g_segfault_lens_active = 0;
static void* g_segfault_lens_win_handler = NULL;

/* ANSI Color codes */
#define SFL_RED     "\033[1;31m"
#define SFL_GREEN   "\033[1;32m"
#define SFL_YELLOW  "\033[1;33m"
#define SFL_BLUE    "\033[1;34m"
#define SFL_MAGENTA "\033[1;35m"
#define SFL_CYAN    "\033[1;36m"
#define SFL_WHITE   "\033[1;37m"
#define SFL_DIM     "\033[2m"
#define SFL_RESET   "\033[0m"

static void sfl_enable_ansi_windows(void) {
#if defined(_WIN32) || defined(_WIN64)
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hOut, &dwMode)) {
            dwMode |= 0x0004; /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */
            SetConsoleMode(hOut, dwMode);
        }
    }
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != INVALID_HANDLE_VALUE) {
        DWORD dwMode = 0;
        if (GetConsoleMode(hErr, &dwMode)) {
            dwMode |= 0x0004;
            SetConsoleMode(hErr, dwMode);
        }
    }
#endif
}

static const char* sfl_get_crash_cause(uint32_t code, uintptr_t fault_addr, uintptr_t violation_type) {
#if defined(_WIN32) || defined(_WIN64)
    switch (code) {
        case 0xC0000005: /* STATUS_ACCESS_VIOLATION */
            if (fault_addr == 0 || fault_addr < 0x1000) {
                return (violation_type == 1) ? "Null Pointer Write Dereference" : "Null Pointer Read Dereference";
            }
            if (violation_type == 1) return "Write Access Violation (Invalid Pointer / Buffer Overflow)";
            if (violation_type == 8) return "Data Execution Prevention (DEP) Violation";
            return "Read Access Violation (Use-After-Free / Invalid Pointer)";
        case 0xC0000094: /* STATUS_INTEGER_DIVIDE_BY_ZERO */
            return "Integer Divide by Zero";
        case 0xC000008E: /* STATUS_FLOAT_DIVIDE_BY_ZERO */
            return "Floating Point Divide by Zero";
        case 0xC00000FD: /* STATUS_STACK_OVERFLOW */
            return "Stack Overflow (Infinite Recursion / Large Stack Alloc)";
        case 0xC000001D: /* STATUS_ILLEGAL_INSTRUCTION */
            return "Illegal CPU Instruction (Corrupted Code / Unsupported ISA)";
        case 0xC0000025: /* STATUS_NONCONTINUABLE_EXCEPTION */
            return "Non-continuable Exception / Fatal Abort";
        default:
            return "Fatal Hardware / System Exception";
    }
#else
    switch (code) {
        case SIGSEGV:
            if (fault_addr == 0 || fault_addr < 0x1000) {
                return "Null Pointer Dereference (SIGSEGV)";
            }
            return "Invalid Memory Access / Segfault (SIGSEGV)";
        case SIGFPE:
            return "Arithmetic Error / Division by Zero (SIGFPE)";
        case SIGILL:
            return "Illegal Instruction (SIGILL)";
        case SIGBUS:
            return "Bus Error / Alignment Fault (SIGBUS)";
        case SIGABRT:
            return "Process Aborted / assert() failed (SIGABRT)";
        default:
            return "Fatal POSIX Signal";
    }
#endif
}

#if defined(_WIN32) || defined(_WIN64)
#include <malloc.h>

static LONG WINAPI sfl_win_vectored_handler(PEXCEPTION_POINTERS pExcInfo) {
    DWORD code = pExcInfo->ExceptionRecord->ExceptionCode;

    /* Filter only fatal hardware/process crash exceptions */
    if (code != 0xC0000005 && /* ACCESS_VIOLATION */
        code != 0xC0000094 && /* INT_DIVIDE_BY_ZERO */
        code != 0xC000008E && /* FLOAT_DIVIDE_BY_ZERO */
        code != 0xC00000FD && /* STACK_OVERFLOW */
        code != 0xC000001D && /* ILLEGAL_INSTRUCTION */
        code != 0xC0000025) { /* NONCONTINUABLE */
        return EXCEPTION_CONTINUE_SEARCH;
    }

    /* In case of stack overflow, repair stack guard page to permit execution */
    if (code == 0xC00000FD) {
        _resetstkoflw();
    }

    /* Reentrancy prevention */
    if (InterlockedCompareExchange((volatile LONG*)&g_segfault_lens_active, 1, 0) != 0) {
        static const char msg[] = "\n[segfault-lens] Double fault detected inside crash handler! Terminating.\n";
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, (DWORD)sizeof(msg) - 1, &written, NULL);
        ExitProcess(code);
    }

    sfl_enable_ansi_windows();

    uintptr_t violation_type = 0;
    uintptr_t fault_addr = 0;
    if (code == 0xC0000005 && pExcInfo->ExceptionRecord->NumberParameters >= 2) {
        violation_type = (uintptr_t)pExcInfo->ExceptionRecord->ExceptionInformation[0];
        fault_addr = (uintptr_t)pExcInfo->ExceptionRecord->ExceptionInformation[1];
    } else {
        fault_addr = (uintptr_t)pExcInfo->ExceptionRecord->ExceptionAddress;
    }

    const char* cause_str = sfl_get_crash_cause(code, fault_addr, violation_type);

    fprintf(stderr, "\n");
    fprintf(stderr, SFL_RED "================================================================================\n" SFL_RESET);
    fprintf(stderr, SFL_RED "  💥 CRASH DETECTED by segfault-lens\n" SFL_RESET);
    fprintf(stderr, SFL_RED "================================================================================\n" SFL_RESET);
    fprintf(stderr, SFL_WHITE "  Exception Code  : " SFL_YELLOW "0x%08X\n" SFL_RESET, (unsigned int)code);
    fprintf(stderr, SFL_WHITE "  Diagnosis       : " SFL_RED "%s\n" SFL_RESET, cause_str);
    fprintf(stderr, SFL_WHITE "  Fault Address   : " SFL_CYAN "0x%016llX" SFL_RESET "\n", (unsigned long long)fault_addr);
    fprintf(stderr, SFL_WHITE "  PC / Instruction: " SFL_CYAN "0x%016llX" SFL_RESET "\n", (unsigned long long)pExcInfo->ExceptionRecord->ExceptionAddress);

    /* Dump Registers on x64 */
#if defined(_M_X64) || defined(__x86_64__)
    PCONTEXT ctx = pExcInfo->ContextRecord;
    fprintf(stderr, "\n" SFL_MAGENTA "  CPU Registers (x86_64):\n" SFL_RESET);
    fprintf(stderr, SFL_DIM "    RIP: 0x%016llX   RSP: 0x%016llX   RBP: 0x%016llX\n" SFL_RESET,
            (unsigned long long)ctx->Rip, (unsigned long long)ctx->Rsp, (unsigned long long)ctx->Rbp);
    fprintf(stderr, SFL_DIM "    RAX: 0x%016llX   RBX: 0x%016llX   RCX: 0x%016llX\n" SFL_RESET,
            (unsigned long long)ctx->Rax, (unsigned long long)ctx->Rbx, (unsigned long long)ctx->Rcx);
    fprintf(stderr, SFL_DIM "    RDX: 0x%016llX   RSI: 0x%016llX   RDI: 0x%016llX\n" SFL_RESET,
            (unsigned long long)ctx->Rdx, (unsigned long long)ctx->Rsi, (unsigned long long)ctx->Rdi);
    fprintf(stderr, SFL_DIM "    R8 : 0x%016llX   R9 : 0x%016llX   R10: 0x%016llX\n" SFL_RESET,
            (unsigned long long)ctx->R8, (unsigned long long)ctx->R9, (unsigned long long)ctx->R10);
    fprintf(stderr, SFL_DIM "    R11: 0x%016llX   R12: 0x%016llX   R13: 0x%016llX\n" SFL_RESET,
            (unsigned long long)ctx->R11, (unsigned long long)ctx->R12, (unsigned long long)ctx->R13);
#endif

    /* Stack Trace Walk using DbgHelp */
    fprintf(stderr, "\n" SFL_GREEN "  Stack Trace:\n" SFL_RESET);

    HANDLE hProcess = GetCurrentProcess();
    HANDLE hThread = GetCurrentThread();

    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
    SymInitialize(hProcess, NULL, TRUE);

    CONTEXT walkContext = *pExcInfo->ContextRecord;
    STACKFRAME64 frame;
    memset(&frame, 0, sizeof(frame));

#if defined(_M_X64) || defined(__x86_64__)
    DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = walkContext.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = walkContext.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = walkContext.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_IX86) || defined(__i386__)
    DWORD machineType = IMAGE_FILE_MACHINE_I386;
    frame.AddrPC.Offset = walkContext.Eip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = walkContext.Ebp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = walkContext.Esp;
    frame.AddrStack.Mode = AddrModeFlat;
#else
    DWORD machineType = 0;
#endif

    int frameIndex = 0;
    while (machineType != 0 && StackWalk64(machineType, hProcess, hThread, &frame, &walkContext,
                                          NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
        if (frame.AddrPC.Offset == 0) break;

        DWORD64 displacement = 0;
        char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen = MAX_SYM_NAME;

        char funcName[256] = "<unknown>";
        if (SymFromAddr(hProcess, frame.AddrPC.Offset, &displacement, pSymbol)) {
            strncpy(funcName, pSymbol->Name, sizeof(funcName) - 1);
            funcName[sizeof(funcName) - 1] = '\0';
        }

        IMAGEHLP_LINE64 line;
        memset(&line, 0, sizeof(line));
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD lineDisplacement = 0;

        char fileLoc[512] = "";
        if (SymGetLineFromAddr64(hProcess, frame.AddrPC.Offset, &lineDisplacement, &line)) {
            snprintf(fileLoc, sizeof(fileLoc), " at %s:%lu", line.FileName, (unsigned long)line.LineNumber);
        } else {
            /* Fallback: Module name + displacement */
            IMAGEHLP_MODULE64 mod;
            memset(&mod, 0, sizeof(mod));
            mod.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
            if (SymGetModuleInfo64(hProcess, frame.AddrPC.Offset, &mod)) {
                snprintf(fileLoc, sizeof(fileLoc), " [%s + 0x%llX]", mod.ModuleName, (unsigned long long)(frame.AddrPC.Offset - mod.BaseOfImage));
            } else {
                snprintf(fileLoc, sizeof(fileLoc), " [0x%016llX]", (unsigned long long)frame.AddrPC.Offset);
            }
        }

        fprintf(stderr, SFL_WHITE "    #%-2d " SFL_CYAN "%s" SFL_YELLOW "%s\n" SFL_RESET, frameIndex, funcName, fileLoc);
        frameIndex++;
        if (frameIndex >= 32) break; /* Avoid runaway loops */
    }

    SymCleanup(hProcess);

    fprintf(stderr, SFL_RED "================================================================================\n" SFL_RESET);
    fprintf(stderr, SFL_DIM "  Process terminated with status code 0x%08X (segfault-lens)\n\n" SFL_RESET, (unsigned int)code);
    fflush(stderr);

    ExitProcess(code);
    return EXCEPTION_EXECUTE_HANDLER;
}

#else /* POSIX */

static void sfl_posix_signal_handler(int sig, siginfo_t* info, void* uctx_raw) {
    if (__atomic_exchange_n(&g_segfault_lens_active, 1, __ATOMIC_SEQ_CST) != 0) {
        fprintf(stderr, "\n[segfault-lens] Double fault detected. Terminating.\n");
        _exit(128 + sig);
    }

    uintptr_t fault_addr = (uintptr_t)info->si_addr;
    const char* cause_str = sfl_get_crash_cause(sig, fault_addr, 0);

    fprintf(stderr, "\n");
    fprintf(stderr, SFL_RED "================================================================================\n" SFL_RESET);
    fprintf(stderr, SFL_RED "  💥 CRASH DETECTED by segfault-lens\n" SFL_RESET);
    fprintf(stderr, SFL_RED "================================================================================\n" SFL_RESET);
    fprintf(stderr, SFL_WHITE "  Signal          : " SFL_YELLOW "%d (%s)\n" SFL_RESET, sig, sys_siglist[sig]);
    fprintf(stderr, SFL_WHITE "  Diagnosis       : " SFL_RED "%s\n" SFL_RESET, cause_str);
    fprintf(stderr, SFL_WHITE "  Fault Address   : " SFL_CYAN "0x%016lx" SFL_RESET "\n", (unsigned long)fault_addr);

    /* Backtrace */
    fprintf(stderr, "\n" SFL_GREEN "  Stack Trace:\n" SFL_RESET);
    void* callstack[64];
    int frames = backtrace(callstack, 64);
    char** strs = backtrace_symbols(callstack, frames);
    if (strs) {
        for (int i = 0; i < frames; ++i) {
            fprintf(stderr, SFL_WHITE "    #%-2d " SFL_CYAN "%s\n" SFL_RESET, i, strs[i]);
        }
        free(strs);
    }

    fprintf(stderr, SFL_RED "================================================================================\n" SFL_RESET);
    fflush(stderr);

    _exit(128 + sig);
}

#endif

void segfault_lens_init(void) {
#if defined(_WIN32) || defined(_WIN64)
    if (!g_segfault_lens_win_handler) {
        g_segfault_lens_win_handler = AddVectoredExceptionHandler(1, sfl_win_vectored_handler);
    }
#else
    /* Setup alternative stack for handling stack overflows */
    static char alt_stack_mem[SIGSTKSZ * 2];
    stack_t alt_stack;
    alt_stack.ss_sp = alt_stack_mem;
    alt_stack.ss_size = sizeof(alt_stack_mem);
    alt_stack.ss_flags = 0;
    sigaltstack(&alt_stack, NULL);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sfl_posix_signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
#endif
}

void segfault_lens_uninstall(void) {
#if defined(_WIN32) || defined(_WIN64)
    if (g_segfault_lens_win_handler) {
        RemoveVectoredExceptionHandler(g_segfault_lens_win_handler);
        g_segfault_lens_win_handler = NULL;
    }
#else
    signal(SIGSEGV, SIG_DFL);
    signal(SIGFPE, SIG_DFL);
    signal(SIGILL, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    signal(SIGABRT, SIG_DFL);
#endif
}

#endif /* SEGFAULT_LENS_IMPLEMENTATION */
