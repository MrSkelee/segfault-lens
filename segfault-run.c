/**
 * segfault-run.c — Zero-modification CLI supervisor for C/C++ binaries.
 *
 * Runs any C/C++ executable as an external debugger supervisor.
 * If the target segfaults, divides by zero, or overflows the stack,
 * segfault-run catches it out-of-process, dumps registers and full stack trace,
 * and prints an instant colorized panic report.
 *
 * Usage:
 *   segfault-run <program.exe> [args...]
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#define SFL_RED     "\033[1;31m"
#define SFL_GREEN   "\033[1;32m"
#define SFL_YELLOW  "\033[1;33m"
#define SFL_BLUE    "\033[1;34m"
#define SFL_MAGENTA "\033[1;35m"
#define SFL_CYAN    "\033[1;36m"
#define SFL_WHITE   "\033[1;37m"
#define SFL_DIM     "\033[2m"
#define SFL_RESET   "\033[0m"

static void enable_ansi(void) {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            SetConsoleMode(hOut, mode | 0x0004);
        }
    }
    HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
    if (hErr != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hErr, &mode)) {
            SetConsoleMode(hErr, mode | 0x0004);
        }
    }
}

static const char* get_cause_description(DWORD code, ULONG_PTR type, ULONG_PTR addr) {
    switch (code) {
        case 0xC0000005:
            if (addr == 0 || addr < 0x1000) {
                return (type == 1) ? "Null Pointer Write Dereference" : "Null Pointer Read Dereference";
            }
            if (type == 1) return "Write Access Violation (Invalid Pointer / Buffer Overflow)";
            if (type == 8) return "Data Execution Prevention (DEP) Violation";
            return "Read Access Violation (Use-After-Free / Invalid Pointer)";
        case 0xC0000094:
            return "Integer Divide by Zero";
        case 0xC000008E:
            return "Floating Point Divide by Zero";
        case 0xC00000FD:
            return "Stack Overflow (Infinite Recursion / Exhausted Stack)";
        case 0xC000001D:
            return "Illegal CPU Instruction";
        default:
            return "Hardware / System Exception";
    }
}

static void print_crash_report(HANDLE hProcess, HANDLE hThread, EXCEPTION_DEBUG_INFO* pExc) {
    enable_ansi();
    DWORD code = pExc->ExceptionRecord.ExceptionCode;
    ULONG_PTR fault_addr = 0;
    ULONG_PTR violation_type = 0;

    if (code == 0xC0000005 && pExc->ExceptionRecord.NumberParameters >= 2) {
        violation_type = pExc->ExceptionRecord.ExceptionInformation[0];
        fault_addr = pExc->ExceptionRecord.ExceptionInformation[1];
    } else {
        fault_addr = (ULONG_PTR)pExc->ExceptionRecord.ExceptionAddress;
    }

    const char* cause = get_cause_description(code, violation_type, fault_addr);

    fprintf(stderr, "\n");
    fprintf(stderr, SFL_RED "================================================================================\n" SFL_RESET);
    fprintf(stderr, SFL_RED "  💥 CRASH SUPERVISED by segfault-lens\n" SFL_RESET);
    fprintf(stderr, SFL_RED "================================================================================\n" SFL_RESET);
    fprintf(stderr, SFL_WHITE "  Exception Code  : " SFL_YELLOW "0x%08X\n" SFL_RESET, (unsigned int)code);
    fprintf(stderr, SFL_WHITE "  Diagnosis       : " SFL_RED "%s\n" SFL_RESET, cause);
    fprintf(stderr, SFL_WHITE "  Fault Address   : " SFL_CYAN "0x%016llX\n" SFL_RESET, (unsigned long long)fault_addr);
    fprintf(stderr, SFL_WHITE "  Instruction PC  : " SFL_CYAN "0x%016llX\n" SFL_RESET, (unsigned long long)pExc->ExceptionRecord.ExceptionAddress);

    /* Get Thread Context */
    CONTEXT ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.ContextFlags = CONTEXT_ALL;
    if (GetThreadContext(hThread, &ctx)) {
#if defined(_M_X64) || defined(__x86_64__)
        fprintf(stderr, "\n" SFL_MAGENTA "  CPU Registers (x86_64):\n" SFL_RESET);
        fprintf(stderr, SFL_DIM "    RIP: 0x%016llX   RSP: 0x%016llX   RBP: 0x%016llX\n" SFL_RESET,
                (unsigned long long)ctx.Rip, (unsigned long long)ctx.Rsp, (unsigned long long)ctx.Rbp);
        fprintf(stderr, SFL_DIM "    RAX: 0x%016llX   RBX: 0x%016llX   RCX: 0x%016llX\n" SFL_RESET,
                (unsigned long long)ctx.Rax, (unsigned long long)ctx.Rbx, (unsigned long long)ctx.Rcx);
        fprintf(stderr, SFL_DIM "    RDX: 0x%016llX   RSI: 0x%016llX   RDI: 0x%016llX\n" SFL_RESET,
                (unsigned long long)ctx.Rdx, (unsigned long long)ctx.Rsi, (unsigned long long)ctx.Rdi);
        fprintf(stderr, SFL_DIM "    R8 : 0x%016llX   R9 : 0x%016llX   R10: 0x%016llX\n" SFL_RESET,
                (unsigned long long)ctx.R8, (unsigned long long)ctx.R9, (unsigned long long)ctx.R10);
#elif defined(_M_ARM64) || defined(__aarch64__)
        fprintf(stderr, "\n" SFL_MAGENTA "  CPU Registers (ARM64 / AArch64):\n" SFL_RESET);
        fprintf(stderr, SFL_DIM "    PC : 0x%016llX   SP : 0x%016llX   FP : 0x%016llX   LR : 0x%016llX\n" SFL_RESET,
                (unsigned long long)ctx.Pc, (unsigned long long)ctx.Sp, (unsigned long long)ctx.Fp, (unsigned long long)ctx.Lr);
        fprintf(stderr, SFL_DIM "    X0 : 0x%016llX   X1 : 0x%016llX   X2 : 0x%016llX   X3 : 0x%016llX\n" SFL_RESET,
                (unsigned long long)ctx.X[0], (unsigned long long)ctx.X[1], (unsigned long long)ctx.X[2], (unsigned long long)ctx.X[3]);
        fprintf(stderr, SFL_DIM "    X4 : 0x%016llX   X5 : 0x%016llX   X6 : 0x%016llX   X7 : 0x%016llX\n" SFL_RESET,
                (unsigned long long)ctx.X[4], (unsigned long long)ctx.X[5], (unsigned long long)ctx.X[6], (unsigned long long)ctx.X[7]);
#elif defined(_M_IX86) || defined(__i386__)
        fprintf(stderr, "\n" SFL_MAGENTA "  CPU Registers (x86 32-bit):\n" SFL_RESET);
        fprintf(stderr, SFL_DIM "    EIP: 0x%08X   ESP: 0x%08X   EBP: 0x%08X\n" SFL_RESET,
                (unsigned int)ctx.Eip, (unsigned int)ctx.Esp, (unsigned int)ctx.Ebp);
        fprintf(stderr, SFL_DIM "    EAX: 0x%08X   EBX: 0x%08X   ECX: 0x%08X   EDX: 0x%08X\n" SFL_RESET,
                (unsigned int)ctx.Eax, (unsigned int)ctx.Ebx, (unsigned int)ctx.Ecx, (unsigned int)ctx.Edx);
        fprintf(stderr, SFL_DIM "    ESI: 0x%08X   EDI: 0x%08X\n" SFL_RESET,
                (unsigned int)ctx.Esi, (unsigned int)ctx.Edi);
#elif defined(_M_ARM) || defined(__arm__)
        fprintf(stderr, "\n" SFL_MAGENTA "  CPU Registers (ARM 32-bit):\n" SFL_RESET);
        fprintf(stderr, SFL_DIM "    PC : 0x%08X   SP : 0x%08X   LR : 0x%08X\n" SFL_RESET,
                (unsigned int)ctx.Pc, (unsigned int)ctx.Sp, (unsigned int)ctx.Lr);
        fprintf(stderr, SFL_DIM "    R0 : 0x%08X   R1 : 0x%08X   R2 : 0x%08X   R3 : 0x%08X\n" SFL_RESET,
                (unsigned int)ctx.R0, (unsigned int)ctx.R1, (unsigned int)ctx.R2, (unsigned int)ctx.R3);
#endif

        /* Stack Walk */
        fprintf(stderr, "\n" SFL_GREEN "  Stack Trace:\n" SFL_RESET);

        SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        SymInitialize(hProcess, NULL, TRUE);

        STACKFRAME64 frame;
        memset(&frame, 0, sizeof(frame));

#ifndef IMAGE_FILE_MACHINE_ARM64
#define IMAGE_FILE_MACHINE_ARM64 0xAA64
#endif
#ifndef IMAGE_FILE_MACHINE_ARMNT
#define IMAGE_FILE_MACHINE_ARMNT 0x01c4
#endif

#if defined(_M_X64) || defined(__x86_64__)
        DWORD machine = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset = ctx.Rip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Rbp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Rsp;
        frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_ARM64) || defined(__aarch64__)
        DWORD machine = IMAGE_FILE_MACHINE_ARM64;
        frame.AddrPC.Offset = ctx.Pc;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Fp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Sp;
        frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_ARM) || defined(__arm__)
        DWORD machine = IMAGE_FILE_MACHINE_ARMNT;
        frame.AddrPC.Offset = ctx.Pc;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.R11;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Sp;
        frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_IX86) || defined(__i386__)
        DWORD machine = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset = ctx.Eip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = ctx.Ebp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = ctx.Esp;
        frame.AddrStack.Mode = AddrModeFlat;
