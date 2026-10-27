# Testing checklist

Mark only after manually testing in both thread and process mode.

- [ ] Task 1: GUI opens, tabs independent, scroll, tab close, no busy wait
- [ ] Task 2: external command, `cd`, `history`, `jobs`, `fg`, `bg`, `exit`, `source`, `$?`
- [ ] Task 3: quoting, escapes, multiline, `$VAR`, nested substitutions
- [ ] Task 4: `<`, `>`, `>>`, combinations
- [ ] Task 5: 2-stage and 3-stage pipelines, pipeline redirection
- [ ] Task 6: multiWatch simultaneous commands, 2-second repeat, Ctrl+C, exact formatting
- [ ] Task 7: hotkeys, configuration persistence, copy/paste
- [ ] Task 8: Ctrl+C, Ctrl+Z, background jobs, `jobs`, `fg`, `bg`, cleanup

Known incomplete items are described in README.md.
