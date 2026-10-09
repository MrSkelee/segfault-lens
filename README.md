# 💥 segfault-lens

> **Instant Rust-like crash diagnostics, panic screens & line-level stack traces for C and C++ programs.**

Zero-dependency single-header library (`segfault-lens.h`) and out-of-process CLI supervisor (`segfault-run`).

[![npm version](https://img.shields.io/npm/v/segfault-lens.svg)](https://www.npmjs.com/package/segfault-lens)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

---

## ⚡ The Problem

In C and C++, when a program dereferences a null pointer or corrupts memory, it immediately terminates with a silent, useless message:

```text
Segmentation fault (core dumped)
# Or on Windows:
Process exited with code 3221225477 (0xC0000005)
```

**Zero line numbers. Zero function names. Zero context.**
Developers spend hours firing up GDB, WinDbg, or IDE debuggers just to answer one simple question: **"Which line crashed?"**

---

## 🚀 The Solution

`segfault-lens` intercepts the crash and prints a colorized, human-readable panic screen right in your terminal:

```text
================================================================================
  💥 CRASH DETECTED by segfault-lens
================================================================================
  Exception Code  : 0xC0000005
  Diagnosis       : Null Pointer Write Dereference
  Fault Address   : 0x0000000000000000
  PC / Instruction: 0x00007FF7408C200E

  CPU Registers (x86_64):
    RIP: 0x00007FF7408C200E   RSP: 0x000000F6BFAFFA30   RBP: 0x0000000000000000
    RAX: 0x0000000000000000   RBX: 0x0000027900CAE950   RCX: 0x000000F6BFAFF960
    RDX: 0x0000027900AE0000   RSI: 0x0000000000000000   RDI: 0x0000027900CAA6B0
    R8 : 0x7FFFFFFFFFFFFFFC   R9 : 0x0000000000002042   R10: 0x0000027900CA9088

  Stack Trace:
    #0  level_three at src/worker.c:7
    #1  level_two at src/worker.c:11
    #2  level_one at src/worker.c:15
    #3  main at src/main.c:41
================================================================================
  Process terminated with status code 0xC0000005 (segfault-lens)
```

---

## 📦 How to Use

### Mode 1: Zero-Code Supervisor CLI (No Code Changes Needed!)

Run any existing compiled binary through `segfault-lens`:

```bash
# Run with npx:
npx segfault-lens ./my_program.exe

# Or install globally:
npm install -g segfault-lens
segfault-run ./my_program.exe [arguments...]
```

If the binary segfaults, divides by zero, or overflows the stack, `segfault-lens` supervises it out-of-process and prints the panic report automatically!

---

### Mode 2: Single-Header C / C++ Library

Drop `segfault-lens.h` into your project:

```bash
npx segfault-lens init
```

Include it at the top of your `main.c` / `main.cpp`:

```c
#define SEGFAULT_LENS_IMPLEMENTATION
#include "segfault-lens.h"

int main(void) {
    segfault_lens_init(); // Installs crash handlers

    // Your code here...
    int* ptr = NULL;
    *ptr = 42; // Triggers instant panic screen with line numbers!

    return 0;
}
```

Compile with debug symbols (e.g. `cl /Zi /MD main.c` on Windows or `gcc -g main.c` on Linux).

---

## 🛡️ Edge Cases Handled

1. **Stack Overflow / Infinite Recursion**: The supervisor runs out-of-process, so even if the target thread completely blows its stack to pieces, the stack trace and recursion depth (up to 32 frames) are captured cleanly.
2. **Stripped / Release Binaries (Missing PDB)**: Gracefully falls back to module-relative offsets (`[app.exe + 0x1420]`) without hanging or printing garbage.
3. **Reentrancy Protection**: Uses atomic CAS guards to prevent infinite recursive crash loops if memory corruption occurs inside exception handling.

---

## 📄 License

MIT © [MrSkelee](https://github.com/MrSkelee)
