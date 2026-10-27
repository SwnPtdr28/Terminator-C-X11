# Terminator — Task 1–8 development build

This is a **working development build**, not a claim of full compliance with the CS69201 rubric. It is built on the user's existing X11/pthread/process-mode project. It never calls `system()`, `popen()` or `/bin/bash -c` to run user commands.

## Build

In WSL Ubuntu with `gcc`, `make` and `libx11-dev` installed:

```bash
make clean && make
./terminator
./terminator useProcess=True
```

Open the folder using `code .` from WSL. `make` compiles the full program. Run `tests/run_smoke.sh` to exercise the command engine and confirm both GUI modes launch using Xvfb, if available.

## Task 1–8 status — read before submitting

1. **X11 GUI, tabs, threads and process modes:** implemented basic GUI and independent per-tab CWD. Some edge cases (crash isolation, cleanup) need more testing.
2. **External commands and built-ins:** `fork/execvp`, `cd`, `history`, `jobs`, `fg`, `bg`, `$?`, unknown command errors implemented at a basic level. `exit` currently prints instructions to close the tab instead of closing it; `source` not implemented.
3. **Multiline, quotes, substitutions:** single/double quotes, escapes, `$NAME`, `$?`, and trailing-backslash continuation work in common cases. **Nested `$(...)`, backticks and full multiline grammar are not implemented.**
4. **Redirection:** `<`, `>`, `>>` supported on external commands and pipelines; not full shell fd-redirection syntax.
5. **Pipelines:** `cmd1 | cmd2 | ...` using N-1 pipes and N child processes. Simple pipelines and redirection are supported; pipeline built-ins are limited.
6. **multiWatch:** prototype `multiWatch ["date", "echo hello"]` executes commands concurrently, polls pipes, repeats after 2 seconds and handles Ctrl+C in common cases. Output format and cancellation edge cases require further work.
7. **Keyboard controls:** Ctrl+T, Ctrl+Tab, Ctrl+Shift+W, Ctrl+A/E/U/K/W/L, arrows, Backspace/Delete, Up/Down, basic Tab completion. **Configurable persisted hotkeys and X11 copy/paste are not implemented.**
8. **Signals and jobs:** foreground process-group Ctrl+C/Z, `cmd &`, `jobs`, `fg`, `bg` prototypes. **Not complete POSIX job control**: no controlling-terminal transfer, background pipeline tracking and child cleanup need improvement.

Tasks 9–11 are **not complete**: shared history uses an advisory-file-locked persistent file rather than required process-shared semaphore/shared-memory design; Ctrl+R search is absent; Tab completion is basic; a custom script interpreter is absent.

## Quick manual tests

- In both modes: `pwd`, `cd /tmp`, `pwd`, `echo hello`, `echo "hello world"`, `echo $HOME`, `false`, `echo $?`.
- Redirect: `echo alpha > /tmp/term.txt`, `cat < /tmp/term.txt`, `echo beta >> /tmp/term.txt`, `cat /tmp/term.txt`.
- Pipeline: `printf "a\nb\n" | wc -l`.
- Background: `sleep 10 &`, `jobs`, `fg 1`.
- MultiWatch: `multiWatch ["date", "echo hello"]` then Ctrl+C.
- Create two tabs and verify `cd /tmp` in one does not change `pwd` in the other.
- Test `./terminator useProcess=True` separately; process mode uses IPC packets for output.

## Important caveats

The smoke test validates basic engine commands and checks the X11 windows can launch; it is **not an automated end-to-end X11 keyboard test**. A successful compilation is not proof of full rubric compliance. Do not submit this as a finished eight-task project without fixing the incomplete parts and testing thoroughly.
