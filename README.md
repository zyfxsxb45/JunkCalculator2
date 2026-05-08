<div align="right">
  <strong>English</strong> | <a href="README_zh-CN.md">简体中文</a>
</div>

# Junk Calculator 2.3.2.1

![Version](https://img.shields.io/badge/Version-v2.3.2.1-orange.svg?style=flat-square)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg?style=flat-square&logo=c%2B%2B)
![Zero Dependencies](https://img.shields.io/badge/Dependencies-0-brightgreen.svg?style=flat-square)
![CMake](https://img.shields.io/badge/CMake-3.15+-064F8C.svg?style=flat-square&logo=cmake)
![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=flat-square)

A scripting language and computer algebra system (CAS) implemented in C++20. It relies on a custom bytecode compiler and a stack-based virtual machine, requiring no third-party dependencies.

Developed by Yu Liangyang, Tsinghua University.

---

## Technical Overview

### Architecture
- **Lexer**: Tokenizer supporting over 55 token types, including string interpolation (`f""`), raw strings with custom delimiters (`r"TAG()TAG"`), alternating single/double quotes, imaginary suffixes (`3i`), and variadic ellipsis (`...`).
- **Parser**: Recursive descent parser producing an AST (Abstract Syntax Tree) with over 30 node types. Supports operator precedence, block statements, comma sequence evaluation, and destructuring.
- **Compiler**: AST-to-bytecode compiler utilizing the Visitor pattern. Handles lexical scoping, auto-local variable declaration, loop patching, and closure capture.
- **Virtual Machine**: Stack-based bytecode interpreter. Implements late-binding for function calls, exception handling with line-number unwinding, an interactive step-debugger, execution profiling, and dynamic operator dispatching.

### Language Semantics
- **Type System & Memory Management**: `std::variant`-backed dynamic typing supporting 17 internal types. 
  - *Value Types*: Scalars (double, BigInt, Complex) and Matrices (Real, Complex, String) use contiguous memory and pass-by-value semantics.
  - *Reference Types*: Containers (`List`, `Dict`, `Set`) and OOP `Instance`s use pass-by-reference semantics (backed by PIMPL architecture and `std::shared_ptr`).
- **Gradual Typing**: Optional runtime type contracts for function parameters and return values (e.g., `func(a: double, b: matrix) -> bool = ...`). Supports base types, containers, and class inheritance definitions.
- **Garbage Collection (GC)**: Mark-and-Sweep Garbage Collector (`GcHeap`) executing on top of the VM stack. Traces GC roots (Globals, Stack, Upvalues, and Contexts) to resolve cyclic references.
- **Object-Oriented Programming**: Single inheritance (`extends`), `super` dispatching, and operator overloading via dunder methods (e.g., `__add__`). Instances support destructuring assignment.
- **Control Flow**: `if/else`, `while`, `for`, `for-in`, `switch/case`, `break/continue/return`.
- **Error Handling**: `try/catch/throw` constructs and functional `pcall` with stack tracebacks.
- **Execution Control**: Robust `Ctrl+C` interrupt mechanism to safely halt infinite loops or heavy CAS computations without crashing the VM. Pressing `Ctrl+C` three times consecutively triggers an immediate hard exit.
- **Functions**: Closures, lambdas `(x) => expr`, default parameters, variadic arguments (`...args`), and `ref` parameter binding.
- **Generic Container API**: Array manipulation functions (`push`, `slice`, `map`, `filter`, `reduce`, `sort`, `join`, `zip`, etc.) operate across four container types: `RealMatrix`, `ComplexMatrix`, `StringMatrix`, and `List`.
- **Set Algebra**: `Set` type with O(1) membership testing (`in`). Supports operators for union (`|`), intersection (`&`), difference (`-`), and Cartesian product (`*`). Includes powerset generation (`setPow`) and relation predicates.

### Mathematics & CAS Engine
- **Computer Algebra System (CAS)**: A symbolic mathematics engine operating on Directed Acyclic Graphs (DAG). Features simplification (`simplify`, `expand`, `contract`, `factor`, `trigsimp`), symbolic calculus (`diff`, `integ`, `limit`, `taylor`), and exact analytic root finding (`solveEq`).
- **Polynomial Algebra**: Uses Subresultant Pseudo-Remainder Sequences for polynomial GCD, and Finite Field $\mathbb{Z}_p$ mapping (Cantor-Zassenhaus algorithm) for multivariate factorization. 
- **Integration Engine**: Implements subsets of the Risch algorithm, including Hermite reduction, the Rothstein-Trager algorithm, and Liouvillian differential field extensions.
- **Arbitrary-Precision**: Base-10^9 compressed `BigInt` layout. Implements high-base division, GCD/LCM, and modular exponentiation.
- **Exact Rationals & Promotion**: `Fraction` types recursively cross-reduce. Exact rational powers (e.g., `(1/2)^(1/2)`) that cannot be resolved numerically auto-promote into `SymExpr` CAS trees to prevent floating-point precision loss.
- **Linear Algebra**: `Matrix<T>` template supporting Gaussian-Jordan elimination, QR decomposition (Modified Gram-Schmidt), LU decomposition (Doolittle partial pivoting), and Eigenvalues (Hessenberg + Givens QR iteration).

### Native Modules & Standard Library
Native C++ extensions exposed to the execution context:
- `image`: OOP-based BMP generation, drawing primitives with SDF (Signed Distance Field) sub-pixel anti-aliasing, and ASCII font rendering.
- `prob`: OOP-based statistical distributions (PDF, CDF, Quantile via Newton iteration) and hypothesis tests.
- `json`: JSON serialization and deserialization.
- `socket`: Low-level TCP/IP networking stack (WinSock2/POSIX bindings).
- `bytes`: Memory buffering and low-level binary I/O operations.
- `window`: Native GUI window rendering engine. Supports Mouse-Look pointer capturing and independent IME toggling (Win32).
- `latex`: Bi-directional LaTeX engine. Serializes JC2 objects to LaTeX, and parses raw LaTeX formulas into executable closures.

JC2 standard libraries loaded via `import`:
- `collections`: Data structures including `Stack`, `Queue`, `Deque`, `PriorityQueue` (Heap), and Search Trees.
- `regex`: Object-Oriented NFA regex engine with capture groups, alternation, and quantifiers.
- `discrete`: Discrete mathematics toolkit covering combinatorics, binary relations, and graph traversal.
- `engine`: Game framework abstraction over the `window` module for render loops and event state management.
- `net`: OOP wrapper for TCP streams (`TcpSocket` and `TcpServer`).
- `http`: HTTP/1.1 client supporting URL parsing and GET/POST requests.
- `buffer`: Binary manipulation API with cursor support.

---

## What's New in v2.3.3.0

Version 2.3.3.0 focuses on refactoring the compiler's scoping system and memory semantics, introducing new closure state modifiers, and expanding the Command-Line Interface (CLI) and related toolchains to further enhance the security and stability of the language core.

### ⚠️ Breaking Changes & Migration Guide

This update unifies the underlying variable model processing logic. Older code may require the following migrations:

1. **Deprecation of the `global` keyword**
   - **Reason**: Introduced a more structured upward addressing mechanism.
   - **Migration**: Replace all `global` keywords in old code with the newly introduced `ref` modifier. When referencing or modifying external variables inside a function, uniformly use `ref x = ...`.
2. **Function Declaration Desugaring**
   - **Impact**: Removed the privileged scope of explicit functions. All `func() = {}` declarations are now strictly equivalent to anonymous closure assignments `func = () => {}` at compile time. Function names are now completely identical to ordinary variables and follow the same auto-local shadowing rules.
3. **Modifier Binding Scope Reduced to Identifier Level**
   - **Impact**: In destructuring declarations, if you need to modify an upper-level variable or set a persistent state, you must individually mark the modifier for each specific variable. For example: `[ref a, state b, c] = [1, 2, 3]`.

### Compiler & Core Scope Refactoring
This update officially establishes a tri-state closure scope framework consisting of ordinary local assignment (Auto-local), persistent state (`state`), and reference modification (`ref`):
- **New `ref` Modifier (Upper Scope Penetration)**: Breaks the default auto-local shadowing rule. Allows direct addressing and modification of identically named variables in the outer (or global) scope from within a closure. `ref` features strict existence validation, only permitting modifications to existing variables. If an attempt is made to penetrate and modify an undeclared variable, the VM will immediately intercept it and throw a runtime error, completely eliminating accidental global environment pollution caused by typos.
- **New `state` Modifier (Private Persistent State)**: Grants closure variables local persistent memory capabilities. Variables marked with `state` are strictly initialized only once during the closure's lifecycle. Subsequent calls to the function will retain and allow modification of this state, perfectly implementing state machine mechanics without polluting the external space. If there is no explicit assignment (e.g., `state x`), it safely defaults to `none`.
- **Refined Destructuring Assignment Semantics**: Optimized LHS (Left-Hand Side) addressing priority, enabling `ref` and `state` to seamlessly integrate into multi-dimensional array and dictionary destructuring syntax.
- **Fixed Recursive Closures**: After desugaring functions into anonymous closures, the underlying system automatically captures its own name by reference, supporting seamless and safe self-recursive closure calls.
- **Expression Expansion**: Restored full support for Comma Expressions and adjusted related parsing priorities to be compatible with the new scope modifiers.

### Memory Safety & VM Engine Optimization
- **Dedicated Global Modifier Instruction**: To support strict `ref` validation, the underlying VM introduces a new `OP_SET_GLOBAL_REF` opcode, completing variable existence checks and read/writes within O(1) single hash addressing complexity, achieving safety while maintaining zero performance overhead.
- **Cyclic Reference Prevention**: Added object reference path detection for deep copy `clone()` and deep freeze `val()` to prevent stack overflows or memory exhaustion when operating on self-referencing containers (like self-contained dictionaries or lists).
- **Streamlined Instruction Set**: Removed redundant function call opcodes from the underlying virtual machine, optimizing the instruction execution pipeline.

### Command-Line Interface (CLI) & Toolchain
- **New `jc` Alias**: When building via CMake, wrapper programs `jc` (Linux/macOS) and `jc.bat` (Windows) are automatically generated as minimalist aliases for the original `JunkCalculator2` command.
- **Expanded CLI Startup Parameters**:
  - Added `-e` / `--eval <code>` parameter, supporting direct evaluation of a single line of code passed via the command line, exiting immediately after.
  - Added `-q` / `--quiet` silent startup mode, hiding the REPL banner and interactive prompt, facilitating seamless integration of JC2 with standard system pipelines for data stream processing.
- **Refactored Help System**:
  - Help documentation data has been fully migrated to an independent JSON file for backend driving.
  - Supports querying specific subtopics directly in the system terminal (e.g., `jc --help scope`).
  - The built-in REPL help query now includes fuzzy word matching based on Levenshtein distance.
- **VS Code Extension Upgrade**: The extension now interfaces with the system's JSON API documentation parsing, providing complete intelligent auto-completion for native functions and keywords.

### Math Engine (CAS) & Documentation
- **CAS Evaluation Optimization**: Introduced `poly-exp` closed-form calculation shortcut rules in the symbolic computation engine to prevent crashes caused by excessive recursion depth during limit calculations, polynomial differentiation, or integration.
- **Complete Documentation Coverage**: Updated the system's built-in documentation data structure, comprehensively supplementing standard specifications and example code for system constants, control functions, and class magic methods (e.g., `__str__`, `__add__`).

---

## Building

Requires a C++20 compliant compiler and CMake 3.15+.

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

*Note: On MSVC, the CMake script uses `/MT` static linkage and Link Time Code Generation (`/GL`, `/LTCG`).*

---

## Command-Line Interface

    JunkCalculator2                    # Interactive REPL session
    JunkCalculator2 script.jc2         # Execute a script
    JunkCalculator2 --run script.jc2   # Execute a script (explicit flag)
    JunkCalculator2 script.jc2 -d      # Execute and print bytecode disassembly
    JunkCalculator2 script.jc2 --debug # Execute with interactive step-debugger
    JunkCalculator2 script.jc2 --profile # Execute and print performance report

*Script Path Context: The `run` and `import` instructions push the executing script's directory onto a paths stack, resolving relative I/O based on the current file's location rather than the terminal's working directory.*

---

## Project Layout

    +-- src/
    |   +-- main.cpp                Entry point, CLI parser, and Workspace I/O
    |   +-- frontend/               Frontend components (Lexer, Parser, Compiler, AST, Highlight)
    |   +-- vm/                     Virtual Machine core (VM, Bytecode, Builtins, Interrupts)
    |   +-- memory/                 Memory & Type System (Value variant, GcHeap)
    |   +-- math/                   Math primitives (BigInt, Fraction, Complex, Matrix, Base)
    |   +-- cas/                    Computer Algebra System (Symbolic, Integration, Factorization)
    |   +-- modules/                Native C++ extensions (Image, Probability, JSON, Socket, etc.)
    +-- lib/                        Standard JC2 libraries
    +-- examples/                   Showcase scripts
    +-- jc2-language/               VS Code Language Support Extension

---

## License

MIT
