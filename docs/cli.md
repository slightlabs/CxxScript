# The `cxxscript` CLI

The `cxxscript` binary (built by `CXXSCRIPT_BUILD_EXAMPLES`) is a runner, checker, formatter,
debugger, and REPL for `.script` files.

```text
cxxscript run <file> [proc] [args...]   Load file and call proc (default: main)
cxxscript check <file>...               Compile-check files without running
cxxscript eval '<statements>'           Evaluate top-level statements
cxxscript fmt <file>... [-w]            Pretty-print (or rewrite with -w)
cxxscript debug <file> [proc] [args...] Step-through debugger
cxxscript                               Interactive REPL
```

## `run`

Loads a file (following `import`s) and invokes a procedure. Exit codes: `0` success, `1`
compile error / unknown procedure, `2` runtime error, `3` usage error.

```bash
cxxscript run examples.script main
cxxscript run calc.script add 3 4        # prints 7
```

Arguments are parsed as `true`/`false` booleans, `'c'` chars, `"..."` strings, integers, or
doubles — falling back to `string`.

## `check`

Compile-checks one or more files without running them. Prints `<file>: OK` or diagnostics
(errors are fatal to the check; warnings print but don't fail it). Useful in CI to gate script
changes:

```bash
cxxscript check scripts/*.script
```

## `eval`

Evaluates statements in a throwaway manager and prints the resulting `Value`:

```bash
cxxscript eval 'return 6 * 7;'                    # 42
cxxscript eval 'int32[] a = [1,2]; push(a, 3); return len(a);'   # 3
```

## `fmt`

Canonical pretty-printer: normalizes indentation, spacing, and brace placement while
preserving comments and declaration order. Prints to stdout; `-w` rewrites in place (reporting
`reformatted` only when the content changed):

```bash
cxxscript fmt messy.script          # diff-check in CI: fmt file | diff - file
cxxscript fmt -w scripts/*.script
```

`fmt` fails with a parse error if the file doesn't parse — it never produces partial output.

## `debug`

Runs a procedure under an interactive, statement-granular debugger driven by the engine's
debug hook. It stops before the first statement; commands:

| Command | Action |
|---|---|
| `s` / `step` | Execute the next statement (into calls) |
| `n` / `next` | Next statement, stepping **over** calls |
| `c` / `continue` | Run to the next breakpoint |
| `b [file:]line` | Set a breakpoint (current file by default) |
| `rb [file:]line` | Remove a breakpoint |
| `p <expr>` | Evaluate an expression in the stopped frame's locals |
| `locals` | List visible variables and their values |
| `bt` / `where` | Print the call stack |
| `l` / `list` | Show source around the current line |
| `q` / `quit` | Abort execution |
| `h` / `help` | Command list |

```bash
cxxscript debug app.script main
# /abs/app.script:3 in main
#   3 |     int32 total = 0;
# (dbg) b 7
# (dbg) c
# (dbg) p total * 2
# = 84
```

## REPL

Bare `cxxscript` (or `cxxscript repl`) opens an interactive session. Statements persist as
globals across lines; brace-balanced multi-line input is accumulated automatically; a trailing
semicolon may be omitted. Bare expressions print their value with `=`. Dot commands:

| Command | Action |
|---|---|
| `.help` | Show help |
| `.procs` | List loaded procedures |
| `.load <file>` | Load a script file's procedures into the session |
| `.reset` | Discard all state |
| `.quit` / `.exit` | Leave |

Procedure definitions typed at the prompt are added to the session and can be called from
later lines.
