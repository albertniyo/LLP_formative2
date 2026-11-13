# LLP_formative2
This repository contains 4 questions for the second formative, where each folder has its own codes, results and report.

| Directories | contexts |
|---|---|
| [Q1](Q1/) | Three-stage pipeline (`cat \| cut \| sort`) built with `fork`/`execvp`/`pipe`/`dup2`, analysed with `strace` (`pipeline`) |
| [Q2](Q2/) | System calls vs standard I/O: file copy with different buffer sizes (`copy_sys` / `copy_stdio`) |
| [Q3](Q3/) | Perfect numbers with POSIX threads vs a single thread, using a mutex-protected counter (`perfect`) |
| [Q4](Q4/) | Concurrent log scanning with a thread pool and a shared work queue (`logscan`) |

Do check the `README.md` of each question for the report, build and run steps, aas well as deliverables.