#else
        DWORD machine = 0;
#endif

        int f = 0;
        while (StackWalk64(machine, hProcess, hThread, &frame, &ctx,
                           NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
            if (frame.AddrPC.Offset == 0) break;

            char buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
            PSYMBOL_INFO pSym = (PSYMBOL_INFO)buf;
            pSym->SizeOfStruct = sizeof(SYMBOL_INFO);
            pSym->MaxNameLen = MAX_SYM_NAME;
            DWORD64 disp = 0;

            char funcName[256] = "<unknown>";
            if (SymFromAddr(hProcess, frame.AddrPC.Offset, &disp, pSym)) {
                strncpy(funcName, pSym->Name, sizeof(funcName) - 1);
                funcName[sizeof(funcName) - 1] = '\0';
            }

            IMAGEHLP_LINE64 line;
            memset(&line, 0, sizeof(line));
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisp = 0;

            char loc[512] = "";
            if (SymGetLineFromAddr64(hProcess, frame.AddrPC.Offset, &lineDisp, &line)) {
                snprintf(loc, sizeof(loc), " at %s:%lu", line.FileName, (unsigned long)line.LineNumber);
            } else {
                IMAGEHLP_MODULE64 mod;
                memset(&mod, 0, sizeof(mod));
                mod.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
                if (SymGetModuleInfo64(hProcess, frame.AddrPC.Offset, &mod)) {
                    snprintf(loc, sizeof(loc), " [%s + 0x%llX]", mod.ModuleName, (unsigned long long)(frame.AddrPC.Offset - mod.BaseOfImage));
                }
            }

            fprintf(stderr, SFL_WHITE "    #%-2d " SFL_CYAN "%s" SFL_YELLOW "%s\n" SFL_RESET, f, funcName, loc);
            f++;
            if (f >= 32) break;
        }

        SymCleanup(hProcess);
    }

    fprintf(stderr, SFL_RED "================================================================================\n" SFL_RESET);
    fprintf(stderr, SFL_DIM "  Target process terminated by segfault-lens supervisor (Exit Code 0x%08X)\n\n" SFL_RESET, (unsigned int)code);
    fflush(stderr);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("segfault-lens CLI supervisor v1.0.0\n");
        printf("Usage: segfault-run <executable> [arguments...]\n");
        return 1;
    }

    char fullAppPath[MAX_PATH] = "";
    if (GetFullPathNameA(argv[1], MAX_PATH, fullAppPath, NULL) == 0) {
        strncpy(fullAppPath, argv[1], sizeof(fullAppPath) - 1);
    }

    /* Build command line string */
    char cmdline[32768] = "";
    strcat(cmdline, "\"");
    strcat(cmdline, fullAppPath);
    strcat(cmdline, "\"");
    for (int i = 2; i < argc; ++i) {
        strcat(cmdline, " ");
        strcat(cmdline, "\"");
        strcat(cmdline, argv[i]);
        strcat(cmdline, "\"");
    }

    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "Failed to launch target process: %lu\n", GetLastError());
        return 1;
    }

    DEBUG_EVENT dbg;
    DWORD continueStatus = DBG_CONTINUE;
    int finalExitCode = 0;

    while (WaitForDebugEvent(&dbg, INFINITE)) {
        continueStatus = DBG_CONTINUE;

        switch (dbg.dwDebugEventCode) {
            case EXCEPTION_DEBUG_EVENT: {
                DWORD exCode = dbg.u.Exception.ExceptionRecord.ExceptionCode;
                /* Ignore first chance breakpoint or C++ exception */
                if (exCode == STATUS_BREAKPOINT) {
                    continueStatus = DBG_CONTINUE;
                    break;
                }

                /* Check fatal exception */
                if (exCode == 0xC0000005 || exCode == 0xC0000094 || exCode == 0xC000008E ||
                    exCode == 0xC00000FD || exCode == 0xC000001D) {
                    print_crash_report(pi.hProcess, pi.hThread, &dbg.u.Exception);
                    TerminateProcess(pi.hProcess, exCode);
                    finalExitCode = (int)exCode;
                    ContinueDebugEvent(dbg.dwProcessId, dbg.dwThreadId, DBG_CONTINUE);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                    return finalExitCode;
                }

                continueStatus = DBG_EXCEPTION_NOT_HANDLED;
                break;
            }

            case EXIT_PROCESS_DEBUG_EVENT:
                finalExitCode = (int)dbg.u.ExitProcess.dwExitCode;
                ContinueDebugEvent(dbg.dwProcessId, dbg.dwThreadId, DBG_CONTINUE);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                return finalExitCode;

            default:
                break;
        }

        ContinueDebugEvent(dbg.dwProcessId, dbg.dwThreadId, continueStatus);
    }

    return finalExitCode;
}
#endif
