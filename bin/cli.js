#!/usr/bin/env node

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { spawn } from 'node:child_process';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const args = process.argv.slice(2);

if (args.length === 0 || args[0] === '--help' || args[0] === '-h') {
  console.log(`
\x1b[1;36msegfault-lens\x1b[0m — Instant Rust-like crash diagnostics & panic screens for C and C++

\x1b[1mUSAGE:\x1b[0m
  npx segfault-lens <executable> [args...]       Supervise binary and print colorized crash traceback
  npx segfault-lens run <executable> [args...]   Explicit run command
  npx segfault-lens init                         Drop 'segfault-lens.h' into current directory
  npx segfault-lens --version                    Show version

\x1b[1mFEATURES:\x1b[0m
  💥 Zero code modification needed with supervisor CLI
  📦 Single-header ANSI C / C++ library available for in-process hooks
  🔍 Catches: Null Dereferences, Access Violations (0xC0000005), Div-by-Zero, Stack Overflows
  📍 Resolves exact source files, line numbers, and function names
  ⚡ Dumps full CPU registers (RIP, RAX, RBX, etc.)
`);
  process.exit(0);
}

if (args[0] === '--version' || args[0] === '-v') {
  const pkg = JSON.parse(fs.readFileSync(path.join(__dirname, '..', 'package.json'), 'utf8'));
  console.log(`segfault-lens v${pkg.version}`);
  process.exit(0);
}

if (args[0] === 'init') {
  const srcHeader = path.join(__dirname, '..', 'segfault-lens.h');
  const destHeader = path.join(process.cwd(), 'segfault-lens.h');
  fs.copyFileSync(srcHeader, destHeader);
  console.log(`\x1b[1;32m✔ segfault-lens.h dropped into:\x1b[0m ${destHeader}`);
  console.log(`\nInclude in your project:\n  #define SEGFAULT_LENS_IMPLEMENTATION\n  #include "segfault-lens.h"\n\n  int main(void) {\n      segfault_lens_init();\n      ...\n  }`);
  process.exit(0);
}

const targetBinary = (args[0] === 'run') ? args[1] : args[0];
const targetArgs = (args[0] === 'run') ? args.slice(2) : args.slice(1);

if (!targetBinary) {
  console.error('\x1b[1;31mError:\x1b[0m No executable specified. Run `npx segfault-lens --help` for usage.');
  process.exit(1);
}

const supervisorExe = path.join(__dirname, 'segfault-run.exe');
if (!fs.existsSync(supervisorExe)) {
  console.error(`\x1b[1;31mError:\x1b[0m Native supervisor binary missing at ${supervisorExe}`);
  process.exit(1);
}

const child = spawn(supervisorExe, [targetBinary, ...targetArgs], {
  stdio: 'inherit',
  cwd: process.cwd()
});

child.on('exit', (code) => {
  process.exit(code ?? 0);
});
