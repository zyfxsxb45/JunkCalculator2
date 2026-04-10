# Junk Calculator 2.2.1

A scripting language and scientific calculator implemented in C++20. It features a custom bytecode compiler and a stack-based virtual machine, requiring no third-party dependencies.

Developed by Yu Liangyang, Tsinghua University.

---

## Technical Overview

### Architecture
- **Lexer**: Tokenizer supporting 55+ token types, including string interpolation (`f""`), raw strings with custom delimiters (`r"TAG()TAG"`), imaginary suffixes (`3i`), and variadic ellipsis (`...`).
- **Parser**: Recursive descent parser producing an AST (Abstract Syntax Tree) with 30+ node types. Supports operator precedence, block statements, and destructuring.
- **Compiler**: AST-to-bytecode compiler (Visitor pattern). Handles lexical scoping, auto-local variable declaration, loop patching, and closure capture.
- **Virtual Machine**: Stack-based bytecode interpreter. Implements late-binding for function calls, exception handling with line-number unwinding, an interactive step-debugger, execution profiling, and dynamic operator dispatching.

### Language Semantics
- **Type System & Memory Management**: `std::variant`-backed dynamic typing supporting 17 internal types. 
  - *Value Types*: Scalars (double, BigInt, Complex) and Matrices (Real, Complex, String) use contiguous memory and pass-by-value semantics.
  - *Reference Types*: Containers (`List`, `Dict`, `Set`) and OOP `Instance`s use pass-by-reference semantics (backed by PIMPL architecture and `std::shared_ptr`).
- **Garbage Collection (GC)**: Mark-and-Sweep Garbage Collector (`GcHeap`) running on top of the VM stack. It traces GC roots (Globals, Stack, Upvalues) to resolve cyclic references.
- **Object-Oriented Programming**: Single inheritance (`extends`), `super` dispatching, and operator overloading via dunder methods (e.g., `__add__`, `__getitem__`). Instances support destructuring assignment.
- **Control Flow**: `if/else`, `while`, `for`, `for-in`, `switch/case`, `break/continue/return`.
- **Error Handling**: `try/catch/throw` block constructs and functional `pcall`.
- **Functions**: Closures, lambdas `(x) => expr`, default parameters, variadic arguments (`...args`), and `ref` parameter binding.
- **Generic Container API**: Array manipulation functions (`push`, `slice`, `map`, `filter`, `reduce`, `sort`, `join`, `zip`, etc.) operate across four container types: `RealMatrix`, `ComplexMatrix`, `StringMatrix`, and `List`, utilizing `std::visit` and `if constexpr`.
- **Set Algebra**: `Set` type with O(1) membership testing (`in`). Supports operators for union (`|`), intersection (`&`), difference (`-`), and Cartesian product (`*`). Includes powerset generation (`setPow`) and relation predicates.
- **Paradigms**: List comprehensions, partial application via `_` placeholder, argument unpacking (`apply()`), pipe operator (`|>`), and map/filter/reduce.

### Math & Number Theory Engine
- **Arbitrary-Precision**: Base-10^9 compressed `BigInt` layout. Implements high-base division, GCD/LCM, and modular exponentiation.
- **Exact Rationals**: `Fraction` type backed by `BigInt` with automatic cross-reduction.
- **Complex Analysis**: Native `Complex` structures with implementations for `exp`, `log`, `sin`, `cos`, `sqrt`, and general exponentiation.
- **Linear Algebra**: `Matrix<T>` template supporting Gaussian-Jordan elimination, QR decomposition (Modified Gram-Schmidt), LU decomposition (Doolittle partial pivoting), and Eigenvalues (Hessenberg + Givens QR iteration).
- **Matrix Calculus**: Matrix power `A^B` calculated via Taylor series expansion and scaling-and-squaring.
- **Solvers**: Closed-form polynomial roots over ℂ (degrees 1 through 4), linear equation systems (exact, infinite, or least-squares approximations), and Newton-Raphson single-variable root finding.
- **Number Theory**: Miller-Rabin primality test, integer factorization. File-backed prime engine utilizing 64KB page buffers and O(1) block indexing.

### Native Modules & Standard Library
Native C++ extensions exposed to the execution context:
- `image`: 24-bit BMP generation, plotting functions, and Bresenham line drawing.
- `prob`: 11 statistical distributions (PDF, CDF, Quantile via Newton iteration) and hypothesis tests (t-test, Welch, chi-squared).
- `json`: Serialization and deserialization between JC2 data structures and JSON strings.

JC2 standard libraries loaded via `import`:
- `regex`: Backtracking NFA regex engine with capture groups, character classes, alternation, and bounded quantifiers (`{m,n}`).
- `discrete`: Discrete mathematics toolkit covering combinatorics, binary relation analysis, graph traversal algorithms, and boolean logic truth table generation.

Standard libraries register their documentation dynamically via the `__register_help` API, providing `help("module")` support at runtime.

---

## What's New in v2.2.1

### Generic Container API
Array manipulation built-in functions have been refactored to a unified architecture using `std::visit`. Functions operate uniformly across container types and preserve the input container type in their output:

