# Junk Calculator 2.0

A fully-featured scripting language and scientific calculator built from scratch in C++20.

Developed by **Yu Liangyang**, Tsinghua University.

## Features

- **Complete interpreter pipeline**: Lexer → Parser → AST → Evaluator
- **16+ data types**: double, BigInt, Fraction, Complex, Matrix, String, Dict, List, Class instances
- **OOP**: Classes, single inheritance, super, operator overloading (20+ dunder methods)
- **Functional**: Lambdas, closures, pipe operator `|>`, list comprehensions
- **Linear algebra**: Eigenvalues (Hessenberg QR), LU/QR decomposition, matrix functions
- **Number theory**: Miller-Rabin primality, factorization, Euler's totient, Möbius function
- **Native modules**: JSON, Image (BMP), Probability distributions
- **Modern syntax**: f-strings, raw strings, destructuring, default parameters, `3i` literals
- **Script-centric path resolution**: relative paths resolve from the executing script
- **VS Code extension**: Syntax highlighting + one-click script execution

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

JC2> f"pi = {PI::.4f}"
pi = 3.1416

JC2> data |> sort |> unique |> reverse

## Building
Requires C++20 and CMake 3.15+.