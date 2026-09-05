# Security Policy

## Reporting a Vulnerability

If you discover a security vulnerability in CxxScript, please **do not open a public issue**.
Instead, report it privately via [GitHub Security Advisories](https://github.com/slightlabs/CxxScript/security/advisories/new).

Please include:

- A description of the vulnerability and its potential impact
- Steps to reproduce (a minimal `.script` snippet or C++ repro is ideal)
- The affected version/commit

We'll acknowledge reports as soon as possible and coordinate a fix and disclosure timeline with you.

## Scope

CxxScript is an embeddable script interpreter. Because scripts may originate from a semi-trusted
source (config files, business rules authored by non-developers, etc.), the interpreter supports
optional runtime guardrails — `ScriptManager::setExecutionLimits(maxCallDepth, maxSteps)` — that
cap recursion depth and total execution steps so a malformed or malicious script fails safely
instead of crashing or hanging the host process. **These limits are disabled by default** (`0` =
unlimited); hosts that execute untrusted or semi-trusted scripts should call
`setExecutionLimits(...)` with sane bounds for their use case. See the
[embedding guide](https://slightlabs.github.io/CxxScript/embedding/) for details.

Memory-safety issues (crashes, use-after-free, buffer overflows) triggered by loading or executing
a script are considered in scope; issues that require modifying the host application's own C++
code are generally out of scope.
