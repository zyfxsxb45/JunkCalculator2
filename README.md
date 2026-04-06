# Junk Calculator 2.0

A full-featured scripting language and scientific calculator built from scratch in C++20.

**Developed by Yu Liangyang, Tsinghua University**

---

## Features

### Language

- Dynamic typing with 16+ value types (double, BigInt, Fraction, Complex, Matrix, String, Dict, List, Class instances...)
- Classes with single inheritance, super, operator overloading (20+ dunder methods)
- Closures, lambdas, default parameters, ref parameters
- Control flow: if/else, while, for, for-in, switch/case, break/continue/return, try/catch/throw
- Destructuring assignment: `[a, b] = [b, a]`
- List comprehension: `[x^2 for x in range(10) if x % 2 == 0]`
- Pipe operator: `data |> sort |> unique |> reverse`
- F-strings: `f"Hello, {name}! Pi = {PI::.4f}"`
- Raw strings: `r"TAG(content with "quotes")TAG"`
- Imaginary literals: `3 + 4i` (lexical, survives variable override)
- Dict literals: `{name: "Alice", age: 30}`
- Matrix slicing: `A[1:3, :]`, `A[::-1, :]`
- Block matrix assembly: `[A, B; C, D]`

### Math Engine

- Arbitrary-precision integers (base-10^9 compressed BigInt)
- Exact fractions (auto-reduced via GCD)
- Complex numbers with full transcendental function support
- Matrix algebra: determinant, inverse, eigenvalues (Hessenberg QR), LU/QR decomposition
- Matrix functions: exp, log, sin, cos, sqrt, power (Taylor + diagonalization)
- Linear equation solver (unique / infinite / least-squares)
- Polynomial solver (closed-form degree 1-4)
- Numerical calculus: differentiation, integration (Simpson), root finding (Newton)
- Number theory: primality (Miller-Rabin), factorization, Euler totient, Mobius, etc.
- Streaming prime engine: 4MB paged buffer, anchor index for O(1) lookup

### Native Modules (C++)

    import "image"    // BMP image generation and function plotting
    import "prob"     // 11 probability distributions + hypothesis tests
    import "json"     // JSON encode / decode / pretty-print

### Standard Libraries (JC2)

    import "regex"    // Backtracking NFA regex engine (pure JC2)

### Tooling

- REPL with ANSI syntax-highlighted output
- VS Code extension with syntax highlighting and one-click run
- Script-centric path resolution (imports resolve relative to the script, not CWD)
- Workspace management: save / load / ls / rm
- 25-topic help system compiled into the binary

---

## Quick Start

    JC2> 2 + 3
    5

    JC2> A = [1, 2; 3, 4]
    [1, 2]
    [3, 4]

    JC2> det(A)
    -2

    JC2> [x^2 for x in range(5)]
    [0, 1, 4, 9, 16]

    JC2> class Point {
    ...    init(x, y) = { self.x = x; self.y = y }
    ...    dist() = sqrt(self.x^2 + self.y^2)
    ...    __str__() = f"({self.x}, {self.y})"
    ...  }

    JC2> p = Point(3, 4)
    <Point {x: 3, y: 4}>

    JC2> p.dist()
    5

    JC2> f"distance = {p.dist()}"
    distance = 5

    JC2> [1, 4, 1, 5, 9, 2, 6] |> sort |> unique |> reverse
    [9, 6, 5, 4, 2, 1]

---

## Building

Requires C++20 and CMake 3.15+.

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

The executable is self-contained with no external dependencies.

---

## Command-Line Usage

    JunkCalculator2                    # Interactive REPL
    JunkCalculator2 script.jc2         # Run a script
    JunkCalculator2 --run script.jc2   # Run a script (explicit flag)

---

## Project Structure

    +-- main.cpp                    Entry point, REPL, workspace I/O
    +-- Lexer.h/cpp                 Tokenizer (55+ token types)
    +-- Parser.h/cpp                Recursive descent parser (30+ AST nodes)
    +-- Evaluator.h/cpp             Visitor-pattern tree walker (180+ builtins)
    +-- Value.h                     Dynamic type system (std::variant)
    +-- Expr.h                      AST node definitions
    +-- Token.h                     Token types and utilities
    +-- Complex.h                   Complex numbers + transcendental functions
    +-- Matrix.h                    Generic matrix template + linear algebra
    +-- BigInt.h                    Arbitrary-precision integers + number theory
    +-- Fraction.h                  Exact rational arithmetic
    +-- Base.h                      Multi-radix number system + bitwise ops
    +-- Tolerance.h                 IEEE 754 dynamic floating-point tolerance
    +-- Image.h                     BMP image generation engine
    +-- Probability.h               Distribution classes + special functions
    +-- Highlight.h                 ANSI terminal colorization
    +-- HelpText.h                  25-topic help system (compile-time embedded)
    +-- Module.h                    Native C++ module registration framework
    +-- README.md                   This file
    +-- modules/
    |   +-- json_module.h           JSON serialization native module
    |   +-- image_module.h          Image engine native module
    |   +-- prob_module.h           Probability native module
    +-- lib/
    |   +-- regex.jc2               Regular expression engine (pure JC2)
    +-- jc2-language/               VS Code extension (syntax + runner)
        +-- package.json            VS Code extension manifest
        +-- language-configuration.json     VS Code language configuration
        +-- extension.js            VS Code extension entry point (registers runner)
        +-- syntaxes/         
            +-- jc2.tmLanguage.json         TextMate grammar for JC2 syntax highlighting

---

## License

MIT