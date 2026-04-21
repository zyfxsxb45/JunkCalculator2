# Junk Calculator 2.3.0.0

A scripting language and computer algebra system (CAS) implemented in C++20. It features a custom bytecode compiler and a stack-based virtual machine, requiring no third-party dependencies.

Developed by Yu Liangyang, Tsinghua University.

---

## Technical Overview

### Architecture
- **Lexer**: Tokenizer supporting 55+ token types, including string interpolation (`f""`), raw strings with custom delimiters (`r"TAG()TAG"`), alternating single/double quotes, imaginary suffixes (`3i`), and variadic ellipsis (`...`).
- **Parser**: Recursive descent parser producing an AST (Abstract Syntax Tree) with 30+ node types. Supports operator precedence, block statements, comma sequence evaluation, and destructuring.
- **Compiler**: AST-to-bytecode compiler (Visitor pattern). Handles lexical scoping, auto-local variable declaration, loop patching, and closure capture.
- **Virtual Machine**: Stack-based bytecode interpreter. Implements late-binding for function calls, exception handling with line-number unwinding, interactive step-debugger, execution profiling, and dynamic operator dispatching.

### Language Semantics
- **Type System & Memory Management**: `std::variant`-backed dynamic typing supporting 17 internal types. 
  - *Value Types*: Scalars (double, BigInt, Complex) and Matrices (Real, Complex, String) use contiguous memory and pass-by-value semantics.
  - *Reference Types*: Containers (`List`, `Dict`, `Set`) and OOP `Instance`s use pass-by-reference semantics (backed by PIMPL architecture and `std::shared_ptr`).
- **Gradual Typing**: Optional runtime type contracts for function parameters and return values (e.g., `func(a: double, b: matrix) -> bool = ...`). Supports base types, containers, and class inheritance assertions.
- **Garbage Collection (GC)**: Mark-and-Sweep Garbage Collector (`GcHeap`) running on top of the VM stack. Traces GC roots (Globals, Stack, Upvalues, and Contexts) to resolve cyclic references.
- **Object-Oriented Programming**: Single inheritance (`extends`), `super` dispatching, and operator overloading via dunder methods (e.g., `__add__`, `__getitem__`). Instances support destructuring assignment.
- **Control Flow**: `if/else`, `while`, `for`, `for-in`, `switch/case`, `break/continue/return`.
- **Error Handling**: `try/catch/throw` constructs and functional `pcall` with ANSI-colorized call stack tracebacks.
- **Functions**: Closures, lambdas `(x) => expr`, default parameters, variadic arguments (`...args`), and `ref` parameter binding.
- **Generic Container API**: Array manipulation functions (`push`, `slice`, `map`, `filter`, `reduce`, `sort`, `join`, `zip`, etc.) operate across four container types: `RealMatrix`, `ComplexMatrix`, `StringMatrix`, and `List`.
- **Set Algebra**: `Set` type with O(1) membership testing (`in`). Supports operators for union (`|`), intersection (`&`), difference (`-`), and Cartesian product (`*`). Includes powerset generation (`setPow`) and relation predicates.

### Mathematics & CAS Engine
- **Computer Algebra System (CAS)**: A native symbolic mathematics engine operating on ASTs. Features heuristic simplification (`simplify`, `expand`, `contract`, `factor`, `trigsimp`), symbolic calculus (`diff`, `integ`, `limit`, `taylor`), and exact analytic root finding (`solveEq`).
- **AST Compilation**: The `toFunc()` architecture compiles unresolved symbolic ASTs into native VM closures for numerical evaluation across scalars and matrices.
- **Arbitrary-Precision**: Base-10^9 compressed `BigInt` layout. Implements high-base division, GCD/LCM, and modular exponentiation.
- **Exact Rationals & Promotion**: `Fraction` types recursively cross-reduce. Exact rational powers (e.g., `(1/2)^(1/2)`) that cannot be resolved numerically auto-promote into `SymExpr` CAS trees to prevent floating-point precision loss.
- **Complex Analysis**: Native `Complex` structures with implementations for `exp`, `log`, `sin`, `cos`, `sqrt`, `cbrt`, `root`, and general exponentiation.
- **Linear Algebra**: `Matrix<T>` template supporting Gaussian-Jordan elimination, QR decomposition (Modified Gram-Schmidt), LU decomposition (Doolittle partial pivoting), and Eigenvalues (Hessenberg + Givens QR iteration).

### Native Modules & Standard Library
Native C++ extensions exposed to the execution context:
- `image`: OOP-based BMP generation, drawing primitives with SDF (Signed Distance Field) sub-pixel anti-aliasing, and IBM VGA ASCII font rendering.
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

## What's New in v2.3.0.0

- **Computer Algebra System (CAS):** Integrated a fully functional symbolic engine. Supports exact equation solving (`solveEq`), symbolic differentiation (`diff`), integration (`integ`), limit calculation via L'Hôpital's rule and Taylor expansion (`limit`, `taylor`), and polynomial factorization (`factor`).
- **Symbolic-to-Closure Compilation:** Introduced `toFunc()`, which converts symbolic AST nodes into executable VM closures.
- **Automatic Dimensional Promotion:** `BigInt` and `Fraction` types raised to unresolved fractional exponents now automatically promote to symbolic expressions (`SymExpr`) instead of degrading to double-precision floats.
- **Strict Numerical Functions:** Added `cbrt` and `root` to the standard math library, featuring strict type isolation to prevent symbolic expressions from escaping during pure numerical evaluation.
- **Module & Dependency Decoupling:** Separated symbolic evaluation interfaces into `SymEval.h` to resolve cyclic compilation dependencies between the Virtual Machine's value variant and the CAS abstract syntax tree.
- **Traceback and Error Handling:** The exception system now properly unwinds nested `eval` and VM instances, allowing inner `StackTracedException`s to be caught and evaluated by outer script `try-catch` blocks.

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
    +-- Symbolic.h/cpp              Computer Algebra System (CAS) and Calculus engine
    +-- SymEval.h                   Independent numerical evaluation bridge
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
    +-- modules/                    Native C++ extensions
    +-- lib/                        Standard JC2 libraries
    +-- examples/                   Showcase scripts
    +-- jc2-language/               VS Code Language Support Extension

---

## License

MIT