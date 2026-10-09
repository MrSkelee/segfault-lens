import { execSync, spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';

const DIR = 'C:\\Users\\andre\\.gemini\\antigravity-ide\\scratch\\segfault-lens';
const CLI = path.join(DIR, 'bin', 'cli.js');

console.log('\x1b[1;36m=== SUITE DI VERIFICA COMPLETA segfault-lens v1.0.3 ===\x1b[0m\n');

let passed = 0;
let total = 5;

// TEST 1: Versione CLI
try {
  const res = execSync(`node "${CLI}" --version`, { encoding: 'utf8' }).trim();
  if (res.includes('1.0.3')) {
    console.log('\x1b[1;32m✔ TEST 1 PASSED:\x1b[0m CLI version corrisponde a v1.0.3');
    passed++;
  } else {
    console.error('\x1b[1;31m✘ TEST 1 FAILED:\x1b[0m output inatteso:', res);
  }
} catch (e) {
  console.error('\x1b[1;31m✘ TEST 1 FAILED:\x1b[0m', e.message);
}

// TEST 2: Comando init
try {
  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'sfl-init-'));
  execSync(`node "${CLI}" init`, { cwd: tmpDir, encoding: 'utf8' });
  const exists = fs.existsSync(path.join(tmpDir, 'segfault-lens.h'));
  fs.rmSync(tmpDir, { recursive: true, force: true });
  if (exists) {
    console.log('\x1b[1;32m✔ TEST 2 PASSED:\x1b[0m "npx segfault-lens init" rilascia segfault-lens.h correttamente');
    passed++;
  } else {
    console.error('\x1b[1;31m✘ TEST 2 FAILED:\x1b[0m segfault-lens.h non trovato');
  }
} catch (e) {
  console.error('\x1b[1;31m✘ TEST 2 FAILED:\x1b[0m', e.message);
}

// TEST 3: Programma normale (senza crash)
try {
  const normalSrc = path.join(DIR, 'normal.c');
  fs.writeFileSync(normalSrc, '#include <stdio.h>\nint main(void){ printf("Tutto ok!\\n"); return 0; }\n');
  execSync(`cmd.exe /c ""C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools\\VC\\Auxiliary\\Build\\vcvars64.bat" && cl /nologo normal.c /Fe:normal.exe"`, { cwd: DIR, stdio: 'pipe' });
  const runRes = spawnSync('node', [CLI, 'normal.exe'], { cwd: DIR, encoding: 'utf8' });
  if (runRes.status === 0 && runRes.stdout.includes('Tutto ok!')) {
    console.log('\x1b[1;32m✔ TEST 3 PASSED:\x1b[0m Programma sano eseguito senza falsi positivi (exit 0)');
    passed++;
  } else {
    console.error('\x1b[1;31m✘ TEST 3 FAILED:\x1b[0m', runRes.stdout, runRes.stderr);
  }
} catch (e) {
  console.error('\x1b[1;31m✘ TEST 3 FAILED:\x1b[0m', e.message);
}

// TEST 4: Supervisor CLI su binario con crash (Null Pointer Deref)
try {
  const runCrash = spawnSync('node', [CLI, 'demo.exe'], { cwd: DIR, encoding: 'utf8' });
  const out = runCrash.stderr + runCrash.stdout;
  const hasPanicHeader = out.includes('CRASH SUPERVISED by segfault-lens');
  const hasDiag = out.includes('Null Pointer Write Dereference') || out.includes('0xC0000005');
  const hasRegisters = out.includes('CPU Registers');
  const hasStack = out.includes('calcola_dati') && out.includes('demo.c:5');
  
  if (hasPanicHeader && hasDiag && hasRegisters && hasStack) {
    console.log('\x1b[1;32m✔ TEST 4 PASSED:\x1b[0m Supervisor intercetta crash, registri e stack trace esatto (demo.c:5)');
    passed++;
  } else {
    console.error('\x1b[1;31m✘ TEST 4 FAILED:\x1b[0m Mancano componenti diagnostici:', {
      hasPanicHeader, hasDiag, hasRegisters, hasStack
    });
  }
} catch (e) {
  console.error('\x1b[1;31m✘ TEST 4 FAILED:\x1b[0m', e.message);
}

// TEST 5: In-process single-header library (segfault-lens.h)
try {
  const inprocRes = spawnSync(path.join(DIR, 'test_inprocess.exe'), [], { cwd: DIR, encoding: 'utf8' });
  const out5 = inprocRes.stderr + inprocRes.stdout;
  const hasInprocHeader = out5.includes('CRASH DETECTED by segfault-lens');
  const hasInprocStack = out5.includes('bad_function') && out5.includes('test_inprocess.c:7');

  if (hasInprocHeader && hasInprocStack) {
    console.log('\x1b[1;32m✔ TEST 5 PASSED:\x1b[0m Libreria in-process (segfault-lens.h) intercetta crash e riga sorgente');
    passed++;
  } else {
    console.error('\x1b[1;31m✘ TEST 5 FAILED:\x1b[0m In-process handler incompleto');
  }
} catch (e) {
  console.error('\x1b[1;31m✘ TEST 5 FAILED:\x1b[0m', e.message);
}

console.log(`\n\x1b[1mRISULTATO: ${passed}/${total} test superati con successo.\x1b[0m`);