| Function Group | RealMatrix | ComplexMatrix | StringMatrix | List |
|---|:---:|:---:|:---:|:---:|
| first / last / pop / shift | ✓ | ✓ | ✓ | ✓ |
| push / prepend / insert / removeAt | ✓ | ✓ | ✓ | ✓ |
| slice (function & syntax) | ✓ | ✓ | ✓ | ✓ |
| reverse / flatten / unique | ✓ | ✓ | ✓ | ✓ |
| indexOf / count | ✓ | ✓ | ✓ | ✓ |
| join | ✓ | ✓ | ✓ | ✓ |
| map / filter / reduce | ✓ | ✓ | ✓ | ✓ |
| any / all / countIf | ✓ | ✓ | ✓ | ✓ |
| sort (natural & custom) | ✓ | ✓ | ✓ | ✓ |
| zip / cat | ✓ | ✓ | ✓ | ✓ |
| cumsum / cumprod / diffs | ✓ | ✓ | — | ✓ |
| toList / toMatrix | ✓ | ✓ | ✓ | ✓ |

*Note: The functions `pop()` and `shift()` are destructive when applied to Lists.*

### Set Data Type
Introduced `Set` as an unordered deduplicated collection with reference semantics and GC tracking:
- **Membership**: Hash-table based lookup (`in`).
- **Operators**: `a | b` (union), `a & b` (intersection), `a - b` (difference), `a * b` (Cartesian product), with compound variants `|=`, `&=`.
- **Set Operations**: `setPow(s)` (powerset), `setSymDiff(a, b)` (symmetric difference).
- **Relation predicates**: `isSubset`, `isSuperset`, `isDisjoint`.
- **Elements**: Supports varied JC2 value types via content-based hashing.
- **Container API**: Integrates with `add(s, val)`, `remove(s, val)`, `discard(s, val)`, `clear(s)`, `len(s)`.

### New Standard Libraries (Object-Oriented Refactoring)
- **`discrete.jc2`**: Refactored into a class-based discrete mathematics toolkit:
  - **`Prop` Class**: Propositional logic with operator overloading (`&`, `|`, `-`), tautology testing, and truth table generation.
  - **`Relation` Class**: Binary relation analysis utilizing a fluent API, transitive/reflexive closures, and equivalence class partitioning.
  - **`Graph` Class**: Directed/undirected graph models supporting BFS, DFS, cycle detection, topological sorting (Kahn's), and Dijkstra's algorithm.
  - **Combinatorics**: `permutations(arr)`, `combinations(arr, k)`.

- **`regex.jc2`**: Refactored to utilize an object-oriented architecture (`Regex` and `ReMatch` classes) enabling pattern pre-compilation and cached state execution. Upgraded with bounded quantifier support (`{m}`, `{m,}`, `{m,n}`) and backreference replacement. Maintains a backward-compatible global functional API.

### Dynamic Help Registry
Standard library documentation is decoupled from the C++ binary. Libraries register help text at import time via the `__register_help(topic, text)` API. The `help()` function queries both the static C++ help database and dynamic script-registered entries.

### Build & Fixes
- Suppressed MSVC C4702 (unreachable code) warnings in `std::visit` + `if constexpr` dispatch patterns.
- Added `Value::toString()` method for internal string conversion.

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

    +-- main.cpp                    Entry point, CLI parser, and Workspace I/O
    +-- Lexer.h/cpp                 Tokenizer logic
    +-- Parser.h/cpp                Recursive descent parser
    +-- Compiler.h/cpp              AST traversal and Bytecode emitter
    +-- VM.h/cpp                    Stack-based Virtual Machine execution loop
    +-- Bytecode.h                  OpCode definitions and Chunk representations
    +-- BuiltinRegistry.h/cpp       Generic & stateless built-in functions
    +-- Value.h                     std::variant Type System, Callables, Dict, List & Set
    +-- Expr.h                      AST Object Nodes
    +-- Token.h                     Lexical Enums
    +-- Complex.h                   Complex data and polynomial solvers
    +-- Matrix.h                    Matrix template and Linear Algebra routines
    +-- BigInt.h                    Large numerals and Number Theory engine
    +-- Fraction.h                  Rational numeric wrapper
    +-- Base.h                      Arbitrary-radix data types & bitwise ops
    +-- Tolerance.h                 IEEE-754 dynamic floating point margins
    +-- Image.h                     BMP memory buffer class
    +-- Probability.h               Statistics distributions math backend
    +-- Highlight.h                 ANSI sequence token colorizer
    +-- HelpText.h                  Compile-time embedded help topics
    +-- Module.h                    C++ module mounting macros
    +-- GcHeap.h                    Mark-and-Sweep Garbage Collector
    +-- modules/
    |   +-- json_module.h           JSON encode/decode native module
    |   +-- image_module.h          Image wrapper native module
    |   +-- prob_module.h           Probability distribution native module
    +-- lib/
    |   +-- regex.jc2               Standard library: NFA Regex engine
    |   +-- discrete.jc2            Standard library: Discrete Mathematics
    +-- jc2-language/               VS Code Language Support Extension

---

## License

MIT