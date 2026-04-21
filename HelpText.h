#ifndef JC2_HELP_TEXT_H
#define JC2_HELP_TEXT_H

#include <map>
#include <string>

namespace jc {
    inline std::map<std::string, std::string> DynamicHelp;

    const std::map<std::string, std::string> BuiltinHelp = {
        {"main", R"HELP(
=================== Junk Calculator 2.3.0.0 — Help ===================

  Session Commands
  ────────────────────────────────────────────────────────────────────
    help                  Show this overview
    help <topic>          Dive into a specific topic (see list below)
    clear                 Wipe all user-defined variables
    exit / quit           Leave the calculator
    color on / color off  Enable/disable REPL syntax highlighting
    gcinfo()              Show current Garbage Collector tracking status
    gc()                  Force a Mark-and-Sweep garbage collection cycle

  Workspace & Scripts
  ────────────────────────────────────────────────────────────────────
    save <name>           Snapshot variables  → data/<name>.jc2
    load <name>           Restore a previously saved workspace
    run  <file>           Execute a script (sets relative path context)
    import "lib"          Load a library (deduplicates automatically)

    setWorkspace("path")  Change workspace directory (function form)
    getWorkspace()        Returns current workspace path
    pwd()                 Print script dir and workspace dir
    modules()             List available native modules

  Command-Line Usage
  ────────────────────────────────────────────────────────────────────
    JunkCalculator2                    Interactive REPL
    JunkCalculator2 script.jc2         Run a script
    JunkCalculator2 --run script.jc2   Run a script (explicit flag)
    JunkCalculator2 script.jc2 -d      Run with bytecode disassembly output
    JunkCalculator2 -d                 REPL with bytecode disassembly

  Topics (type "help <topic>")
  ────────────────────────────────────────────────────────────────────
    basic       Operators, constants, formatting, elementary functions
    complex     Complex number creation, properties, promotion rules
    fraction    Exact rational arithmetic with BigInt backing
    matrix      Matrix construction, indexing A[i,j], row/col ops
    linalg      Determinant, inverse, rank, decompositions, eigenvalues
    lineq       Solving Ax = b (unique / infinite / least-squares)
    matfunc     Matrix-level exp, sin, cos, log, sqrt, matpow
    vector      Dot, cross, projection, angle, parallelism tests
    poly        Closed-form polynomial solver (degree 1–4)
    cas         Computer Algebra System (Symbolic math, limits, integrals)
    calculus    User functions (single & multi-var), diff, integ, table
    stat        Descriptive statistics, percentiles, regression
    bigint      Arbitrary-precision integers & number-theoretic functions
    base        Radix conversion, display shells, bitwise operations
    typecheck   Type predicates (isint, isstring, ismatrix, ...)
    sys         RNG, prime engine, workspace, scripting, colors
    control     if/else, while, for, for-in, switch, break/continue/return
    scope       auto-local, global, const, delete, ref, closures
    string      String functions, indexing, slicing, escape sequences
    array       Array (row vector) manipulation, functional programming
    list        Heterogeneous list (any type, nestable)
    dict        Dictionary (key-value store)
    set         Unordered deduplicated Set (reference semantics)
    class       Classes, instances, inheritance, operator overloading
    error       Error handling (try/catch/throw/pcall)
    fileio      File I/O (read, write, CSV, directory)
    import      Code reuse (import libraries and scripts)

  Native Modules (require `import` before use)
  ────────────────────────────────────────────────────────────────────
    image       BMP image generation (import "image")
    prob        Probability distributions & hypothesis tests (import "prob")
    json        JSON encode/decode/pretty (import "json")
    bytes       Bare-metal memory & binary file I/O (import "bytes")
    socket      Low-level TCP/IP operating system bindings (import "socket")
    window      Real-time Native GUI window rendering (import "window")
    latex       Bi-directional LaTeX compiler & serializer (import "latex")

    Type `modules()` in the REPL to see all available native modules.

  Standard Libraries (require `import` before use, use help("<library>") for details)
  ────────────────────────────────────────────────────────────────────
    regex       Regular Expression standard library (import "regex")
    discrete    Discrete math utilities standard library (import "discrete")
    buffer      High-level binary buffer and cursor API (import "buffer")
    net         High-level TCP socket OOP wrapper (import "net")
    http        Modern HTTP/1.1 Client with JSON support (import "http")
    engine      Object-Oriented Game & GUI Framework (import "engine")
    collections Data structures: stacks, queues, heaps, trees (import "collections")

  Quick-Start Cheatsheet
  ────────────────────────────────────────────────────────────────────
    x = 3.14                       assign a real
    const G = 9.81                 immutable constant (delete G to remove)
    z = 3 + 4i                     complex number
    z = complex(3, 4)              same, via constructor
    A = [1, 2; 3, 4]               2×2 matrix
    A = matrix(2, 2, 1, 2, 3, 4)   same, via constructor
    A[0, 1] = 99                   in-place modify
    [a, b] = [1, 2]                destructuring assignment
    {x, y: yy} = obj               dict/instance destructuring
    [x, y] = [y, x]                swap variables
    v = [1; 2; 3]                  column vector (math)
    arr = [1, 2, 3]                row vector (data/array)
    L = list(1, "hi", [1,2])       heterogeneous list
    L = [1, "hi", [1,2]]           same as above (list literal)
    d = {a: 1, b: 2}               dictionary literal
    d.a = 99                       dictionary dot operator
    f(x, y = 0) = x + y            function with default parameter
    f(a, ...rest) = sum(rest)      variadic (rest) parameters
    apply(f, [1, 2, 3])            unpack arguments (spread)
    (x) => x^2                     lambda expression
    math_func(_, 10)               partial application (auto-currying)
    class Dog extends Animal {...} inheritance
    p = Point(3, 4)                instance creation
    p.dist()                       method call
    super.init(name)               call parent constructor
    __add__(o) = ...               operator overloading
    10 / 3                         exact fraction → 10/3
    2 ^ 10                         power (strictly power, NOT bitwise XOR)
    A | B                          set union / base-2 bitwise OR
    A & B                          set intersection / base-2 bitwise AND
    int(3.7)                       truncate to integer → 3
    double(frac(1,3))              convert to float → 0.333...
    3 > 2 && !false                comparison + logic
    x > 0 ? x : -x                 ternary operator
    format("x={:.2f}", PI)         string formatting → "x=3.14"
    s = 'Say "Hi!"'                alternating single/double quotes
    f"x = {x}, pi = {PI::.2f}"     string interpolation (f-string)
    func( (a=1, 2) )               pass sequence as function argument
    switch (x) { case 1: {...} }   pattern matching
    for ([k, v] in d) { ... }      destructured for-in
    [x^2 for x in range(10)]       list comprehension
    import "math_utils"            load a library
    try { 1/0 } catch (e) { e }    error handling
    // comment                     everything after // is ignored
    resetConst()                   restore PI, E, i, I, true, false
    pi()  e()  i()  none()         constant factory functions (always available)
    isint(x)  isstring(x)          type predicates (see: help typecheck)
    delete x                       remove any variable (including const)
    data |> sort |> unique         pipe operator (left-to-right)
    breakpoint()                   trigger interactive step-debugger

======================================================================
)HELP"},

        { "basic", R"HELP(
═══ Constants, Operators & Elementary Functions ═══

  Comments
  ──────────────────────
    // text              Everything after // is ignored until end of line.
                         Works in expressions, blocks, functions, and scripts.

   Arithmetic Operators
  ──────────────────────
    +  -  *  /          Standard four operations
    \                   Left division: a\b ≡ inv(a) * b
    ^                   Power             2^10 → 1024 (NOT bitwise XOR!)
    %                   Truncated modulo  (-7) % 3 → -1
    =                   Assignment        x = 42

  Set & Bitwise Operators
  ──────────────────────
    |                   Set Union / Base-2 Bitwise OR
    &                   Set Intersection / Base-2 Bitwise AND
    
    * Note: To prevent syntax ambiguity and precedence issues with polynomials, 
      the caret (^) is STRICTLY reserved for exponentiation. 
      For bitwise XOR, use the function `bitxor(a, b)`.

  Compound Assignment
  ──────────────────────
    x += expr           x -= expr           x *= expr
    x /= expr           x %= expr           x ^= expr
    x &= expr           x |= expr
    Also works on indexed elements: A[i, j] += 1, d.flag |= Set("X")

  Comma Sequence (Serial Evaluation)
  ──────────────────────
    expr1, expr2, expr3
    Evaluates each expression from left to right, discarding the results 
    of all but the last. The entire sequence returns the final expression.
    
      a = 1, b = 2, c = 3           // Sequential execution without braces
      
    ★ Strict Boundary Warning:
    The comma (,) has the LOWEST operator priority. To use a sequence inside 
    a lambda body, an assignment, or as a FUNCTION ARGUMENT, you MUST wrap 
    it explicitly in parentheses to prevent parsing ambiguity:
    
      val = (a = 10, b = 20, 30)    // val becomes 30
      f = (x) => (t = x*2, t+1)     // Lambda series
      print( (a=1, b=2) )           // Mandatory for function arguments!

   Destructuring Assignment (Arrays & Dicts)
  ──────────────────────
    [a, b, c] = [10, 20, 30]          // Array destructuring
    { name, age: a } = { name: "Bob", age: 20 }  // Dict destructuring
    Works with any iterable on the right side:
      [a, b] = [1; 2]                    Column vector
      [a, b] = list("hello", 42)          List
      { x, y } = Point(10, 20)           Force-extract from instances!
    Variable Swap & Discard:
      [x, y] = [y, x]
      [_, second, _] = [10, 20, 30]      Discard with underscore

  Partial Application & Variadic Arguments
  ──────────────────────
    Placeholder `_`: Generates a closure automatically (Auto-Currying)
      f(x, y) = x^2 + y
      p = f(_, 10)                 →  (__ph_0) => f(__ph_0, 10)
      p(5)                         →  35
      [1, 2, 3] |> map(f(_, 10), _) // Powerful with pipes!

    Rest Parameters `...`: Packs extra arguments into a List
      sum_all(prefix, ...nums) = prefix + str(sum(nums))
      sum_all("Total", 1, 2, 3)    → "Total3"

    Apply: Unpacks a List/Matrix into separate arguments
      args = [1, 2, 3]
      apply(sum_three, args)       ≡ sum_three(1, 2, 3)

  Lambda (Anonymous Functions)
  ──────────────────────
    (x) => expr                    Single parameter
    (x, y) => { stmt1; stmt2; }    Multi parameter block body

  List Comprehension
  ──────────────────────
    [expr for var in iterable]                Basic
    [expr for var in iterable if condition]    With filter
    [expr for x in A for y in B]              Nested (cartesian product)
    [expr for [a, b] in pairs]                Destructured

    Examples:
      [x^2 for x in range(10)]               → [0, 1, 4, 9, ..., 81]
      [x for x in range(20) if x % 2 == 0]   → [0, 2, 4, ..., 18]
      [x*y for x in range(3) for y in range(3)] → [0,0,0, 0,1,2, 0,2,4]
      [f"{k}={v}" for [k,v] in dict("a",1)]  → ["a=1"]

    Returns a row vector if all elements are real numbers, otherwise a List.

  Pipe Operator (|>) & Multi-line Rules
  ──────────────────────
    expr |> function                 Passes expr as the argument to function.
    Reads left-to-right. Equivalent to function(expr).

    Chaining:
      [3,1,4,1,5] |> sort |> unique |> reverse

    ★ Multi-line Rules:
    Because JC2 uses newlines to end statements, to split a pipe chain across 
    multiple lines, you MUST either wrap the entire chain in parentheses `(...)` 
    OR place the `|>` operator at the very end of the previous line:

      // Method 1: Wrap in parentheses (Recommended for leading pipes)
      result = ( [1,2,3,4,5]
          |> filter((x) => x > 2, _)
          |> map((x) => x^2, _)
          |> sum )
          
      // Method 2: Trailing pipes
      result = data |>
          sort |>
          reverse

  Default Parameter Values
  ──────────────────────
    f(x, y = 0, z = 1) = x + y * z
    f(5)                →  5       (y=0, z=1)
    f(5, 3)             →  8       (z=1)

    Rules:
    • Parameters with defaults must come AFTER required parameters.
    • Defaults are evaluated once at definition time.
    • Works in named functions, lambdas, class methods, and constructors.

  Comparison Operators (return 1 = true, 0 = false)
  ──────────────────────
    ==  !=  <  <=  >  >=
    • BigInt / Fraction  — exact, no rounding
    • double             — tolerance-aware (IEEE 754 ULP neighbourhood)
    • Complex            — == and != compare both parts
    • Matrix / String    — == / != element-wise; ordering throws error

    in                Membership / containment test (returns 1 or 0)
                        3 in [1,2,3]          → 1

  Logical Operators (short-circuit, return 1 or 0)
  ──────────────────────
    &&                AND    (right side not evaluated if left is false)
    ||                OR     (right side not evaluated if left is true)
    !expr             NOT    (prefix unary)
    Truthiness:
      Falsy:  0, 0.0, BigInt(0), Fraction(0/n), none, ""
      Truthy: everything else

  Built-in Constants & Constant Factory Functions
  ──────────────────────
    PI          3.14159265358979…        (overridable, deletable)
    E           2.71828182845904…        (overridable, deletable)
    i  /  I     Imaginary unit (i² = −1) (overridable, deletable)
    none        None value                (overridable, deletable)
    true / false 1.0 / 0.0                (overridable, deletable)

    Constants are ordinary global variables — you CAN overwrite or delete
    them. To restore them, use any of:
      resetConst()      Restores all 6: PI, E, i, I, true, false
      PI = pi()         Restore individually via factory function
      E = e()
      i = i()           (or just use the literal suffix: 3 + 4i)
      none = none()     

    Factory functions (always available, cannot be shadowed):
      pi()              → 3.14159265358979…
      e()               → 2.71828182845904…
      i()               → imaginary unit (0+1i)
      none()            → none value

    Tip: Prefer the literal suffix 3i / 4i over the variable i, because
    the variable can be overwritten (e.g., by a for loop). The suffix is
    a lexical token and is immune to variable shadowing.

  Elementary Functions
  ──────────────────────
    abs(x)              Absolute value
    sqrt(x)             Square root     sqrt(−4) → 2i
    cbrt(x)             Cube root
    root(x, y)          y-th root of x
    exp(x)              e^x
    log(x) / ln(x)      Natural logarithm  log(−1) → πi
    log(base, x)        Custom-base logarithm
    pow(x, y)           Same as x^y
    idiv(a, b)          Integer division (truncates toward zero)
    deg(x)              Radians to degrees
    rad(x)              Degrees to radians
    len(x)              Length / element count

  Type Conversion Functions
  ──────────────────────
    int(x)              Truncate to integer (toward zero)
                          int(3.7)        → 3
                          int(-3.7)       → -3
                          int(frac(7,2))  → 3
                          int(3+0i)       → 3
                          int("42")       → 42
    double(x)           Convert to floating point
                          double(frac(1,3)) → 0.333...
                          double(42)        → 42.0
                          double(3+0i)      → 3.0
    complex(x)          Convert to complex (imag=0)
                          complex(1.5)      → 1.5+0i
    complex(a, b)       Construct complex from parts
                          complex(3, 4)     → 3+4i
    matrix(r, c, ...)   Construct matrix from dimensions + elements
                          matrix(2, 2, 1, 2, 3, 4)       → RealMatrix
                          matrix(2, 2, 1+1i, 2, 3, 4-2i)    → ComplexMatrix
                          matrix(2, 2, "a", "b", "c", "d") → StringMatrix
                          matrix(3, 3)                    → 3×3 zero matrix

  Trigonometric & Hyperbolic  (also accept Complex & Matrix)
  ──────────────────────
    sin  cos  tan       asin  acos  atan  atan2(y, x)
    sinh  cosh  tanh

  Rounding & Sign  (scalar / Complex / Matrix)
  ──────────────────────
    floor(x, n)         Floor to n decimal places
    ceil(x, n)          Ceiling
    round(x, n)         Round half-away-from-zero
    trunc(x, n)         Truncate toward zero
    sgn(x)              Sign: −1, 0, or 1

  Formatting & I/O
  ──────────────────────
    format(fmt, ...)    Python-style string formatting
                          format("{:>10.2f}", PI)
    input()             Read a line from stdin
    clock()             High-resolution timer (seconds since epoch)
    sleep(seconds)      Pause execution

  Ternary Operator
  ──────────────────────
    cond ? a : b        Only the selected branch is evaluated.
                        Right-associative: x==1 ? "one" : "other"

  String Interpolation (f-strings)
  ──────────────────────
    f"text {expr} more text"
    f"x = {x}, y = {y}"           →  "x = 3, y = 4"
    f"pi = {PI::.4f}"             →  "pi = 3.1416"
    f"hex = {255::x}"             →  "hex = ff"
    f"{name::>10}"                →  "     Alice"

    Format specs use :: separator: {expr::spec}
      spec = [<>^][width][.prec][fdex]
    No ambiguity with ternary operator:
      f"{x > 0 ? x : -x}"         Works perfectly (single : is part of ternary)

  Raw Strings (r-strings)
  ──────────────────────
    r"C:\path\to\file"          Backslashes are literal, no escaping.
    r"(content with "quotes")"  Custom delimiter (empty tag).
    r"TAG(content)TAG"          Named delimiter for maximum safety.

  Built-in Function Overloading
  ──────────────────────
    Built-in functions (sin, cos, map, etc.) are protected against
    accidental redefinition with the SAME parameter count:
      sin(x) = x * 2             ✗ Error: conflicts with built-in (arity=1)
      sin(a, b) = a + b          ✓ OK: different arity, creates an overload
      sin(1)                     → 0.841... (built-in always takes priority)
      sin(1, 2)                  → 3        (user overload, no conflict)
    Variadic built-ins (print, list, cat, ...) cannot be overloaded at all.
)HELP" },

        { "complex", R"HELP(
═══ Complex Numbers ═══

  Construction
  ──────────────────────
    z = 3 + 4i           Imaginary suffix (recommended)
    w = 2i               Pure imaginary
    z = 3 + 4*i          Also works (i is a preset variable)
    sqrt(-1)             Automatic promotion → 1i

    z = complex(3, 4)    Constructor form → 3+4i
    z = complex(1.5)     Real → Complex with imag=0 → 1.5  (type becomes Complex)
    z = complex(3+4i)    Pass-through (already complex)

    The 'i' suffix is a lexical literal — it cannot be accidentally
    overwritten by loops or variable assignments:
      for (i in range(10)) { ... }   // variable 'i' is overwritten
      3 + 4i                          // still works! (4i is a literal)
      3 + 4*i                         // BROKEN (i is now 9)

    To recover the variable i after it has been overwritten:
      i = i()             // i() is a factory function, always available
      // Or simply use the literal suffix: 3 + 4i

    Numeric forms supported:
      1i                  Imaginary unit
      3.14i               Floating point imaginary
      1e3i                Scientific notation imaginary
      -2i                 Negative imaginary (unary minus applied)

  Property Extraction
  ──────────────────────
    Re(z)               Real part
    Im(z)               Imaginary part
    abs(z)              Modulus |z|
    arg(z)              Argument (radians, ∈ (−π, π])
    conj(z)             Complex conjugate

  Type Conversion
  ──────────────────────
    double(3+0i)        → 3.0    (only if imaginary part is zero)
    double(3+4i)        → Error  (nonzero imaginary part)
    int(3+0i)           → 3      (only if imaginary part is zero)
    iscomplex(z)        → 1      (type predicate, see: help typecheck)
    isreal(3+0i)        → 1      (real if imag ≈ 0)

  Automatic Promotion
  ──────────────────────
    All elementary functions (sin, cos, exp, log, sqrt, asin, acos …)
    seamlessly accept complex arguments and return complex results.

    exp(PI * 1i) + 1    →  0       (Euler's identity)
    asin(2)             →  complex

    Matrix operations also auto-promote:
    [1, 2] * 1i         → ComplexMatrix
    A + 3i              → ComplexMatrix
    matrix(2, 2, 1+1i, 2, 3, 4-2i)  → ComplexMatrix via constructor
)HELP" },

        { "fraction", R"HELP(
═══ Exact Rational Arithmetic (Fractions) ═══

  Fractions use BigInt numerator/denominator and auto-reduce via GCD.
  This allows infinite-precision rational arithmetic without floating
  point inaccuracies.

  Creation
  ──────────────────────
    frac(a, b)          Explicit:  frac(1, 3) → 1/3
    10 / 3              Integer ÷ Integer yields exact fraction

  Properties
  ──────────────────────
    num(f)              Numerator (BigInt)
    den(f)              Denominator (BigInt, always positive)

  Arithmetic
  ──────────────────────
    Fraction ⊕ Fraction   → exact Fraction
    Fraction ⊕ BigInt     → exact Fraction
    Fraction ⊕ double     → degrades to double
    Fraction ⊕ Complex    → degrades to Complex

  Type Conversion
  ──────────────────────
    double(frac(1,3))   → 0.333333…    (convert to floating point)
    int(frac(7,2))      → 3             (truncate toward zero)
    int(frac(-7,2))     → -3            (truncate toward zero)
    evalf(frac(1,3))    → 0.333333…    (alias for double())

  Type Predicates
  ──────────────────────
    isfrac(x)           → 1 if x is a Fraction type
    isint(frac(6,3))    → 1 (denominator reduces to 1)
    isreal(frac(1,3))   → 1 (fractions are real numbers)

  Examples
  ──────────────────────
    f = frac(1, 3)
    f * 3               →  1           (exact, reduced to BigInt if den=1)
    f + frac(1, 6)      →  1/2         (auto-reduced)
    f + 0.5             →  0.833333…   (double)
    f ^ 2               →  1/9         (exact power)
    f ^ -1              →  3           (reciprocal)
)HELP" },

        { "matrix", R"HELP(
═══ Matrix Construction, Access & Manipulation ═══

  Construction
  ──────────────────────
    A = [1, 2; 3, 4]            2×2 matrix
    B = [1, 2, 3]               1×3 row vector
    v = [1; 2; 3]               3×1 column vector
    
    Elements may be expressions:  [sin(PI), 2*i; 0, 1]
    Any complex element auto-promotes the whole matrix to ComplexMatrix.
    If any element is a string, the entire matrix becomes a StringMatrix.

  Constructor Function
  ──────────────────────
    matrix(r, c)                          r×c zero matrix
    matrix(r, c, e1, e2, ..., eN)         r×c matrix filled with elements
    
    Auto-detects element type:
      matrix(2, 2, 1, 2, 3, 4)             → RealMatrix
      matrix(2, 2, 1+1i, 2, 3, 4-2i)       → ComplexMatrix
      matrix(2, 2, "a", "b", "c", "d")     → StringMatrix
      matrix(2, 2, 1, "mixed", 3, 4)       → StringMatrix (mixed → string)

  Arithmetic & Scalars
  ──────────────────────
    A + B, A − B                Element-wise  (dimensions must match)
    A * B                       Matrix multiplication
    A / B                       Right division: A * inv(B)
    A ^ n                       Integer power (n<0 uses inverse)
    
    A + c,  c + A               ≡ A + c·I   (square only, diagonal shift)
    
    addS(A, c)  subS(A, c)  mulS(A, c)  divS(A, c)  powS  modS
      (Element-wise scalar broadcasting across ANY shape)

  Dimensions & Element Access
  ──────────────────────
    row(A) / cols(A)    Number of rows / columns
    len(A)              Total element count
    
    A[i, j]             Read/Write element at row i, col j
    A[i]                Vector element, or matrix row i (as vector)
    
    Submatrix Slicing (MATLAB/Python style):
      A[1:4, 2:5]       Extract block from row 1 to 3, col 2 to 4
      A[i, :]           Extract entire row i
      A[:, j]           Extract entire column j
      A[::-1, :]        Reverse matrix rows vertically

      Assignments into slices work as long as dimensions align:
        A[1:3, :] = [9, 9]     (Broadcast scalar)
        A[:, 0] = [1; 2; 3]    (Vector injection)

    get(A, r, c)        Function form of A[r,c]
    set(A, r, c, val)   Return new matrix with element changed (pure)

  Row / Column Operations
  ──────────────────────
    getR  getC  delR  delC  swapR  swapC  multiR  multiC  addR  addC

  Concatenation & Block Matrix Assembly
  ──────────────────────
    You can build matrices directly via sub-matrix merging:
      A = [1, 2; 3, 4]
      B = [5, 6; 7, 8]
      
      C = [A, B]          Horizontal concat: [1,2,5,6; 3,4,7,8]
      D = [A; B]          Vertical concat:   [1,2; 3,4; 5,6; 7,8]
      [A, zeros(2,2); id(2), B]   Build a 4×4 block matrix!

    Function equivalents:
      integR(A, B)        Horizontal  [A | B]
      integC(A, B)        Vertical    [A ; B]
      integD(A, B)        Diagonal block

    ★ CRITICAL WARNING (Array Literal Flattening):
      Because `[ ]` uses MATLAB-style concatenation, `[ [1, 2], [3, 4] ]` 
      does NOT create a nested List of Lists! The commas trigger horizontal 
      concatenation, resulting in a FLAT 1x4 RealMatrix: `[1, 2, 3, 4]`.
      To create true nested containers/multi-dimensional arrays of distinct
      objects, you MUST use the `list()` factory:
      L = list(list(1, 2), list(3, 4))

  Structure & Generators
  ──────────────────────
    trans(A)            Transpose  Aᵀ
    ctrans(A)           Conjugate transpose  A*
    gauss(A)            Reduced row echelon form (RREF)
    reshape(A, r, c)    Reshape  (element count must match)
   
    id(n)               n×n identity
    ones(n,c)           All-ones
    zeros(n,c)          All-zeros
    magic(n)            Magic square  (n ≥ 3)

  Type Predicates (see: help typecheck)
  ──────────────────────
    ismatrix(A)         Any matrix type
    isrealmat(A)        RealMatrix specifically
    iscomplexmat(A)     ComplexMatrix specifically
    isstringmat(A)      StringMatrix specifically
    isvector(A)         Row vector or column vector
    issquare(A)         Square matrix

  Conversion
  ──────────────────────
    toList(A)           Matrix → List of Lists (2D structure)
    toMatrix(L)         List of Lists → matrix (reverse)
    toArray(L)          Flat List → row vector
    toStrMat(A)         Any matrix → StringMatrix
)HELP" },

        {"linalg", R"HELP(
═══ Linear Algebra ═══
  All functions transparently support real and complex matrices.

  Scalar Properties
  ──────────────────────
    det(A)              Determinant
    rank(A)             Rank
    tr(A)               Trace
    norm(A)             Frobenius norm  ‖A‖_F
    cond(A)             Condition number  ‖A‖·‖A⁻¹‖
    perm(A)             Permanent

  Operations & Cofactors
  ──────────────────────
    inv(A)              Inverse  (Gauss-Jordan)
    adj(A)              Classical adjugate
    mpow(A, n)          A^n for integer n
    sub(A, r, c)        Matrix with row r & col c removed
    cof(A, r, c)        Cofactor: det(sub(A,r,c))
    Acof(A, r, c)       Algebraic cofactor: (−1)^(r+c) · cof

  Decompositions & Spectra
  ──────────────────────
    qr_Q(A), qr_R(A)           QR Decomposition (Modified Gram-Schmidt)
    lu_L(A), lu_U(A), lu_P(A)  PA = LU (Partial pivoting Doolittle)
    orth(A)                    Orthonormal column basis (Q from QR)
    null(A)                    Null-space basis (Homogeneous solution)
    eig(A)                     Eigenvalues (Hessenberg + QR iteration)
    eigvec(A)                  Eigenvector matrix
    diag(A)                    Diagonal eigenvalue matrix D
    diagP(A)                   Change-of-basis matrix P   (A = P D P⁻¹)
)HELP"},

        {"lineq", R"HELP(
═══ Linear Equation Systems  Ax = b ═══

  Primary Solver
  ──────────────────────
    lsolve(A, b)        Automatic solver via Gaussian Elimination:
                          • Unique solution     → exact x
                          • Infinite solutions  → one particular solution
                          • No solution         → least-squares approximation
                        A must be an M×N matrix, b must be an M×1 column vector.

  Diagnostics
  ──────────────────────
    linfo(A, b)         Print rank info and classify the system:
                          Outputs whether the system has a unique solution,
                          infinite solutions, or no exact solution.
    residual(A, x, b)   Residual vector  r = b − A*x

  Least-Squares (forced)
  ──────────────────────
    lstsq(A, b)         Normal-equation path: solves (A*A)x = A*b
)HELP"},

        {"matfunc", R"HELP(
═══ Matrix Transcendental Functions ═══
  Under the hood, JC2 evaluates these using Taylor series expansion 
  (with scaling-and-squaring) or matrix diagonalization.

  Functions
  ──────────────────────
    exp(A)              Matrix exponential  (e^A = I + A + A²/2! + ...)
    log(A) / ln(A)      Matrix logarithm    (inverse of exp)
    sqrt(A)             Matrix square root
    sin(A), cos(A)      Matrix trigonometric functions
    tan(A)              sin(A) * inv(cos(A))
    sinh(A), cosh(A)    Matrix hyperbolic functions
    tanh(A)             sinh(A) * inv(cosh(A))
    matpow(A, B)        General matrix-to-matrix power: exp(B * ln(A))

  Sanity-Check Identities
  ──────────────────────
    exp(A) * exp(−A)           ≈ I
    sin(A)^2 + cos(A)^2        ≈ I

  Tip: For pure integer powers, ALWAYS use mpow(A, n) or the ^ operator.
)HELP"},

        {"vector", R"HELP(
═══ Vector Geometry ═══
  Mathematical vectors are explicitly N×1 column matrices:  v = [1; 2; 3]
  (Row vectors like [1, 2, 3] are arrays — see 'help array')

  Generation
  ──────────────────────
    seq(a, b)           Produces column vectors for math use.
                          seq(1, 3)    → [1; 2; 3]

  Properties
  ──────────────────────
    dim(v)               Number of components (rows)
    vnorm(v)             Euclidean length / magnitude  ‖v‖
    normalize(v)         Unit vector  v / ‖v‖

  Products
  ──────────────────────
    dot(a, b)           Hermitian inner product (a* · b). Works in ℂ.
    cross(a, b)         Cross product (strictly 3-D).
    triple(a, b, c)     Scalar triple product: dot(a, cross(b, c))

  Projections & Angles
  ──────────────────────
    sproj(a, b)         Scalar projection of 'a' onto 'b'
    vproj(a, b)         Vector projection of 'a' onto 'b'
    angle(a, b)         Angle between 'a' and 'b' (in radians)

  Boolean Tests (return 1 or 0)
  ──────────────────────
    isperp(a, b)        Are vectors perpendicular?
    isparallel(a, b)    Are vectors parallel?
)HELP"},

        {"poly", R"HELP(
═══ Polynomial Equation Solver (degree 1–4) ═══
  JC2 internally features exact closed-form algebraic formulas
  (Linear, Quadratic, Cardano's, Ferrari's) to find all roots over ℂ.
  
  Returns a complex column vector of roots.

  Usage
  ──────────────────────
    solve(a, b)                  ax + b = 0             (linear)
    solve(a, b, c)               ax² + bx + c = 0       (quadratic)
    solve(a, b, c, d)            ax³ + … = 0            (Cardano)
    solve(a, b, c, d, e)         ax⁴ + … = 0            (Ferrari)
    solveEq(expr, "x")           Exact symbolic solver (see: help cas)
    
  Examples
  ──────────────────────
    solve(1, 0, −1)              x² = 1   → [1; −1]
    solve(1, 0, 0, −1)           x³ = 1   → cube roots of unity
    solve(i, −2, 4*i, 3, −1)     Complex coefficients work perfectly!
)HELP"},

        {"cas", R"HELP(
═══ Computer Algebra System (CAS) ═══
  JC2 features a powerful, exact Computer Algebra System for symbolic mathematics.

  ★ The Boundary: Numerical vs. Symbolic Computation
  ──────────────────────
    JC2 strictly separates Numerical and Symbolic domains:
    • Numerical (Default): Variables hold values (x = 5). Expressions evaluate 
      immediately to numbers, matrices, or fractions.
    • Symbolic (CAS): Variables hold AST nodes (x = sym("x")). Expressions 
      build algebraic trees instead of evaluating.
      
    You cross from Numerical → Symbolic by using `sym("x")` or string variables.
    You cross from Symbolic → Numerical by using `evalf()`, `subs()`, or `toFunc()`.

  ★ Automatic Symbolic Promotion
  ──────────────────────
    • Fractional Powers: Exact integers or fractions raised to fractional powers 
      (e.g., 2^(1/2)) no longer degrade to floating-point `double`. If they cannot 
      be evaluated exactly, they automatically promote to a Symbolic expression.
    • Function Nodes: Only standard built-in math functions (sin, cos, exp, log, 
      sqrt, etc.) can be captured as symbolic nodes (`SymFunc`). Passing a symbolic 
      variable to a user-defined function or an incompatible built-in will either 
      evaluate it immediately or throw a TypeError.

  Symbolic Variables & Expressions
  ──────────────────────
    x = sym("x")                 Create a symbolic variable
    expr = x^2 + 2*x + 1         Build exact symbolic expressions
    subs(expr, "x", 5)           Substitute x with 5 → 36
    subs(x + y, ["x", "y"], [1, 2]) Multiple substitution

  Algebraic Manipulation
  ──────────────────────
    expand((x+1)^3)              → x^3 + 3*x^2 + 3*x + 1
    factor(x^2 - 4)              → (x - 2) * (x + 2)
    simplify(expr)               Smart heuristic simplification
    contract(2*log(x) + log(y))  → log(x^2 * y)
    trigsimp(sin(x)^2 + cos(x)^2)→ 1

  Calculus (Exact Symbolic)
  ──────────────────────
    diff(sin(x^2), "x")          → 2 * x * cos(x^2)
    integ(x^2, "x")              → 1/3 * x^3
    integ(x^2, "x", 0, 1)        → 1/3 (Definite integral)
    limit(sin(x)/x, "x", 0)      → 1 (L'Hôpital's rule supported)
    taylor(sin(x), "x", 0, 5)    → x - 1/6 * x^3 + 1/120 * x^5

  Equation Solving
  ──────────────────────
    solveEq(x^2 - 2, "x")        → [sqrt(2), -sqrt(2)] (Exact roots!)
    solveEq(x^3 - 1, "x")        → [1, (-1)^(2/3), (-1)^(4/3)]

  Compilation & Evaluation
  ──────────────────────
    evalf(expr)                  Force evaluate to floating point
    evalv(expr)                  Evaluate preserving exact types (Fraction/Complex)
    f = toFunc(x^2 + y, ["x", "y"])  Compile AST to blazing-fast VM closure
    f(3, 4)                      → 13
)HELP"},

        { "calculus", R"HELP(
═══ Functions, Calculus & Tabulation ═══

  Defining Functions
  ──────────────────────
    f(x) = sin(x) + x^2                Single variable
    g(x, y) = sqrt(x^2 + y^2)          Multi variable
    h(a, b) = { t = a+b; t^2 }         Block body (auto-local t)
    f(x, k = 1) = sin(k * x)           Default parameters supported!

    Functions are compiled into bytecode closures executed by the VM.
    They automatically capture the surrounding scope at definition time.

  Built-in Function Overloading
  ──────────────────────
    You can define a user function with the same name as a built-in,
    as long as the parameter count is DIFFERENT:
      exp(a, b, c) = a + b + c     ✓ OK (built-in exp takes 1 arg)
      exp(x) = x                   ✗ Error (conflicts with built-in arity)
    When calling, built-in functions always take priority if the argument
    count matches a built-in signature.

  Calculus (Symbolic & Numerical)
  ──────────────────────
    JC2 supports both exact symbolic calculus (via the CAS engine) and 
    high-performance numerical approximations.

    Symbolic (Exact):
      diff(x^2, "x")       → 2 * x
      integ(x^2, "x")      → 1/3 * x^3
      limit(sin(x)/x, "x", 0) → 1
      (See `help cas` for full symbolic features)

    Numerical (Approximation):
      diff(f, x0)          Derivative f′(x₀)  (5-point central difference)
      integ(f, a, b)       Definite integral   (Simpson's 1/3, 100000 slices)
      integ(f, a, b, n)    Custom slice count n
      solveE(f, x0)        Find a root of f(x) = 0 near x₀  (Newton-Raphson)

  Tabulation (multivariate supported)
  ──────────────────────
    table(f, start, step, n)   1-D: evaluate f over an arithmetic sequence
    table(f, M)                M is N×k — evaluates f on each row
    table(f, v1, v2, …)        Bind multiple column vectors to f's parameters

  Lambda Functions (Shorthand)
  ──────────────────────
    Useful for higher-order functions and inline calculus:
      diff((x) => x^3, 2)             → 12.0  (derivative of x³ at x=2)
      integ((x) => x, 0, 1)           → 0.5
      solveE((x) => x^2 - 4, 3)       → 2.0

      map((x) => x^2, v)              Square every element
      filter((x) => x > 0, v)         Keep positives
      reduce((a, b) => a + b, v, 0)   Sum all elements
)HELP" },

        {"stat", R"HELP(
═══ Descriptive Statistics & Regression ═══

  Data lives in matrices or vectors (both row and column shapes are accepted).

  Central Tendency & Dispersion
  ──────────────────────
    mean(X)             Arithmetic mean
    median(X)           Median (50th percentile)
    mode(X)             Mode  (returns a row array if multimodal)
    var(X)              Population variance       (divided by N)
    svar(X)             Sample variance           (divided by N−1)
    std(X)              Population std dev
    sstd(X)             Sample std dev
    span(X)             Statistical range: max(X) − min(X)

  Extremes & Quantiles
  ──────────────────────
    max(X), min(X)      Extreme values
    perc(X, p)          p-th percentile  (0 ≤ p ≤ 100)
    sort(X)             Sorted copy  (always returns a row array)

  Bivariate Analysis
  ──────────────────────
    X and Y must have the exact same number of elements.
    
    cov(X, Y)           Population covariance
    corr(X, Y)          Pearson correlation coefficient  r
    rsq(X, Y)           Coefficient of determination  R²
    regress(X, Y)       Linear regression Y = a + bX
                        Automatically prints the model equation & returns [a, b].
)HELP"},

        { "prob", R"HELP(
═══ Probability Distributions & Hypothesis Tests — Native Module ═══

  Requires: import "prob"

  Distributions are first-class objects backed by native C++ code.
  After importing, Distribution objects are class instances
  (type → "Distribution").

  Creating Distributions
  ──────────────────────
    import "prob"

    CONTINUOUS:
      Normal()              Standard normal N(0,1)
      Normal(mu, sigma)     General normal N(μ, σ²)
      TDist(df)             Student's t-distribution
      Chi2(df)              Chi-squared
      FDist(d1, d2)         F-distribution
      ExpDist(lambda)       Exponential
      GammaDist(shape,rate) Gamma
      BetaDist(a, b)        Beta
      Uniform(a, b)         Continuous uniform on [a,b)

    DISCRETE:
      Binom(n, p)           Binomial
      Poisson(lambda)       Poisson
      Geom(p)               Geometric (# trials until first success)

  Distribution Methods (Object-Oriented API)
  ──────────────────────
    D.pdf(x)              Probability density / mass
    D.cdf(x)              Cumulative distribution P(X ≤ x)
    D.quantile(p)         Inverse CDF: find x such that P(X ≤ x) = p
    D.mean()              Distribution mean
    D.var()               Distribution variance
    D.std()               Distribution std dev
    D.sample(n)           Draw n random samples → returns a row array

    * Global variants like distInfo(D), pdf(D, x), and mean(D) are also 
      supported for backward compatibility and functional piping.

  Special Math Functions (available after import "prob")
  ──────────────────────
    gamma(x)              Gamma function Γ(x)
    lgamma(x)             Log-gamma ln(Γ(x))
    betaFn(a, b)          Beta function B(a,b) = Γ(a)Γ(b)/Γ(a+b)
    erf(x)                Error function
    erfc(x)               Complementary error function 1-erf(x)

  Hypothesis Tests (returns [statistic, df, p-value])
  ──────────────────────
    ttest(X)              One-sample t-test:  H0: μ = 0
    ttest(X, mu0)         One-sample t-test:  H0: μ = μ0
    ttest2(X, Y)          Welch two-sample:   H0: μ_X = μ_Y
    ttestP(X, Y)          Paired t-test:      H0: mean(X-Y) = 0
    chi2test(obs, exp)    Chi-squared goodness-of-fit

    Significance levels: *** (p<0.001)  ** (p<0.01)  * (p<0.05)  n.s.

  Example
  ──────────────────────
    import "prob"
    d = Normal(100, 15)
    d.cdf(130)                   → 0.9772  (97.72%)
    d.quantile(0.95)             → 124.67
    d.mean()                     → 100
    s = d.sample(1000)
    mean(s)                      ≈ 100
)HELP" },

        { "bigint", R"HELP(
═══ Arbitrary-Precision Integers & Number Theory ═══

  Every integer literal in JC2 is parsed natively as a base-10⁹ BigInt.
  It can grow to millions of digits. Memory is your only limit.

  Integer ÷ Integer → Exact Fraction.

  Type Conversion
  ──────────────────────
    int(3.7)            → 3           (truncate toward zero)
    int(-3.7)           → -3
    int(frac(7,2))      → 3           (BigInt floor division)
    int("12345")        → 12345       (parse from string)
    double(42)          → 42.0        (to floating point)
    isbigint(x)         → 1 if x is BigInt type

  Combinatorics (Exact BigInt Results)
  ──────────────────────
    factorial(n)         n!
    fib(n)               Fibonacci Fₙ
    C(n, k)              Binomial coefficient
    A(n, k)              Permutations (Arrangements)
    catalan(n)           Catalan number

  Divisibility & Primes
  ──────────────────────
    gcd(a, b), lcm(a, b), digits(n)
    isPrime(n)           Deterministic Miller-Rabin test
    isprime(n)           Lowercase alias (same function)
    nextPrime(n)         Smallest prime > n
    nthPrime(k)          k-th prime (requires mounted Prime.txt)
    primePi(n)           π(n) — number of primes ≤ n
    factor(n)            Returns an N×2 matrix: [Prime, Exponent]

  Arithmetic Functions & Modular Arithmetic
  ──────────────────────
    phi(n)               Euler's totient
    divisors(n)          Divisor count
    sigma(n, k)          Sum of k-th powers of divisors
    omega(n), bigOmega(n) Prime factor counts
    mobius(n)            Möbius function
    isPerfect(n)         Perfect number test
    mod(a, b)            Mathematical mod (always ≥ 0)
    modpow(a, e, m)      aᵉ mod m

  Numeric Predicates (see also: help typecheck)
  ──────────────────────
    isint(x)             Is x an integer? (BigInt, or integer-valued double/Fraction)
    iseven(n)            Is n even?
    isodd(n)             Is n odd?
    isprime(n)           Is n prime?
    ispositive(n)        Is n > 0?
    isnegative(n)        Is n < 0?
    iszero(n)            Is n = 0?
)HELP" },

        {"base", R"HELP(
═══ Radix Conversion & Bitwise Operations ═══

  BaseNum wraps a standard decimal BigInt inside a radix shell (2 to 36+).

  Conversion Functions
  ──────────────────────
    base(val, r)         Wrap an integer `val` in a base-`r` display shell.
                           base(255, 16) → [FF]_16
    bnum("str", r)       Parse a string directly as base `r`.
                           bnum("FF", 16)     bnum("45_63_2", 64)
    changeBase(v, r)     Re-display an existing BaseNum in a new base `r`.
    data(b)              Strip the radix shell → return plain BigInt.

  Arithmetic Assimilation
  ──────────────────────
    BaseNums support  +  −  *  /  %  ^  with base assimilation:
    If one operand is base 10 (or a plain integer), the result inherits the 
    BaseNum's radix. Two different non-10 bases cannot be mixed.

  Bitwise Operations (Base-2 Exclusive)
  ──────────────────────
    BaseNums automatically support native bitwise operators when radix = 2.
    (Wrap your inputs using `base(x, 2)` before passing them here).
    a & b                AND (Native operator)
    a | b                OR  (Native operator)
    a &= b               Compound AND assignment
    a |= b               Compound OR assignment
    bitxor(a, b)         XOR (Function only, since `^` is used for math power)
    bitnot(a, w)         NOT (auto-aligns to bytes, or explicitly w-bits)
    bitshift(a, n)       Logical Shift: Left (n > 0) / Right (n < 0)

)HELP"},

        { "sys", R"HELP(
═══ System, Generators & Runtime ═══

  Command-Line Usage & VM Switches
  ──────────────────────
    JunkCalculator2                    Interactive REPL
    JunkCalculator2 script.jc2         Run a script file
    JunkCalculator2 script.jc2 -d      Run with bytecode disassembly
    JunkCalculator2 script.jc2 --debug Run with interactive step-debugger
    JunkCalculator2 script.jc2 --profile Run and print performance report

    REPL Dynamic Switches (can be toggled anytime):
      -d on / off         Enable/disable real-time bytecode disassembly
      --debug on / off    Enable/disable global step-debugger
      --profile on / off  Enable/disable execution profiler

  Interactive Debugger & Profiler
  ──────────────────────
    breakpoint()          Suspends VM execution and opens the debug console.
    debugger()            Alias for breakpoint().
                          Type `help` inside `(jc2-dbg)` to see commands 
                          like `step`, `continue`, `stack`, and `p <var>`.
                          
    Profiler Mode         When `--profile on`, JC2 benchmarks every opcode 
                          and function call, outputting a highly detailed 
                          flame/time report upon execution completion.

  Random Number Generation (Mersenne Twister)
  ──────────────────────
    rand()  /  rand(min, max)            Continuous [min, max)
    randint(min, max)                    Discrete [min, max]
    randmat(r, c, min, max)              Matrix of doubles
    randimat(r, c, min, max)             Matrix of integers
    randc() / randcmat()                 Complex equivalents

  System Constants & Recovery
  ──────────────────────
    PI, E, i, I, true, false are ordinary global variables. They can be
    freely overwritten or deleted. To recover them:

    resetConst()          Restore all 6 constants at once
    PI = pi()             Restore individually via factory function
    E = e()               (factory functions are built-in and permanent)
    i = i()               (or just use the literal suffix: 3 + 4i)
    none = none()         

    Factory functions (always available, cannot be shadowed):
      pi()    → 3.14159265358979…
      e()     → 2.71828182845904…
      i()     → imaginary unit (0+1i)
      none()  → none value (falsy, used to represent "no value")

  Prime Engine (Paged Streaming I/O)
  ──────────────────────
    JC2 streams `Prime.txt` via 64 KB zero-RAM buffers.
    sysinfo()                    Current prime database status
    mountPrimes("path")          Redirect the engine to a custom prime file
    buildIndex()                 Build an O(1) anchor tree in RAM

  Garbage Collector (Mark-and-Sweep)
  ──────────────────────
    JC2 runs an automatic background Garbage Collector to track reference 
    types (Lists, Dicts, Instances, Closures) and prevent cyclic memory leaks.
    
    gcinfo()              Print current heap status (tracked objects, thresholds)
    gc()                  Force a standard GC sweep, returning freed object count
    gc(true)              Aggressive sweep (clears the 'ANS' variable first to 
                          prevent it from shielding dead objects from collection)

  Workspace & Path Management
  ──────────────────────
    All workspace operations are function-based:

    setWorkspace("path")         Change workspace directory
    setWorkspace("default")      Reset to ./data/
    getWorkspace()               Returns current workspace path as string
    pwd()                        Print both script dir and workspace dir

    save <name>                  REPL command: snapshot variables → data/<name>.jc2
    load <name>                  REPL command: restore saved workspace

  Script Execution & Path Context
  ──────────────────────
    run("path")                  Execute a .jc2 script file (function form)

    Executing a script automatically pushes its directory onto the
    Path Stack. All relative file operations (readFile, import) inside
    the script resolve relative to the script's own location.

    When the script finishes or errors out, the directory is popped.

  REPL Syntax Highlighting
  ──────────────────────
    JC2 automatically colorizes the REPL output by value type:
      Numbers (double/BigInt/Fraction)   → Yellow
      Complex                            → Magenta
      Strings                            → Green
      Matrices                           → White
      Functions/Classes                  → Blue
      Dicts/Lists                        → Cyan
      BaseNum                            → Bright Cyan
      Errors                             → Red

    Commands:
      color on              Enable colors (default)
      color off             Disable colors (for piping output)
      color("on")           Function form (usable in scripts)
      highlight("code")     Returns a colorized version of JC2 code

    Colors are automatically enabled on Windows via Virtual Terminal
    Processing.

  Native Module System
  ──────────────────────
    JC2 supports native C++ modules compiled into the executable.
    They provide high-performance functionality without external files.

    modules()                   List all available native modules
    import "moduleName"         Load a module (deduplicated, instant)

    Currently available:
      image    BMP image generation, plotting, drawing primitives
      prob     Probability distributions, special functions, hypothesis tests
      json     JSON serialization & deserialization

    To see details:
      help image              help prob              help json

    Native modules take priority over .jc2 files with the same name.
    Importing the same module twice is a safe no-op.
    After import, module functions are indistinguishable from built-ins.
)HELP" },

        { "control", R"HELP(
═══ Control Flow ═══

  Truthiness
  ──────────────────────
    Falsy:  0, 0.0, BigInt(0), Fraction(0/n), none, ""
    Truthy: Everything else (non-empty strings, matrices, classes, instances)
    bool(x)             Explicit conversion to 1.0 or 0.0

  Logical Operators
  ──────────────────────
    && (short-circuit AND), || (short-circuit OR), !expr (NOT)

  If / Else
  ──────────────────────
    if (cond) { body } else if (c2) { body } else { body }
    
    ★ Single-line Shorthand: Braces {} are OPTIONAL for single statements!
      if (HP <= 0) die() else play_hit_sound()
      
    Returns the value of the final evaluated expression in the selected branch.

  For / While Loops
  ──────────────────────
    while (cond) { body }
    for (init; cond; update) { body }
    
    ★ Single-line Shorthand:
      while (i < 10) i += 1

  For-In Loop
  ──────────────────────
    for (x in iterable) { body }
    Supported: arrays, column vectors, matrix rows, strings, lists, dicts.

    Destructured For-In:
      for ([var1, var2] in iterable) { body }
      for ([key, val] in dict("a", 1)) print(key)   // No braces needed!

  Switch
  ──────────────────────
    switch (expr) {
        case val1, val2: { body }
        default:         { body }
    }
    Expression returns the matched branch value. No fall-through.

  Control Statements & Scope Safety
  ──────────────────────
    break              Exit innermost loop
    continue           Skip to next iteration
    return expr        Exit function returning the evaluated expr
    
    *Note: Single-line statements (without braces) are inherently wrapped in 
     implicit blocks. Any auto-local variable initialized inside a single-line 
     statement remains securely isolated and will NOT pollute the outer scope!
)HELP" },

        { "scope", R"HELP(
═══ Variable Scoping & Protection Rules ═══

  Top Level (REPL)
  ──────────────────────
    All variables assigned at the top level are global. 
    Blocks `{}`, `for`, and `while` do NOT create isolated scopes at the top.

  Inside Functions (Auto-Local)
  ──────────────────────
    All *newly assigned* variables inside a function or a method are 
    automatically local to that function's execution scope.

    Global Declaration (Variable Lifting)
  ──────────────────────
    To read and *modify* an outer/global variable from inside a function,
    declare it via `global` so it doesn't get shadowed as a new local:
      counter = 0

    ★ Inline Shorthand (Recommended):
      bump() = global counter += 1      // Modifies outer 'counter' instantly!
      reset() = global counter = 0      // Force assignment to outer scope.
    
    ★ Multi-declaration:
      bump(x) = { 
          global counter, last_add      // Declare multiple
          counter += x
          last_add = x
      }

  Closures (Automatic Environment Capture)
  ──────────────────────
    Lambdas and inner functions automatically capture the enclosing 
    environment at creation time (via deep value copy).
      makeAdder(n) = { return (x) => x + n }

  Pass-by-Reference (ref)
  ──────────────────────
    Force a function parameter to mutate the original outer variable.
      addOne(ref x) = { x = x + 1 }

  Default Parameter Values
  ──────────────────────
    f(x, y = 0) = x + y
    Required parameters must precede parameters with defaults.
    Defaults are evaluated *once at definition time*.

  Destructuring & Scope
  ──────────────────────
    Destructured variables obey auto-local rules:
      f() = { [a, b] = [10, 20]; return a + b }   // a, b are local
    Use 'global' to project destructured variables into the outer scope.

  Constants (const)
  ──────────────────────
    const G = 9.81       Immutable — modification throws an error
    G = 10               ✗ Error: Cannot modify const variable 'G'
    delete G             ✓ Permitted: force-remove any variable, including const

    All variables (including const and system constants like PI) can be 
    deleted with `delete`. Use `resetConst()` or factory functions like
    `pi()` to recover system constants after deletion.

  System Constants (PI, E, i, I, true, false)
  ──────────────────────
    These are ordinary global variables — they CAN be overwritten:
      PI = 99             ✓ Allowed (PI is now 99)
      delete PI           ✓ Allowed (PI is removed entirely)
      PI = pi()           ✓ Restore via factory function
      resetConst()        ✓ Restore all 6 at once

  Built-in Function Protection
  ──────────────────────
    Built-in functions (sin, cos, map, sort, etc.) live in a separate
    native table and CANNOT be deleted or directly overwritten.

    However, you CAN create a user function with the same name if it
    has a DIFFERENT parameter count (overloading):
      sin(x) = x * 2             ✗ Error: conflicts with built-in (arity=1)
      sin(a, b) = a + b          ✓ OK: different arity
      sin(1)                     → 0.841... (built-in always wins)
      sin(1, 2)                  → 3        (user overload kicks in)
      delete sin                 → only removes the user overload

    Variadic built-ins (print, list, cat, dict, ...) cannot be
    overloaded at all — they accept any number of arguments.

  Variable Lifecycle Summary
  ──────────────────────
    Entity            Overwrite?   Delete?    Recover?
    ─────────────────────────────────
    User variable     ✓            ✓          —
    const variable    ✗            ✓          —
    System constant   ✓            ✓          resetConst() / pi()
    Built-in func     overload*     user only   automatic (native table)
    
    * Only with a different parameter count
)HELP" },

        { "string", R"HELP(
═══ String Functions ═══

  Strings are created with either double quotes ("") or single quotes (''). 
  This alternating mechanism allows you to embed quotes effortlessly without 
  needing backslash escapes:
    s1 = "hello world"
    s2 = 'He said "Hello!"'
    s3 = "It's perfectly fine."  

  Conversion
  ──────────────────────
    str(x)              Converts any value → string (calls __str__ on instances)
    eval("expr")        Parse & evaluate the string as JC2 code
    type(x)             Returns the type name ("double", "String", etc.)
    ord("A") / chr(65)  ASCII code conversion
    parseNum("42")      Parse string → number (BigInt or double)

  Escape Sequences
  ──────────────────────
    \n  \t  \\  \"  \'  \r  \0

  Length & Indexing (0-based, negative safely wraps)
  ──────────────────────
    len(s)              String length                  
    s[i] / s[-1]        Character at index   
    charAt(s, i)        Function form of s[i]
    s[start : end]      Slice [start, end)
    s[start:end:step]   Slice with stepping
                          "hello"[::-1] → "olleh" (String reversal!)

  Substrings
  ──────────────────────
    substr(s, start)          From start index to end
    substr(s, start, length)  Length characters from start

  Search
  ──────────────────────
    find(s, sub, pos)         Index of first match (-1 if absent)
    contains(s, sub)          1 if found, 0 otherwise
    startsWith / endsWith     1 or 0
    "sub" in s                Boolean test (identical to contains)

  Transformation
  ──────────────────────
    upper(s) / lower(s) / trim(s) / replace(s, old, new) / repeat(s, n)

  Concatenation & Splitting
  ──────────────────────
    "a" + "b"                   String concatenation
    "abc" * 3                   String repetition → "abcabcabc"
    3 * "ha"                    Same, commutative → "hahaha"
    concat(a, b, c, ...)        Arbitrary type concatenation → string
    split(s, delim)             Split into a List

  String Predicates (return 1 or 0)
  ──────────────────────
    isstring(x)         Is x a string type?
    isalpha(s)          All characters are alphabetic?
    isdigit(s)          All characters are digits?
    isalnum(s)          All characters are alphanumeric?
    isspace(s)          All characters are whitespace?
    isupper(s)          All alphabetic characters are uppercase?
    islower(s)          All alphabetic characters are lowercase?
    isempty(s)          Is the string empty (length 0)?

  String Interpolation (f-strings)
  ──────────────────────
    f"text {expr} text"             Embed any expression
    f"Hello, {name}!"               Variable interpolation
    f"area = {PI * r^2::.2f}"       With format spec (:: separator)

  Raw Strings (r-strings)
  ──────────────────────
    r"text"              No escape processing — backslashes are literal.
    r"C:\Users\name"     → "C:\Users\name"

    Custom Delimiter (for patterns containing parentheses):
      r"TAG(content)TAG"
      r"RE((\d+)-(\d+))RE"    ← Use this for regex patterns starting with (

  StringMatrix — Tabular String Data
  ──────────────────────
    ["hello", "world"]            Creates a StringMatrix (1×2)
    ["a", "b"; "c", "d"]          Creates a StringMatrix (2×2)

    ★ UNIVERSAL COMPATIBILITY:
    StringMatrix now supports ALL array manipulation functions:
      push, prepend, insert, removeAt, slice, reverse, flatten,
      unique, indexOf, count, join, map, filter, reduce, sort,
      any, all, countIf, zip, cat
    
    Examples:
      push(["a","b"], "c")                    → ["a","b","c"]
      sort(["banana","apple","cherry"])       → ["apple","banana","cherry"]
      map((s) => upper(s), ["hello","world"]) → ["HELLO","WORLD"]
      filter((s) => len(s) > 3, ["hi","hello"]) → ["hello"]
      join(["x","y","z"], "-")                → "x-y-z"
      reverse(["a","b","c"])                  → ["c","b","a"]

    Conversion & Utilities:
      toStrMat(A)                         Convert any matrix → StringMatrix
      toList(["a","b"])                   StringMatrix → List
      matrix(2, 2, "a", "b", "c", "d")    StringMatrix via constructor
      strmat(r, c, ...)                   Construct StringMatrix
      strvec(...)                         Construct column StringMatrix
      strrow(...)                         Construct row StringMatrix
      strfill(str, n)                     Create 1xN StringMatrix filled with str
      strfind(mat, str)                   Find string in StringMatrix
      strjoin(mat, delim)                 Join StringMatrix elements
      strsort(mat)                        Sort StringMatrix elements
)HELP" },

{ "array", R"HELP(
═══ Array / Data Functions ═══
  Arrays are structurally defined as row vectors (1×N matrices): `[1, 2, 3]`
  
  ★ UNIVERSAL COMPATIBILITY:
  All array functions below work seamlessly across FOUR container types:
    • RealMatrix      [1, 2, 3]
    • ComplexMatrix    [1+1i, 2+2i]
    • StringMatrix    ["hello", "world"]
    • List            list(1, "hi", [1,2])
  Functions automatically preserve the input type in their output.

  CRITICAL: Unlike Lists, Arrays/Matrices use VALUE SEMANTICS. 
  Assigning `A = B` creates a completely independent deep copy. Modifying `A` 
  will never affect `B`. This guarantees high performance and math safety.

  Element Access
  ──────────────────────
    v[i]                Element at zero-indexed position (negative wraps)
    v[i] = val          Modify element in-place
    first(v) / last(v)  Read first / last element (non-destructive)
    pop(v)              Remove & return LAST element
                          List:   O(1) destructive (mutates original in-place)
                          Matrix: non-destructive (equivalent to last())
    shift(v)            Remove & return FIRST element
                          List:   O(n) destructive (mutates original in-place)
                          Matrix: non-destructive (equivalent to first())
    len(v)              Element count

  Adding & Removing  (Returns new container of same type)
  ──────────────────────
    push(v, val)        Append to end
    prepend(v, val)     Insert at beginning
    insert(v, idx, val) Insert at index
    removeAt(v, idx)    Remove at index (negative wraps)

  Slicing (Native Syntax — works on ALL types)
  ──────────────────────
    v[start : end]            Extract elements [start, end)
    v[start : end : step]     Extract with step (Python-style)
    v[start :]                From start to the end
    v[: end]                  From beginning to end
    v[:]                      Full copy
    v[::-1]                   Complete reversal

    All indices support negative wrapping:
      v[-3 : -1]              Extract 3rd-to-last to 2nd-to-last

    Function form (also supports all types):
      slice(v, start)         From start to end
      slice(v, start, end)    From start to end (exclusive)

  Structure Operations
  ──────────────────────
    reverse(v)          Reverse order (works on String, Matrix, List)
    flatten(M)          Flatten 2D matrix or nested List → 1D
    unique(v)           Remove duplicates (tolerance-aware for doubles)

  Search
  ──────────────────────
    indexOf(v, val)     First index of val (-1 if absent)
    count(v, val)       Count occurrences
    val in v            Membership test → returns 1 or 0

  Destructuring
  ──────────────────────
    [a, b, c] = [10, 20, 30]         Unpack into variables
    [first, _, last] = [1, 2, 3]     Discard with `_`

  Generation
  ──────────────────────
    range(n)            [0, 1, ..., n-1]
    range(a, b, step)   Custom stepping
    fill(val, n)        n copies of val
    linspace(a, b, n)   n evenly-spaced points from a to b

  List Comprehension
  ──────────────────────
    [x^2 for x in range(10)]              Generate inline
    [x for x in data if x > 0]           Filter + transform
    See: `help basic` (List Comprehension section)

  Functional Programming (Works on ALL container types)
  ──────────────────────
    map(f, v)           Apply f to each element, returns same container type
                          map((x) => x*2, [1,2,3])              → [2,4,6]
                          map((s) => upper(s), ["a","b"])        → ["A","B"]
                          map((z) => z*2, [1+1i, 2+2i])         → [2+2i, 4+4i]
                          map((x) => x*10, list(1,2,3))         → list(10,20,30)
    filter(f, v)        Keep elements where f returns truthy
                          filter((x) => x > 2, [1,2,3,4])       → [3,4]
                          filter((s) => len(s) > 3, ["hi","hello"]) → ["hello"]
    reduce(f, v, init)  Left fold
                          reduce((a,b) => a+b, [1,2,3,4])       → 10
                          reduce((a,b) => a+"+"+b, ["x","y","z"]) → "x+y+z"
    any(f, v)           1 if any f(x) is truthy
    all(f, v)           1 if all f(x) are truthy
    countIf(f, v)       Count elements where f(x) is truthy

  Sorting (Works on ALL container types)
  ──────────────────────
    sort(v)             Natural sort (numeric or lexicographic)
                          sort([3,1,4])                  → [1,3,4]
                          sort(["banana","apple"])        → ["apple","banana"]
    sort(v, cmp)        Custom comparator function
                          sort([3,1,4], (a,b) => a > b)  → [4,3,1]  (descending)
                          sort(["bb","a"], (a,b) => len(a) < len(b)) → ["a","bb"]

  Accumulation
  ──────────────────────
    sum(v)              Sum of all elements
    prod(v)             Product of all elements
    cumsum(v)           Cumulative sum     [1,2,3] → [1,3,6]
    cumprod(v)          Cumulative product  [1,2,3] → [1,2,6]
    diffs(v)            Adjacent differences [10,20,35] → [10,15]

  Concatenation & Joining
  ──────────────────────
    cat(a, b, ...)      Concatenate multiple arrays/lists
    join(v, delim)      Join elements into a string with delimiter
                          join([1,2,3], "-")             → "1-2-3"
                          join(["a","b","c"], ", ")       → "a, b, c"
    zip(a, b)           Pair-wise merge → N×2 matrix or list of pairs
    toStrVec(v)         Convert List/StringMatrix to column StringMatrix

  Pipe Operator
  ──────────────────────
    Chains data through multiple transformations left-to-right:
      [5,3,1,4,2] |> sort |> reverse |> (v) => slice(v, 0, 3)
      // → [5, 4, 3]

    See: `help basic` (Pipe Operator section)
)HELP" },

{ "list", R"HELP(
═══ List (Heterogeneous Dynamic Array) ═══
  Lists can store ANY value type available in JC2, including other Lists, 
  Dicts, matrices, strings, and function closures.

  ★ UNIVERSAL COMPATIBILITY:
  All array functions (push, slice, map, filter, reduce, sort, join, etc.)
  now work natively on Lists. See `help array` for the complete reference.

  Creation
  ──────────────────────
    list()                         Creates an empty list
    list(1, "hello", [1;2])        Stores mixed types seamlessly
    toList([1, 2, 3])              Convert array → list of doubles
    toList([1+1i, 2+2i])           Convert ComplexMatrix → list
    toList(["a", "b"])             Convert StringMatrix → list
    toArray(L)                     Convert flat list → row vector (must be numeric)
    toMatrix(L)                    Convert nested List → Matrix/StringMatrix
    Auto-Degradation from [...]:
      JC2 automatically degrades a [...] literal to a List if any element
      is a non-scalar type (List, Dict, Instance, Function, Matrix):
        [1, 2, 3]                    → RealMatrix (all numbers)
        [1, "hello"]                 → StringMatrix (contains string)
        [1, list(2, 3)]              → List  (contains a List)
        [Point(1,2), Point(3,4)]     → List  (contains instances)
        [sin, cos, tan]              → List  (contains functions)
        [{a: 1}, {b: 2}]             → List  (contains Dicts)
        [[1,2], [3,4]]               → List  (contains matrices)

  Access & Modification (0-indexed, negative wraps)
  ──────────────────────
    L[i]                Read element at index i
    L[i] = val          Write or replace element
    nested[1][0]        Chained indexing works natively

  Adding & Removing
  ──────────────────────
    add(L, val)             Append value (same as push)
    remove(L, idx)          Remove at specific index
    clear(L)                Erase all elements

    Legacy functions (also work):
    push(L, val)            Append value to the end
    prepend(L, val)         Insert value at the beginning
    insert(L, idx, val)     Insert value at a specific index
    removeAt(L, idx)        Remove element at specific index

  Element Access (Destructive Operations — List Only)
  ──────────────────────
    first(L) / last(L)     Read endpoints (non-destructive)
    pop(L)                  Remove & return last element — O(1) in-place
    shift(L)                Remove & return first element — O(n) in-place
    len(L)                  Number of elements

    Because Lists use reference semantics, pop() and shift() MUTATE
    the original list directly:
      L = list(1, 2, 3)
      pop(L)                → 3     (L is now [1, 2])
      shift(L)              → 1     (L is now [2])

  Slicing (Native Syntax)
  ──────────────────────
    L[start : end]          List slice [start, end)
    L[start : end : step]   List slice with stepping (negative wraps)
    
    Function form:
    slice(L, start, end)    Equivalent to L[start:end]

  Structure
  ──────────────────────
    reverse(L)              Returns a reversed copy
    cat(L1, L2, ...)        Concatenate multiple lists
    flatten(L)              Recursively flatten nested lists into 1D
                              flatten(list(1, list(2, 3))) → [1, 2, 3]
    unique(L)               Remove duplicates (deep equality)

  Search
  ──────────────────────
    indexOf(L, val)         First index of val (-1 if absent)
    count(L, val)           Count matching occurrences
    val in L                Membership test → returns 1 or 0

  Sorting
  ──────────────────────
    sort(L)                 Sorts by string representation (lexicographic)
    sort(L, cmp)            Sort with custom boolean comparator
                              sort(L, (a, b) => a < b)

  Functional Programming (Full support — identical to arrays)
  ──────────────────────
    map(f, L)               Apply f(x) to each element → new list
    filter(f, L)            Keep elements where f(x) is truthy → new list
    reduce(f, L)            Left fold using first element as initial
    reduce(f, L, init)      Left fold with explicit accumulator
    any(f, L)               1 if ANY f(x) is truthy
    all(f, L)               1 if ALL f(x) are truthy
    countIf(f, L)           Count elements where f(x) is truthy

  Accumulation
  ──────────────────────
    cumsum(L)               Cumulative sum
    cumprod(L)              Cumulative product
    diffs(L)                Adjacent differences

  String Interop
  ──────────────────────
    join(L, ", ")           Join all elements into a string
    zip(L1, L2)             Pair-wise merge → list of 2-element lists

  Conversion
  ──────────────────────
    toList(matrix)          Matrix → List of Lists (2D structure)
    toList(vector)          Vector/Array → Flat List
    toList(string)          String → List of characters
    toMatrix(L)             List → Matrix (auto-detects type)
                              list(1,2,3) → [1, 2, 3]  (row vector)
                              list(list(1,2), list(3,4)) → [1,2; 3,4]
    toStrVec(L)             List → Column StringMatrix

  When to use List vs Array?
  ──────────────────────
    Array   [1, 2, 3]        Homogeneous doubles. Fast math. Use for numerics.
    List    list(1, "a")     Heterogeneous. Use for mixed data.
    Both support the SAME set of manipulation functions.

  Reference Semantics & Memory
  ──────────────────────
    Unlike Arrays/Matrices (which are passed by value and deep-copied),
    Lists use strictly REFERENCE SEMANTICS.
    
      L1 = list(1, 2, 3)
      L2 = L1                   // L2 and L1 share the same data
      L2[0] = 99                // L1[0] is now also 99!

  Garbage Collection
  ──────────────────────
    Lists are tracked by the VM's Mark-and-Sweep Garbage Collector.
      gcinfo()                  View tracked objects
      gc()                      Force memory sweep
)HELP" },

        {"dict", R"HELP(
═══ Dictionary (Key-Value Store) ═══

  Dicts store unordered key-value string mappings in JC2. Keys are exclusively
  strings, but values can be of any type.

  Creation
  ──────────────────────
    d = dict()                          Empty dictionary
    d = {name: "Alice", age: 30}        Literal syntax (recommended)
    d = {"name": "Alice", "age": 30}    Quoted string keys also work
    
    Shorthand Properties (Variables directly to Dict):
      name = "Bob"; age = 25
      d = { name, age }                 → {"name": "Bob", "age": 25}
    For computed keys, use the function form:
      dict(dynamic_key, 42)             → {"dynamic": 42}

  Access & Modification
  ──────────────────────
    d["key"]                            Read / Write the value 
    d["key"] += 1                       Compound assignment directly on the value

  Dot Operator Syntax Sugar
  ──────────────────────
    The dot operator works identically to bracket syntax for string keys:
      d.name                     → "Alice"     (same as d["name"])
      d.age = 31                 → modifies    (same as d["age"] = 31)
      d.score = 95               → adds new key

   Inspection & Manipulation
  ──────────────────────
    add(d, "key", val)                  Inserts or updates a key-value pair
    remove(d, "key")                    Removes the key (throws error if absent)
    discard(d, "key")                   Removes the key (silent if absent)
    clear(d)                            Erases all entries

    len(d) / dictSize(d)                Number of key-value entries
    type(d)                             Returns "Dict"
    hasKey(d, "key")                    Returns 1 if the key exists
    keys(d)                             Returns all keys as a StringMatrix row
    values(d)                           Returns all values
    dictPairs(d)                        Returns an N×2 StringMatrix of [key, value] rows
    dictMerge(d1, d2)                   Merges d2 into d1

  Iteration & Membership
  ──────────────────────
    for (k in d) { ... }             Iterate over keys
    "key" in d                       Returns 1 if the key exists, 0 otherwise

    Destructured iteration (extracts key-value pairs simultaneously):
      for ([k, v] in d) {
          print(k, "=", v)
      }

  Reference Semantics & GC
  ──────────────────────
    Identical to Lists, Dicts are passed by reference.
      d1 = {a: 1}
      d2 = d1
      d2.a = 99                 // d1.a is now also 99
      
    Dicts are fully tracked by the VM's Garbage Collector. Therefore, creating a
    graph where Dict A points to Dict B, and Dict B points back to Dict A will 
    never cause a memory leak when they go out of scope.
)HELP"},

{ "set", R"HELP(
═══ Set (Unordered Deduplicated Collection) ═══

  Sets store unique elements only. Duplicate insertions are silently ignored.
  Elements can be of any type. Sets use REFERENCE SEMANTICS (like Lists/Dicts).

  Construction
  ──────────────────────
    s = Set()                       Empty set
    s = Set(1, 2, 3)               From values (duplicates auto-removed)
    s = Set(1, 1, 2, 2, 3)         → Set{1, 2, 3}
    s = toSet([3, 1, 4, 1, 5])     From array/list/string (deduplicates)
    s = toSet("hello")             → Set{"h", "e", "l", "o"}

  Membership & Iteration
  ──────────────────────
    3 in s                          1 if present, 0 otherwise (O(1) lookup)
    for (x in s) { print(x) }      Iterates in insertion order
    len(s)                          Number of unique elements

    Element Operations (Unified Container API - mutates via reference)
  ──────────────────────
    add(s, val)                     Add element (no-op if already present)
    remove(s, val)                  Remove element (error if absent)
    discard(s, val)                 Remove element (silent if absent)
    clear(s)                        Remove all elements
    setPop(s)                       Remove & return an arbitrary element
    
    (Note: legacy functions setAdd, setRemove, setDiscard, setClear 
     are still supported).

  Set Algebra (returns NEW Set)
  ──────────────────────
    setUnion(a, b)                  a ∪ b  — all elements in either set
    setIntersect(a, b)              a ∩ b  — elements in both sets
    setDiff(a, b)                   a \ b  — elements in a but not in b
    setSymDiff(a, b)                a △ b  — elements in exactly one set
    
    setProduct(a, b)                a × b  — Cartesian product (returns Set of Lists)
    setPow(s)                       P(s)   — Powerset (Set of all subsets, max 20 elems)

    Operator Shortcuts (and Compound Assignments)
  ──────────────────────
    a | b                           Union (same as setUnion)
    a & b                           Intersection (same as setIntersect)
    a - b                           Difference (same as setDiff)
    a * b                           Cartesian product (same as setProduct)
    a |= b                          In-place union (a = a | b)
    a &= b                          In-place intersection (a = a & b)

  Relation Predicates (return 1 or 0)
  ──────────────────────
    isSubset(a, b)                  Is every element of a also in b?
    isSuperset(a, b)                Is every element of b also in a?
    isDisjoint(a, b)                Do a and b share no elements?
    a == b                          Set equality (same elements, any order)

  Type Checks
  ──────────────────────
    isset(x)                        1 if x is a Set
    type(s)                         → "Set"
    isempty(s)                      1 if set has no elements

  Conversion
  ──────────────────────
    toList(s)                       Set → List (preserves insertion order)
    toSet(list)                     List/Array/String → Set (deduplicates)

  Reference Semantics
  ──────────────────────
    Sets share the same underlying memory when assigned:
      s1 = Set(1, 2, 3)
      s2 = s1                      s2 and s1 are the SAME set
      add(s2, 99)                  s1 now also contains 99
    Sets are tracked by the Garbage Collector (see: help sys).

  Examples
  ──────────────────────
    a = Set(1, 2, 3)
    b = Set(2, 3, 4)

    a | b                           → Set{1, 2, 3, 4}
    a & b                           → Set{2, 3}
    a - b                           → Set{1}
    a * Set("x", "y")               → Set{[1, "x"], [1, "y"], ... [3, "y"]}
    setPow(Set(1, 2))               → Set{Set{}, Set{1}, Set{2}, Set{1, 2}}

    chars = toSet("abracadabra")    → Set{"a", "b", "c", "d", "r"}
    3 in a                          → 1
    9 in a                          → 0
    isSubset(Set(1, 2), a)          → 1
)HELP" },

        { "class", R"HELP(
═══ Classes, Instances, Inheritance & Operator Overloading ═══

  Defining a Class
  ──────────────────────
    class ClassName {
        init(params) = { body }         Constructor (optional)
        methodName(params) = expr       Method definition
    }

    • 'init' executes automatically upon instance creation.
    • Inside methods, `self` refers to the active instance.
    • Methods support default parameters: `method(x, y = 0) = ...`

  Field Access (Dot Operator) & Instantiation
  ──────────────────────
    p = Point(3, 4)              Calls init(3, 4)
    p.x                          Read field
    p.x = 10                     Write field (creates if absent)
    Instances use strict REFERENCE SEMANTICS (tracked by the Garbage Collector):
      p2 = p1                    p2 and p1 share the exact same object
      p2.x = 99                  p1.x is automatically 99
      
    You can also force destructure instances just like Dicts:
      {x, y} = p                 Extracts p.x into 'x' and p.y into 'y'


  Inheritance (extends / super)
  ──────────────────────
    class ChildClass extends ParentClass {
        init(...) = { super.init(...); ... }
        method() = ...         // Override parent method
    }

    • Single inheritance only. Child inherits all parent methods.
    • `super.method()` dispatches to the parent class's implementation.

  Operator Overloading (Dunder Methods)
  ──────────────────────
    Classes can inject logic into operators via double-underscore methods.
    Dunder methods are inherited through the class hierarchy.

    Arithmetic:
      __add__ (+)  __sub__ (-)  __mul__ (*)  __div__ (/)  __mod__ (%)  __pow__ (^)

    Reverse arithmetic (when left operand is NOT an instance):
      __radd__  __rsub__  __rmul__  __rdiv__  __rmod__  __rpow__
      Example: 2 * vec  calls  vec.__rmul__(2)

    Unary:
      __neg__()        Unary minus: -obj

    Comparison:
      __eq__ (==)  __neq__ (!=)  __lt__ (<)  __le__ (<=)  __gt__ (>)  __ge__ (>=)

    Membership:
      __contains__(x)   Called by:  x in obj

    Indexing:
      __getitem__(i)               Called by:  obj[i]
      __setitem__(i, v)            Called by:  obj[i] = v

    Conversion hooks (called by built-in functions):
      __str__()    → str(obj), print(obj), f"{obj}", format("{}", obj)
      __len__()    → len(obj)
      __abs__()    → abs(obj)
      __bool__()   → bool(obj)


  Chained Method Calls
  ──────────────────────
    Methods that return `self` enable fluent-style chaining:
      class Builder {
          init() = { self.parts = list() }
          add(x) = { self.parts = push(self.parts, x); return self }
          build() = join(self.parts, "-")
      }
      Builder().add("a").add("b").add("c").build()   → "a-b-c"

  Destructuring with Methods
  ──────────────────────
    Methods can return arrays/lists for destructured assignment:
      class Point { coords() = [self.x, self.y] }
      [px, py] = Point(3, 4).coords()

  Introspection
  ──────────────────────
    type(p)                  Class name → "Point"
    isinstance(p)            1 if p is any class instance
    isinstance(p, Point)     1 if p is (or inherits from) Point
    hasField(p, "x")         1 if field exists
    getFields(p)             All field names as StringMatrix
    getClass(p)              The class definition object
    getParent(Dog)           Parent class or none

  Type Predicates (see: help typecheck)
  ──────────────────────
    isclass(Point)           1 if argument is a class definition
    isinstance(p)            1 if argument is any instance
    isfunction(sin)          1 if argument is a function/closure
)HELP" },

        { "error", R"HELP(
═══ Error Handling & Tracebacks ═══

  In JC2, errors generated by invalid math operations, type mismatches, 
  or explicit `throw` statements will terminate the current script sequence.

  Throwing Errors
  ──────────────────────
    throw "message"             Throws an exact string error message
    throw expr                  Throws any evaluated value
    error("message")            Function form (acts exactly like `throw`)

  Stack Traces (Tracebacks)
  ──────────────────────
    When an unhandled error or exception occurs, JC2's VM unwinds the 
    call stack and prints a highly detailed, ANSI-colorized Traceback. 
    This pinpoints the exact file, script line, and function where the 
    error originated, traversing seamlessly through class dunder methods 
    (__setitem__, etc.) and anonymous closures.

  Try / Catch Blocks
  ──────────────────────
    try {
        risky_code()
    } catch (e) {
        print("Caught an error:", e)
    }

    The `catch` variable (e.g., `e`) receives the error message as a string.
    `try`/`catch` is an expression. It natively returns the value of the 
    `try` block if successful, or the value of the `catch` block if an error occurred.

  Re-throwing & Control Flow
  ──────────────────────
    You can trigger `throw e` inside a catch block to bubble unhandled errors up.
    Control flow signals (`break`, `continue`, and `return`) pass through 
    `try`/`catch` boundaries completely unaffected.

  Protected Call (pcall)
  ──────────────────────
    `pcall(f)` provides a functional alternative to `try`/`catch`.
    It executes a zero-argument function `f` and captures the result safely into a List.

    Returns `[1, result]` on success.
    Returns `[0, errorMsg]` on failure.

      status = pcall(riskyFunc)
      isError(status)       → 1 if error, 0 if success

  Assertions
  ──────────────────────
    assert(cond)                  Throws if cond is falsy
    assert(cond, "msg")           Throws custom message if falsy
    assert("name", got, expected) Throws detailed mismatch error if got != expected
)HELP" },

        {"fileio", R"HELP(
═══ File I/O ═══

  Text Files
  ──────────────────────
    readFile(path)              Reads the entire file into a single string.
    writeFile(path, content)    Writes a string to the file (overwrites existing).
    appendFile(path, content)   Appends a string to the end of the file.
    readLines(path)             Reads the file line-by-line into a List of strings.
    writeLines(path, list)      Writes a List of elements as separate lines.

  CSV Files (Data Analysis)
  ──────────────────────
    readCSV(path, delim)        Reads a CSV into a List of Lists.
    readCSVMat(path, delim)     Reads a CSV directly into a StringMatrix.
    parseCSVNum(path, delim)    Reads a CSV into a mathematical RealMatrix.
    writeCSV(path, data, delim) Writes a Matrix or List out to a CSV file.

  Path Resolution (Script-Centric)
  ──────────────────────
    JC2 resolves all relative file paths based on the location of the 
    CURRENTLY EXECUTING script, not the terminal's working directory.

    If `C:/project/src/main.jc2` calls `readFile("data.txt")`, it will
    look for `C:/project/src/data.txt`. This allows you to build standalone,
    portable JC2 packages and libraries that bundle their own data files.

    If you are typing interactively in the REPL, relative paths resolve
    from the directory where you launched the executable.

  File System Operations
  ──────────────────────
    fileExists(path)            Returns 1 if the file/folder exists, 0 otherwise.
    fileSize(path)              Returns the physical file size in bytes.
    deleteFile(path)            Permanently deletes the specified file.
    listDir(path)               Returns a List of filenames in the specified directory.
)HELP"},

        { "typecheck", R"HELP(
═══ Type Hinting & Predicates ═══

  Gradual Typing (Type Contracts)
  ──────────────────────
    JC2 optionally enforces parameter and return types at runtime during 
    function execution. Unannotated parameters accept any type.

    Syntax:
      func_name(param1: type, param2: type) -> return_type = { body }

    Examples:
      process(data: matrix, scale: double) -> matrix = data * scale
      connect(host: string, port: int = 8080) = { ... }
    
    If an incompatible type is passed, the VM throws a TypeError:
      process([1,2], "fast")
      ✗ TypeError: Parameter 'scale' expected type 'double', got 'string'.

    Supported Base Types: 
      int, double (or float/real/number), string, list, dict, set, 
      matrix, complex, fraction, func, bool, any
    
    Class / OOP Types:
      Any capitalized type name is evaluated as a class instance constraint.
      Inheritance is strictly respected (passing a subclass is valid).
      Example: collide(a: RigidBody, b: RigidBody) -> bool = ...

  Numeric Type Checks (Functions)
  ──────────────────────
    isint(x)            Integer? (BigInt, integer-valued double, 
                        or Fraction with denominator = 1)
    isfloat(x)          double type specifically?
    isnumeric(x)        Any numeric type? (double, BigInt, Fraction, 
                        Complex, BaseNum)
    iscomplex(x)        Complex type specifically?
    isreal(x)           Real number? (double, BigInt, Fraction, BaseNum,
                        or Complex with imaginary part ≈ 0)
    isfrac(x)           Fraction type specifically?
    isbigint(x)         BigInt type specifically?
    isbase(x)           BaseNum type specifically?

    Examples:
      isint(42)           → 1     (BigInt literal)
      isint(3.0)          → 1     (integer-valued double)
      isint(3.5)          → 0
      isint(frac(6,3))    → 1     (reduces to 2)
      isreal(3+0i)        → 1     (imaginary part is zero)
      isreal(3+4i)        → 0
      isnumeric("hello")  → 0

  Container Type Checks
  ──────────────────────
    ismatrix(x)         Any matrix? (Real, Complex, or String)
    isrealmat(x)        RealMatrix specifically?
    iscomplexmat(x)     ComplexMatrix specifically?
    isstringmat(x)      StringMatrix specifically?
    isvector(x)         Row vector (1×N) or column vector (N×1)?
    issquare(x)         Square matrix (N×N)?
    islist(x)           List type?
    isdict(x)           Dict type?

    Examples:
      ismatrix([1,2;3,4])       → 1
      isvector([1,2,3])         → 1   (1×3 row vector)
      isvector([1;2;3])         → 1   (3×1 column vector)
      isvector([1,2;3,4])       → 0   (2×2, not a vector)
      issquare([1,2;3,4])       → 1
      islist(list(1,2))         → 1
      isdict({a: 1})            → 1

  String Predicates
  ──────────────────────
    isstring(x)         Is x a String type?
    isalpha(s)          All characters alphabetic? ("abc" → 1)
    isdigit(s)          All characters digits? ("123" → 1)
    isalnum(s)          All characters alphanumeric? ("abc123" → 1)
    isspace(s)          All characters whitespace? ("  \\t" → 1)
    isupper(s)          All alphabetic chars uppercase? ("ABC" → 1)
    islower(s)          All alphabetic chars lowercase? ("abc" → 1)
    isempty(x)          Empty string, empty List, empty Dict,
                        or zero-dimension matrix?

    Notes:
    • Non-string arguments return 0 for all string predicates
      (except isempty, which also works on Lists, Dicts, matrices).
    • Empty strings return 0 for isalpha/isdigit/isalnum/isspace
      (nothing to test), but 1 for isempty.

  Special Type Checks
  ──────────────────────
    isnone(x)           Is x the none value?
    isfunction(x)       Is x a function or closure?
    isclass(x)          Is x a class definition?
    isinstance(x)       Is x an instance of any class? (1 arg)
    isinstance(x, C)    Is x an instance of class C or its subclass? (2 args)

  Floating-Point Checks
  ──────────────────────
    isnan(x)            Is x NaN? (only for double type)
    isinf(x)            Is x ±Infinity? (only for double type)
    isfinite(x)         Is x a finite number? (BigInt/Fraction → always 1)

  Numeric Value Predicates
  ──────────────────────
    iszero(x)           Is x equal to zero? (works on double, BigInt,
                        Complex, Fraction — uses tolerance for double)
    ispositive(x)       Is x > 0? (double, BigInt, Fraction)
    isnegative(x)       Is x < 0? (double, BigInt, Fraction)
    iseven(n)           Is n an even integer?
    isodd(n)            Is n an odd integer?
    isprime(n)          Is n a prime number? (Miller-Rabin test)

  Type Constructors (see also: help basic)
  ──────────────────────
    int(x)              Truncate to BigInt (toward zero)
    double(x)           Convert to double
    complex(x)          Convert to Complex (imag=0)
    complex(a, b)       Construct Complex from real and imaginary parts
    matrix(r, c, ...)   Construct matrix (auto-detects Real/Complex/String)
    pi()  e()  i()      Constant factory functions (always available)
)HELP" },

        { "json", R"HELP(
═══ JSON Module — Native Module ═══

  Requires: import "json"

  Provides high-performance, native C++ state-machine based JSON serialization
  and deserialization. It guarantees memory safety and DOM-level formatting
  without external dependencies.

  Functions & JS Aliases
  ──────────────────────
    Both traditional and Node.js-style aliases are injected directly into
    the global namespace upon import.

    json_encode(val)  /  stringify(val)    Convert JC2 value → JSON string
    json_decode(str)  /  parse(str)        Parse JSON string → JC2 value
    json_pretty(val)                       Pretty-print with 4-space indent
    json_pretty(val, indent)               Custom indent width (e.g., 2)

  Type Mapping (JC2 → JSON)
  ──────────────────────
    double / BigInt / Fraction   →  number (integers are cleanly truncated)
    String                       →  string (Deep RFC standard escaping)
    List                         →  array
    Dict                         →  object
    RealMatrix (1D / 2D)         →  array / array of arrays
    none()                       →  null
    Complex / Special types      →  "<unserializable_type>" (Fallback)

  Type Mapping (JSON → JC2)
  ──────────────────────
    number (integer)             →  BigInt (avoids precision loss on IDs)
    number (float)               →  double
    string                       →  String
    array                        →  List
    object                       →  Dict
    true / false                 →  1.0 / 0.0 (JC2 native boolean representation)
    null                         →  none()

  Examples
  ──────────────────────
    import "json"

    // 1. Serialization (Object to String)
    d = { name: "Alice", active: true, scores: [85, 92] }
    s = stringify(d)             
    // → '{"name": "Alice", "active": 1, "scores": [85, 92]}'

    // 2. Pretty Print (Perfect for writing config files)
    print(json_pretty(d, 2))     
    // → {
    //     "name": "Alice",
    //     "active": 1,
    //     "scores": [
    //       85,
    //       92
    //     ]
    //   }

    // 3. Deserialization (String to Object)
    raw = r"({"host": "127.0.0.1", "port": 8080})"
    conf = parse(raw)
    conf.port                    // → 8080 (Parsed as BigInt)
    conf.host                    // → "127.0.0.1"

    // 4. File I/O Full Pipeline
    writeFile("config.json", json_pretty(conf))
    loaded = parse(readFile("config.json"))
)HELP" },

        { "import", R"HELP(
═══ Import (Code Reuse & Native Modules) ═══

  The `import` command loads code into the current environment.
  It supports both JC2 script files (.jc2) and native C++ modules.

  Syntax
  ──────────────────────
    import "path"               Loads and executes a .jc2 file
    import "image"              Loads the native Image module
    import "prob"               Loads the native Probability module
    import "json"               Loads the native JSON module

  Native Modules (built into the executable)
  ──────────────────────
    Native modules are written in C++ and compiled into JunkCalculator2.
    They load instantly with zero file I/O overhead.

    modules()                   List all available native modules
    import "moduleName"         Load a native module

    Currently available:
      image     BMP image generation & plotting
      prob      11 probability distributions + hypothesis tests
      json      JSON encode / decode / pretty-print

    Native modules take priority over .jc2 files with the same name.
    Importing the same module twice is a safe no-op (deduplicated).

  Script Modules (.jc2 files)
  ──────────────────────
    import "math_utils"         Loads math_utils.jc2

  Smart Path Resolution (Script-Centric)
  ──────────────────────
    `import` resolves relative paths based on the directory of the
    currently running script, NOT the terminal's working directory.

    Search order for `import "utils"` inside C:/project/src/main.jc2:
    1. C:/project/src/utils
    2. C:/project/src/utils.jc2
    3. C:/project/src/data/utils.jc2
    4. C:/project/src/lib/utils.jc2
    5. <workspace>/utils.jc2
    6. <exe_dir>/lib/utils.jc2  (global fallback)

    Once a library starts executing, its own directory becomes the
    path context for any further imports or file operations within it.

  Features & Safeguards
  ──────────────────────
    • Deduplication: Importing the same file/module twice is a no-op.
    • Chained imports: A library can safely `import` other libraries.
    • Error propagation: Errors inside an imported file abort the import.
    • All definitions become globally available in the caller.
)HELP" },

        { "image", R"HELP(
═══ Image Processing — Native Module ═══

  Requires: import "image"

  The `image` module provides a high-performance 2D rasterization API backed 
  by C++. It allows rendering geometric shapes, statistical plots, and manipulating 
  pixels directly in memory, which can be encoded to BMP formats.

  Initialization
  ──────────────────────
    im = img(width, height [, background_color])
        Allocates a new image surface in RAM. Returns an Image object.
        Colors are passed as hex strings (e.g., "#282C34") or standard names.

  SDF Anti-Aliasing (HD Graphics)
  ──────────────────────
    The JC2 drawing engine natively utilizes Signed Distance Fields (SDF) 
    to provide GPU-grade, sub-pixel anti-aliasing entirely in software.
    
    To activate buttery-smooth edges, simply pass floating-point values 
    for thickness or coordinates. The engine will automatically perform 
    Alpha-blending on the sub-pixel boundaries!

      im.line(10, 10, 90, 90, "red", 1.0)      // Crisp, aliased 1px line
      im.line(10, 10, 90, 90, "red", 1.5)      // Smooth, anti-aliased 1.5px line!
      im.circle(400, 300, 150, "blue", 5.8)    // HD Circle with feathered edges

  Drawing Primitives (Chainable)
  ──────────────────────
    Most methods return `self` to allow fluent chaining.

    im.width()  /  im.height()
    im.clear(color)
    im.setPixel(x, y, color)
    im.getPixel(x, y)                Returns the color as a hex string "#RRGGBB"
    
    im.line(x0, y0, x1, y1, color [, thick=1.0])
    im.rect(x, y, w, h, color [, thick=1.0])
    im.fillRect(x, y, w, h, color)
    im.circle(cx, cy, radius, color [, thick=1.0])
    im.fillCircle(cx, cy, radius, color)

  Text Rendering (Native Hardware Font)
  ──────────────────────
    im.text(text, x, y, color [, scale=1])
        Renders strings or numbers using a zero-dependency, hardcoded IBM VGA 
        8x8 ASCII hardware font. Perfect for HUDs, labels, and retro games.

  Data Visualization (Plotting)
  ──────────────────────
    im.axes(xMin, xMax, yMin, yMax [, color])
        Draws cartesian coordinate axes mapped to the specified range.
    im.scatter(x_matrix, y_matrix, xMin, xMax, yMin, yMax [, color])
        Projects data points from two matrices onto the image canvas.

  I/O & Network Streaming
  ──────────────────────
    im.save("filepath.bmp")
        Encodes the memory surface and flushes it to a valid Windows BMP file.
    
    data = imgReadBytes("filepath.bmp")
        (Global) Reads a binary file's EXACT byte sequence into a String buffer.
)HELP" },

        { "bytes", R"HELP(
═══ Bare-Metal Memory Engine — Native Module ═══

  Requires: import "bytes"

  The `bytes` module provides low-level, zero-dependency C++ memory buffers. 
  It grants absolute control over binary reading, writing, and file I/O.
  
  ★ Note: For everyday use, it is highly recommended to use the standard 
    library wrapper `import "buffer"`, which provides an OOP interface.

  Buffer Allocation & I/O
  ──────────────────────
    b_alloc(size)               Allocate a zeroed buffer of `size` bytes.
    b_pack(array)               Create a buffer from an array of 8-bit integers.
    readFileBytes(path)         Map an entire file from disk into a buffer.
    writeFileBytes(path, buf)   Flush a byte buffer directly to your disk.
    b_len(buf)                  Get the total size of the buffer in bytes.

  Low-Level Reading & Writing (Absolute Offsets)
  ──────────────────────
    b_set(buf, offset, val, type)   Write a value into memory at `offset`.
    b_get(buf, offset, type)        Read a value from memory at `offset`.
    b_get(buf, offset, "str", len)  Read specifically a string of `len` bytes.

    Supported formats (passed as strings): 
      "u8", "i8", "u16", "i16", "u32", "i32", "f32", "f64", "str"

  High-Performance Bulk Operations
  ──────────────────────
    b_write_arr(buf, off, arr, type)  
        Directly memcpy a JC2 Array/Matrix into memory at C++ speeds. 
        (Currently supports "i16" and "f64"). 
        Provides massive performance boosts for audio/data generation.

  Example
  ──────────────────────
    import "bytes"
    b = b_alloc(4)
    b_set(b, 0, 255, "u8")
    b_set(b, 1, 65535, "u16") 
    b_get(b, 1, "u16")             → 65535
)HELP" },

        { "socket", R"HELP(
═══ Native Socket Binding — Native Module ═══

  Requires: import "socket"

  The `socket` module provides unfiltered access to the operating system's 
  network stack (WinSock2 on Windows, POSIX Sockets on Linux/macOS).

  ★ Note: For everyday networking, it is HIGHLY recommended to use the standard 
    library wrapper `import "net"`, which abstracts these pointers into managed 
    TCP objects (`TcpSocket` and `TcpServer`).

  Outbound Connections (Client)
  ──────────────────────
    net_tcp_connect(host, port)
        Resolves domain records (DNS) and dials a TCP stream to the remote peer.
        Returns an opaque pointer wrapped inside a `NativeSocket` Class instance.

  Inbound Listeners (Server)
  ──────────────────────
    net_tcp_server(host, port)
        Binds to a specified local interface (e.g., "127.0.0.1" or "0.0.0.0") 
        and puts the OS networking stack into LISTEN mode with SO_REUSEADDR enabled.
        Returns a server socket wrapper.

    net_tcp_accept(server_socket)
        Blocks the active VM thread pending incoming network traffic. 
        When a peer connects, returns a brand-new client socket wrapper.

  Raw Data Exchange / Teardown
  ──────────────────────
    net_send(socket, text)
        Performs a deep C++ 'send()' call, flushing strings over the TCP boundary.
    net_recv(socket, max_bytes)
        Blocks and reads incoming packets. Returns "" on disconnect.
    net_close(socket)
        Silently severs an established network pipe.
)HELP" },

        { "window", R"HELP(
═══ Native Window Engine — Native Module ═══

  Requires: import "window"
  (Note: Currently restricted to Win32 architectures. Requires User32 / Gdi32 / imm32)

  The `window` module pierces the OS layer to spawn a hardware-accelerated 
  GUI window, complete with an asynchronous, thread-safe Event Queue for 
  zero-latency keyboard and mouse polling.

  Spawning a Window & Basic State
  ──────────────────────
    win = Window(title, width, height)
        Requests the OS to create a fixed-size trackable window.
    win.isOpen()
        Returns 1 if the window is alive, 0 if closed by the user.
    win.show(image_obj)
        Bit-block transfers (Blits) a JC2 `Image` object's memory buffer 
        directly onto the window's device context (HDC) instantly.

  Mouse & Cursor Control (3D/FPS Mechanics)
  ──────────────────────
    win.showCursor(boolean)
        Dynamically hides (0.0) or shows (1.0) the Windows mouse pointer.
        Essential for creating immersive games and custom UI crosshairs.
        
    win.setCursorPos(x, y)
        Forcibly teleports the operating system's mouse pointer to the 
        specified coordinates within the window. Used to create "infinite" 
        mouse-look mechanics in 3D games by continually resetting the 
        mouse to the center of the screen.

  IME (Input Method Editor) Control
  ──────────────────────
    win.setImeEnabled(boolean)
        Dynamically enables or disables the OS Input Method (e.g., Pinyin).
        • Pass `false` (0) for action games to prevent the IME from intercepting WASD.
        • Pass `true`  (1) when expecting text input from the user.

  Real-Time Input Polling
  ──────────────────────
    win.isKeyDown(key)
        Provides instantaneous, zero-latency physical key state.
        Accepts human-readable strings (case-insensitive):
          "W", "A", "S", "D", "0"-"9"
          "UP", "DOWN", "LEFT", "RIGHT"
          "SPACE", "ENTER", "ESC", "SHIFT", "CTRL", "ALT", "TAB"
        Returns 1.0 if currently pressed, 0.0 otherwise.

  Event Queue (win.pollEvent)
  ──────────────────────
    ev = win.pollEvent()
        Non-blocking pop from the OS message queue. Returns `none` if empty.
        If an event exists, returns a Dict with a "type" string:

        • "keydown" / "keyup"
           ev.key       → String ("W", "SPACE", "LEFT", etc.)
           ev.keycode   → Number (Underlying Win32 Virtual-Key code)
        
        • "mousedown" / "mouseup"
           ev.x, ev.y   → Number (Mouse cursor coordinates)
           ev.button    → Number (0 = Left Click, 1 = Right Click)
        
        • "mousemove"
           ev.x, ev.y   → Number (Current coordinates)
        
        • "close"
           The user clicked the 'X' button on the window.
)HELP" },

        { "latex", R"HELP(
═══ LaTeX Mathematical Engine — Native Module ═══

  Requires: import "latex"

  The `latex` module provides bi-directional integration with standard LaTeX 
  math syntax. It can serialize JC2 matrices, fractions, and complex numbers 
  into beautiful LaTeX code, and conversely, it can parse, compile, and 
  evaluate raw LaTeX formulas into blazing-fast native JC2 closures.

  Serialization (JC2 → LaTeX)
  ──────────────────────
    to_latex(obj)
        Converts a JC2 value into a clean LaTeX code string.
        • Fraction:  frac(1,2)      → "\frac{1}{2}"
        • Complex:   3+4i           → "3+4i"
        • Matrix:    [1, 2; 3, 4]   → "\begin{pmatrix} 1 & 2 \\ ... \end{pmatrix}"
        • List:      list(1, 2)     → "\left[ 1, 2 \right]"

  Direct Evaluation (LaTeX → Number or Complex)
  ──────────────────────
    eval_latex("formula")
        Parses and evaluates a constant LaTeX math string directly.
        The underlying engine fully supports the Complex Plane.
        (Tip: Use r-strings `r"..."` so you don't have to double-escape backslashes!)
        
        eval_latex(r"\frac{1+\sqrt{5}}{2}")       → 1.618033
        eval_latex(r"\frac{1+i}{2}")              → 0.5 + 0.5i
        eval_latex(r"e^{i \pi} + 1")              → 0 + 0i  (Euler's Identity!)

  JIT Compilation to JC2 Closures (LaTeX → JC2 Function)
  ──────────────────────
    compile_latex("formula", variable_names)
        Dynamically compiles a parameterized LaTeX formula into an executable 
        JC2 abstract syntax tree, returning a true JC2 function closure.
        `variable_names` must be a List or StringMatrix.

        // 1. Compile the LaTeX math into a native callable function f(x, \theta)
        f = compile_latex(r"\frac{\sin(\theta)}{x^2}", ["x", r"\theta"])
        
        // 2. Call it interactively! (Supports real and complex arguments)
        f(2.0, PI/2)                 → 0.25
        
        // 3. Works seamlessly with all higher-order and calculus functions!
        table(f, [1, 2, 3], fill(PI/2, 3)|>trans)    → Tabulates results over a vector
        diff(f(_, PI/2), 2.0)        → Derivative w.r.t 'x' at x=2.0

  Supported LaTeX Syntax (Parser Engine)
  ──────────────────────
    • Operations:   +, -, *, /, ^
    • Grouping:     { }, ( ), [ ]
    • Fractions:    \frac{numerator}{denominator}
    • Functions:    \sin, \cos, \tan, \sqrt, \ln, \log, \exp, \abs
    • Constants:    \pi, e, i, j   (i and j are intrinsically parsed as 0+1i)
    
    ★ Advanced Feature: Implicit Multiplication
      The recursive descent parser fully understands implicit mathematical 
      multiplication just like a human reading a paper. 
      Expressions like "2i", "xy", and "2 \sin(x)" act identically to 
      "2*i", "x*y", and "2 * \sin(x)".
)HELP" },
    };

} // namespace jc

#endif // JC2_HELP_TEXT_H
