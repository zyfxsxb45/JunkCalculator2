# Junk Calculator 2.2.0

A scripting language and scientific calculator built from scratch in C++20. It features a custom bytecode compiler and a stack-based virtual machine, operating without any third-party dependencies.

Developed by Yu Liangyang, Tsinghua University.

---

## Technical Overview

### Architecture
- **Lexer**: Hand-written tokenizer supporting 55+ token types, including string interpolation (`f""`), raw strings with custom delimiters (`r"TAG()TAG"`), imaginary suffixes (`3i`), and variadic ellipsis (`...`).
- **Parser**: Recursive descent parser producing an AST (Abstract Syntax Tree) with 30+ node types. Supports operator precedence, block statements, and cascading destructuring.
- **Compiler**: AST-to-bytecode compiler (Visitor pattern). Handles lexical scoping, auto-local variable declaration, loop patching, and closure capture (upvalues).
- **Virtual Machine**: Stack-based bytecode interpreter. Implements late-binding for function calls, exception handling with precise line-number unwinding, an interactive step-debugger, execution profiling, and dynamic operator dispatching.

### Language Semantics
- **Type System & Memory Management**: `std::variant`-backed dynamic typing supporting 16 internal types. 
  - *Value Types*: Scalars (double, BigInt, Complex, etc.) and Matrices (Real, Complex, String) use strictly contiguous memory and **pass-by-value** semantics for maximum mathematical auto-vectorization performance.
  - *Reference Types*: Containers (`List`, `Dict`) and OOP `Instance`s use **pass-by-reference** semantics (backed by PIMPL architecture and `std::shared_ptr`). Mutating a passed list inside a function mutates the original object.
- **Garbage Collection (GC)**: Features a custom, zero-pause **Mark-and-Sweep Garbage Collector** (`GcHeap`) running transparently on top of the VM stack. It traces GC roots (Globals, Stack, Upvalues) to detect and shatter cyclic memory leaks (e.g., a `List` containing itself) that standard C++ smart pointers cannot handle.
- **Object-Oriented Programming**: Single inheritance (`extends`), `super` proxy dispatcher, and runtime overriding via 20+ dunder methods (e.g., `__add__`, `__getitem__`). Instances can be directly destructured via `{field1, field2} = obj`.
- **Control Flow**: `if/else`, `while`, C-style `for`, `for-in` (with array/dict destructuring), `switch/case`, `break/continue/return`.
- **Error Handling**: `try/catch/throw` block constructs and functional `pcall` with safe VM-boundary exception containment.
- **Functions**: First-class closures, lambdas `(x) => expr`, default parameters, variadic arguments (`...args`), and `ref` parameter binding for pass-by-reference semantics.
- **Paradigms**: 
  - *Data Manipulation*: List comprehensions, array/dictionary destructuring (`{x, y} = obj`), and object shorthand properties.
  - *Functional*: Partial application via `_` placeholder, argument unpacking (`apply()`), pipe operator `|>` for left-to-right evaluation chains, and functional primitives (`map`, `filter`, `reduce`).

### Math & Number Theory Engine
- **Arbitrary-Precision**: Base-10^9 compressed `BigInt` layout. Implements high-base long division, GCD/LCM, and modular exponentiation.
- **Exact Rationals**: `Fraction` type backed by `BigInt` with automatic cross-reduction to prevent intermediate overflow.
- **Complex Analysis**: Native `Complex` structures with Euler identity implementations for `exp`, `log`, `sin`, `cos`, `sqrt`, and general exponentiation.
- **Linear Algebra**: `Matrix<T>` template supporting Gaussian-Jordan elimination (with row-based IEEE 754 local scaling bounds), QR decomposition (Modified Gram-Schmidt), LU decomposition (Doolittle partial pivoting), and Eigenvalues (Householder to Hessenberg + Givens QR iteration, O(n³)).
- **Matrix Calculus**: Matrix power `A^B` calculated via Taylor series expansion and scaling-and-squaring (`exp(B * ln(A))`).
- **Solvers**: Closed-form polynomial roots over ℂ (degrees 1 through 4), linear equation systems (exact, infinite, or least-squares approximations), and Newton-Raphson single-variable root finding.
- **Number Theory**: Miller-Rabin primality test, integer factorization. Streaming prime engine utilizing 64KB page buffers and O(1) anchor tracking across disk files.

### Native Modules & Standard Library
Native C++ extensions injected directly into the global execution context:
- `image`: Zero-dependency 24-bit BMP generation, plotting functions, and Bresenham line drawing.
- `prob`: 11 statistical distributions (PDF, CDF, Quantile via Newton iteration) and hypothesis tests (t-test, Welch, chi-squared).
- `json`: Recursive serialization/deserialization between JC2 datasets and JSON strings.
- `regex`: Standard library module (`regex.jc2`) implementing a backtracking NFA pattern matcher.

---

## Building

Requires a C++20 compliant compiler and CMake 3.15+.

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

*Note: On MSVC, the CMake script enforces `/MT` static linkage and Link Time Code Generation (`/GL`, `/LTCG`) to produce a single, portable executable without external DLL dependencies.*

---

## Command-Line Interface

    JunkCalculator2                    # Interactive REPL session
    JunkCalculator2 script.jc2         # Execute a script
    JunkCalculator2 --run script.jc2   # Execute a script (explicit flag)
    JunkCalculator2 script.jc2 -d      # Execute and print bytecode disassembly
    JunkCalculator2 script.jc2 --debug # Execute with interactive step-debugger
    JunkCalculator2 script.jc2 --profile # Execute and print performance breakdown report

*Script Path Context: The `run` and `import` instructions automatically push the executing script's directory onto a virtual paths stack, ensuring relative I/O (`readFile`, `import`) resolves relative to the current file, not the terminal's working directory.*

---

## Project Layout

    +-- main.cpp                    Entry point, CLI parser, and Workspace I/O
    +-- Lexer.h/cpp                 Tokenizer logic
    +-- Parser.h/cpp                Recursive descent parser
    +-- Compiler.h/cpp              AST traversal and Bytecode emitter
    +-- VM.h/cpp                    Stack-based Virtual Machine execution loop
    +-- Bytecode.h                  OpCode definitions and Chunk representations
    +-- BuiltinRegistry.h/cpp       180+ generic & stateless built-in functions
    +-- Value.h                     std::variant Type System & Callables
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
    +-- modules/
    |   +-- json_module.h           JSON encode/decode native module
    |   +-- image_module.h          Image wrapper native module
    |   +-- prob_module.h           Probability distribution native module
    +-- lib/
    |   +-- regex.jc2               Standard library (NFA Regex)
    +-- jc2-language/               VS Code Language Support Extension

---

## License

MIT