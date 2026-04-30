# Junk Calculator 2.3.1.0

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

---

## What's New in v2.3.1.0

Version 2.3.1.0 introduces structural updates to the Computer Algebra System (CAS) algorithms and improves Virtual Machine (VM) execution efficiency.

- **CAS Algorithm Updates:**
  - **Polynomial Factorization:** Updated `factor()` to use the Cantor-Zassenhaus algorithm over $\mathbb{Z}_p$ for multivariate polynomial factorization.
  - **Integration:** The `integ()` function now incorporates elements of the Risch algorithm, including Hermite reduction, the Rothstein-Trager algorithm, and Liouvillian differential field extensions.
  - **Polynomial GCD:** Replaced the standard Euclidean algorithm with Subresultant Pseudo-Remainder Sequences to prevent coefficient expansion during symbolic division.
- **DAG Symbol Representation:** Symbolic nodes now utilize directed acyclic graphs (DAG) and a global object pool instead of standard abstract syntax trees. This reduces memory allocation overhead and improves tree comparison performance.
- **Execution Optimization:** 
  - Substituted `try-catch` blocks with `std::optional` in the calculus engine for standard control flow, reducing C++ stack-unwinding overhead.
  - Added `isPolynomialIn` validations to prevent unnecessary expansions of non-polynomial expressions and mitigate stack overflow issues.
- **Mathematical & Language Additions:**
  - Added reciprocal trigonometric functions (`sec`, `csc`, `cot`) and special integral functions (`Si`, `Ci`, `Ei`, `Li`, `fresnel_s`, `fresnel_c`) to the standard library.
  - Introduced `symconfig()` and `setSymLimit()` built-in functions to adjust computational depth and expansion limits at runtime.
  - Unified `ln` internally to `log` for standardized processing.

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
    +-- Symbolic.h/cpp              Computer Algebra System (CAS) core
    +-- SymRules.h                  Pre-defined integration and simplification rules
    +-- Integration.h/cpp           Symbolic integration and Risch algorithm
    +-- Factorization.h/cpp         Polynomial factorization logic
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