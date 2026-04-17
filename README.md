# Junk Calculator 2.2.2

A scripting language and scientific calculator implemented in C++20. It features a custom bytecode compiler and a stack-based virtual machine, requiring no third-party dependencies.

Developed by Yu Liangyang, Tsinghua University.

---

## Technical Overview

### Architecture
- **Lexer**: Tokenizer supporting 55+ token types, including string interpolation (`f""`), raw strings with custom delimiters (`r"TAG()TAG"`), alternating single/double quotes, imaginary suffixes (`3i`), and variadic ellipsis (`...`).
- **Parser**: Recursive descent parser producing an AST (Abstract Syntax Tree) with 30+ node types. Supports operator precedence, block statements, comma sequence evaluation, and destructuring.
- **Compiler**: AST-to-bytecode compiler (Visitor pattern). Handles lexical scoping, auto-local variable declaration, loop patching, and closure capture.
- **Virtual Machine**: Stack-based bytecode interpreter. Implements late-binding for function calls, exception handling with line-number unwinding, an interactive step-debugger, execution profiling, and dynamic operator dispatching.

### Language Semantics
- **Type System & Memory Management**: `std::variant`-backed dynamic typing supporting 17 internal types. 
  - *Value Types*: Scalars (double, BigInt, Complex) and Matrices (Real, Complex, String) use contiguous memory and pass-by-value semantics.
  - *Reference Types*: Containers (`List`, `Dict`, `Set`) and OOP `Instance`s use pass-by-reference semantics (backed by PIMPL architecture and `std::shared_ptr`).
- **Garbage Collection (GC)**: Mark-and-Sweep Garbage Collector (`GcHeap`) running on top of the VM stack. It traces GC roots (Globals, Stack, Upvalues, and Contexts) to resolve cyclic references safely without stack overflows.
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
- `image`: OOP-based 24-bit BMP generation, plotting functions, Bresenham line drawing, and binary file reading.
- `prob`: OOP-based statistical distributions (PDF, CDF, Quantile via Newton iteration) and hypothesis tests.
- `json`: Serialization and deserialization between JC2 data structures and JSON strings.
- `socket`: Low-level TCP/IP networking stack (WinSock2/POSIX bindings) supporting client and server configurations.
- `bytes`: Memory buffering and low-level binary I/O operations.
- `window`: OS-level window spawning capabilities with direct memory-backed image buffer rendering (Win32).
- `latex`: Bi-directional LaTeX engine. Serializes JC2 matrices/fractions to LaTeX strings, and dynamically compiles raw LaTeX mathematical strings into executable JC2 closures.

JC2 standard libraries loaded via `import`:
- `regex`: Object-Oriented NFA regex engine with capture groups, alternation, bounded quantifiers (`{m,n}`), and backreference replacement.
- `discrete`: Object-Oriented discrete mathematics toolkit covering combinatorics, binary relation analysis, graph traversal algorithms, and propositional logic.
- `engine`: Standard library abstraction over the `window` module for game/app render loops and event state management.
- `net`: Object-Oriented wrapper for TCP streams (`TcpSocket` and `TcpServer`).
- `http`: HTTP/1.1 client supporting URL parsing, header extraction, and GET/POST requests.
- `buffer`: High-level binary manipulation API with cursor support.

Standard libraries register their documentation dynamically via the `__register_help` API, providing `help("module")` support at runtime.

---

## What's New in v2.2.2

### Native Graphical & Math Engines
- **Native Windowing (`window`)**: Implemented OS-level window spawning capabilities. Allows rendering and blitting of memory-backed image buffers directly to the screen display.
- **Game/App Engine (`engine.jc2`)**: Added a standard library abstraction over the `window` module to handle render loops and event state management.
- **LaTeX Compiler (`latex`)**: Added a native module bridging JC2 and LaTeX syntax. Supports serialization of JC2 matrices/fractions to LaTeX strings, and JIT compilation of raw LaTeX mathematical strings into executable JC2 function closures.

### Syntax & Parsing Updates
- **String Literals**: Implemented alternating single (`'`) and double (`"`) quote parsing, allowing nested quotes within string literals without explicit escaping.
- **Expression Syntax**: Added feature support for `comma` sequences (`a, b, c`) and expanded the parsing capabilities of single-line statement blocks, increasing syntactic flexibility.
- **Strict Boundaries**: Removed the "lookahead" design in the `call()` parser. Users must now explicitly wrap chained or comma-sequenced statements in parentheses `()` when passing them as function arguments to prevent parsing ambiguity.

### Virtual Machine & OOP Refactoring
- **Module APIs**: The `image` and `prob` native modules have been heavily refactored into Object-Oriented architectures, replacing legacy global functions with instance methods.
- **Context Mapping**: Resolved a critical state leak where the `self` context could be incorrectly overwritten or misdirected during chained method dispatching or closure capture.
- **GC Stability**: Fixed a bug where the Mark-and-Sweep Garbage Collector could trigger a VM stack overflow during the resolution of deeply nested cyclic references.

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
    |   +-- image_module.h          Image wrapper & binary I/O module
    |   +-- prob_module.h           Probability distribution native module
    |   +-- socket_module.h         TCP/IP network bindings module
    |   +-- bytes_module.h          Memory buffering & binary I/O module
    |   +-- window_module.h         Native OS window and rendering module
    |   +-- latex_module.h          LaTeX parser and compiler module
    +-- lib/
    |   +-- regex.jc2               Standard library: NFA Regex engine
    |   +-- discrete.jc2            Standard library: Discrete Mathematics
    |   +-- net.jc2                 Standard library: OOP TCP Sockets
    |   +-- http.jc2                Standard library: HTTP/1.1 Client
    |   +-- buffer.jc2              Standard library: Binary buffer and cursor API
    |   +-- engine.jc2              Standard library: Game and UI Engine
    +-- jc2-language/               VS Code Language Support Extension

---

## License

MIT