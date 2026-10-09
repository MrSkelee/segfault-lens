# Reddit Post: r/C_Programming & r/cpp

**Title:**
I got tired of silent segfaults in C/C++, so I built segfault-lens (instant Rust-like panic screens + line stack traces, zero code changes needed)

**Body:**

In C and C++, when your code dereferences a null pointer, corrupts memory, or divides by zero, you typically get zero useful feedback:

```text
Segmentation fault (core dumped)
# Or on Windows:
Process exited with code 3221225477 (0xC0000005)
```

No line numbers. No function names. No registers. You either have to compile with `-g` and fire up GDB/WinDbg or litter your code with debug prints just to find out which single line failed.

I built **segfault-lens** to solve this once and for all:

```text
================================================================================
  💥 CRASH SUPERVISED by segfault-lens
================================================================================
  Exception Code  : 0xC0000005
  Diagnosis       : Null Pointer Write Dereference
  Fault Address   : 0x0000000000000000
  Instruction PC  : 0x00007FF6E3EF13D0

  CPU Registers (x86_64):
    RIP: 0x00007FF6E3EF13D0   RSP: 0x000000C0E23BFE90   RBP: 0x0000000000000000
    RAX: 0x0000000000000000   RBX: 0x0000021ECA7DF090   RCX: 0x000000C0E23BFE10
    RDX: 0x0000021ECA6A0000   RSI: 0x0000000000000000   RDI: 0x0000021ECA7E9BE0
    R8 : 0x7FFFFFFFFFFFFFFC   R9 : 0x0000000000002042   R10: 0x0000000000000001

  Stack Trace:
    #0  crash_function at src/worker.c:5
    #1  caller at src/worker.c:9
    #2  main at src/main.c:15
================================================================================
```

### Two ways to use it:

1. **Zero-Code CLI Supervisor (no recompilation / no code changes)**:
Run any existing compiled binary directly:
```bash
npx segfault-lens ./my_app.exe
# or install globally:
npm install -g segfault-lens
segfault-run ./my_app.exe
```
Because the supervisor runs out-of-process, even if your thread completely destroys its own stack (Stack Overflow / infinite recursion), the supervisor safely captures the full traceback without crashing itself.

2. **Single-Header C / C++ Library (`segfault-lens.h`)**:
If you prefer in-process handling:
```bash
npx segfault-lens init
```
Drop `#include "segfault-lens.h"` in your `main()` with `segfault_lens_init()`. Zero external dependencies.

### Edge cases handled:
- **Stack Overflows**: Out-of-process supervisor walks up to 32 deep recursive frames without tripping on the blown stack.
- **Stripped / Release builds**: Graceful fallback to module offsets (`[my_app.exe + 0x1420]`) when PDB/DWARF symbols are stripped.
- **Reentrancy guards**: Atomic CAS locks prevent double-fault loops inside handlers.

### Links:
- **npm:** https://www.npmjs.com/package/segfault-lens
- **GitHub:** https://github.com/MrSkelee/segfault-lens

If this saves you 20 minutes of debugging or you find it useful, please consider dropping a ⭐ on GitHub, it really helps the project grow! Would love feedback and any weird edge cases you encounter.
