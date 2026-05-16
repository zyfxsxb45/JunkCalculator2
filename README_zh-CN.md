<div align="right">
  <a href="README.md">English</a> | <strong>简体中文</strong>
</div>

# Junk Calculator 2.4.0.0

![Version](https://img.shields.io/badge/Version-v2.4.0.0-orange.svg?style=flat-square)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg?style=flat-square&logo=c%2B%2B)
![Zero Dependencies](https://img.shields.io/badge/Dependencies-0-brightgreen.svg?style=flat-square)
![CMake](https://img.shields.io/badge/CMake-3.15+-064F8C.svg?style=flat-square&logo=cmake)
![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=flat-square)

一个基于 C++20 实现的脚本语言及计算机代数系统 (CAS)。该方案采用自定义字节码编译器和基于栈的虚拟机执行，**完全无第三方依赖**。

由清华大学 Yu Liangyang 开发。

---

## 技术概览

### 底层架构
- **词法分析器 (Lexer)**：支持超过 55 种词法单元，涵盖字符串插值 (`f""`)、自定义定界符原始字符串 (`r"TAG()TAG"`)、交替单双引号、虚数后缀 (`3i`) 以及可变长参数 (`...`)。
- **语法分析器 (Parser)**：递归下降解析器，生成包含 30 余种节点类型的抽象语法树 (AST)。支持运算符优先级、块语句、逗号序列求值以及解构提取。
- **编译器 (Compiler)**：采用访问者模式将 AST 编译为字节码。负责处理词法作用域、自动局部变量声明、循环补丁 (Loop Patching) 以及闭包环境的捕获。
- **虚拟机 (Virtual Machine)**：基于栈的字节码解释器。实现了函数调用的延迟绑定 (Late-binding)、带行号回溯的异常处理、交互式步进调试器、执行性能分析器 (Profiler) 以及动态运算符分发。

### 语言语义
- **类型系统与内存管理**：由 `std::variant` 驱动的动态类型系统，内部包含 17 种数据类型。
  - *值类型*：标量（双精度浮点数、大整数、复数）与矩阵（实数矩阵、复数矩阵、字符串矩阵）采用连续内存，遵循“值传递 (pass-by-value)”语义。
  - *引用类型*：容器 (`List`, `Dict`, `Set`) 与面向对象 `Instance` 采用“引用传递”语义（底层基于 PIMPL 架构和 `std::shared_ptr`）。
- **渐进式类型 (Gradual Typing)**：支持对函数参数和返回值进行运行时类型契约校验（例如 `func(a: double, b: matrix) -> bool = ...`）。涵盖基础类型、容器类型及类的继承校验。
- **垃圾回收 (GC)**：运行于 VM 栈上的标记-清扫 (Mark-and-Sweep) 垃圾回收器 (`GcHeap`)。追踪 GC 根节点（全局变量、调用栈、闭包上值及上下文）以打破并清除循环引用。
- **面向对象 (OOP)**：支持单继承 (`extends`)、`super` 超类分发以及通过魔术方法（如 `__add__`）实现的运算符重载。实例对象支持解构赋值。
- **控制流**：包含 `if/else`、`while`、`for`、`for-in`、`switch/case`、`break/continue/return`。
- **错误处理**：提供 `try/catch/throw` 结构以及支持栈追踪的函数式 `pcall`。
- **执行控制**：具备强大的 `Ctrl+C` 中断机制，可在不崩溃虚拟机的前提下安全暂停死循环或重型 CAS 计算。连续三次 `Ctrl+C` 将触发强制退出。
- **函数特性**：支持闭包、Lambda 表达式 `(x) => expr`、默认参数、可变长参数 (`...args`) 以及 `ref` 引用参数绑定。
- **泛型容器 API**：提供统一的数组操作接口（如 `push`、`slice`、`map`、`filter`、`reduce`、`sort`、`join`、`zip` 等），可无缝运行于四种底层数据结构：`RealMatrix`、`ComplexMatrix`、`StringMatrix` 与 `List`。
- **集合代数 (Set Algebra)**：提供具有 O(1) 成员判定性能 (`in`) 的 `Set` 类型。支持并集 (`|`)、交集 (`&`)、差集 (`-`) 和笛卡尔积 (`*`) 运算符。内置幂集生成 (`setPow`) 及包含关系断言机制。

### 数学与计算机代数系统 (CAS)
- **计算机代数系统 (CAS)**：基于有向无环图 (DAG) 的符号数学引擎。具备代数化简（`simplify`、`expand`、`contract`、`factor`、`trigsimp`）、符号微积分（`diff`、`integ`、`limit`、`taylor`）以及精确解析求根（`solveEq`）功能。
- **多项式代数**：利用子结式伪余数序列求解多项式 GCD，并采用有限域 $\mathbb{Z}_p$ 映射（Cantor-Zassenhaus 算法）进行多元多项式因式分解。
- **积分引擎**：实现了 Risch 算法的核心子集，包含 Hermite 归约、Rothstein-Trager 算法以及刘维尔微分域扩张。
- **任意精度运算**：采用 Base-10^9 压缩布局的 `BigInt` 引擎。实现了高基数除法、GCD/LCM 及模幂运算。
- **精确有理数与符号提升**：`Fraction` 类型支持递归交叉约分。当遇到无法数值计算的精确有理数幂（如 `(1/2)^(1/2)`）时，会自动“提升”为 `SymExpr` CAS 符号树，彻底杜绝浮点精度丢失。
- **线性代数**：`Matrix<T>` 模板库，支持 Gauss-Jordan 消元、QR 分解（修正的格拉姆-施密特正交化）、LU 分解（Doolittle 部分主元消去法）以及特征值求解（赫森伯格矩阵 + Givens QR 迭代）。

### 原生模块与标准库
注入到执行运行时的原生 C++ 扩展 (Native Modules)：
- `image`：基于 OOP 的高并发 BMP 图像生成器。绘图组件内建 SDF（符号距离场）支持，实现亚像素级抗锯齿，并包含 ASCII 字体渲染器。
- `prob`：概率论与统计模块。提供面向对象的分布类（支持 PDF、CDF 及基于牛顿迭代的分位数逆函数），以及建设性的假设检验功能。
- `json`：高性能 JSON 序列化与反序列化引擎。
- `socket`：操作系统底层的 TCP/IP 网络栈控制引擎（封装了 WinSock2 / POSIX）。
- `bytes`：内存缓冲区控制及裸二进制文件 I/O 引擎。
- `window`：原生 GUI 窗口渲染引擎。支持第一人称鼠标指针捕获 (Mouse-Look) 和独立的输入法 (IME) 状态接管 (Win32)。
- `latex`：双向 LaTeX 引擎。可将 JC2 数学对象序列化为 LaTeX 代码，或将原始 LaTeX 公式解析并即时编译为可执行的 JC2 闭包函数。

通过 `import` 加载的 JC2 标准库：
- `collections`：常用数据结构，包含栈、队列、双端队列、优先队列（堆）以及多种搜索树。
- `regex`：面向对象的 NFA 正则表达式引擎，支持捕获组、选择分支与量词。
- `discrete`：离散数学工具箱，涵盖组合数学、二元关系和图遍历。
- `engine`：面向对象的可视化/游戏框架，在 `window` 模块基础上抽象了渲染主循环与事件状态机。
- `net`：TCP 数据流的高级 OOP 封装（`TcpSocket` 和 `TcpServer`）。
- `http`：现代 HTTP/1.1 客户端，支持 URL 解析及 GET/POST 请求。
- `buffer`：提供游标寻址能力的高级二进制操作 API。

---

## v2.4.0.0 版本更新说明

v2.4.0.0 版本在底层架构上进行了深度重构，引入了 NaN-Boxing 内存模型与裸指针求值栈，带来了显著的性能提升。语言层面新增了统一函数调用语法 (UFCS)、原生命名空间、块级作用域控制以及矩阵的元素级广播运算，进一步增强了语言的表达能力。

### 核心架构与性能优化
- **NaN-Boxing 内存模型**：彻底移除了 `std::variant`，采用 NaN-Boxing 技术重构了底层的 `Value` 动态类型系统，并引入了真正的布尔类型 (`ObjType::BOOL`)，大幅降低了内存占用并提升了分发速度。
- **裸指针求值栈**：将虚拟机中基于 `std::vector` 的求值栈替换为裸指针数组，消除了运行时的动态扩容开销。
- **编译期优化**：编译器新增了常量折叠 (Constant Folding) 与死代码消除 (Dead Code Elimination) 机制。
- **惰性分配**：对内置函数闭包实现了惰性分配与缓存机制，大幅减轻了虚拟机启动时的 GC 压力。

### 语言特性与语法拓展
- **复杂解构赋值**：解构赋值现已支持复杂的左值表达式，允许直接对数组索引和对象属性进行解构赋值（例如 `[a[1], b.prop] = [1, 2]`）。
- **统一函数调用语法 (UFCS)**：全面支持 UFCS（如 `data.map(f)` 等价于 `map(data, f)`），配合管道运算符极大增强了链式调用的流畅度。
- **原生命名空间 (Namespace)**：引入了 `namespace` 关键字用于模块化封装。命名空间块在底层由专属对象驱动，支持基于 Upvalue 的字段捕获与 `const` 常量导出。
- **块级作用域控制**：新增 `local` 关键字，允许显式声明严格绑定于当前代码块（如 `if`、`for`、`{}`）的局部变量，避免污染函数级作用域。
- **多行字符串字面量**：全面支持使用三引号 (`"""` 或 `'''`) 定义多行字符串，并完美兼容 `f` (插值) 与 `r` (原始) 前缀。
- **Lambda 表达式增强**：匿名函数现已完整支持 `ref` 引用捕获与 `...` 可变长参数。
- **可调用实例**：新增 `__call__` 魔术方法，允许将类实例像普通函数一样直接调用。

### 内存语义与对象模型
- **容器的值语义相等性**：重构了容器（List、Dict、Set）的相等性判定与哈希逻辑，现已全面支持基于内容的深度值比较。
- **O(1) 冻结容器哈希缓存**：为冻结状态的容器引入了惰性哈希缓存机制，将其作为 Dict 键或 Set 元素时的哈希计算复杂度从 O(N) 降至 O(1)。
- **深度克隆与状态保留**：新增 `copy()` 内置函数，支持对复杂对象进行深度克隆，并完美保留其内部的冻结 (frozen) 状态。

### 数学引擎与矩阵运算
- **矩阵元素级广播运算**：新增了一整套泛型元素级操作函数（`addE`, `subE`, `mulE`, `divE`, `eqE`, `ltE`, `whereE` 等），全面替代了旧版的隐式标量广播逻辑，支持标量-标量、标量-矩阵以及矩阵-矩阵的无缝广播计算。
- **精确 IEEE 754 比较**：移除了底层模糊的浮点比较逻辑，全面回归严格的 IEEE 754 标准，并新增 `isapprox` 函数用于处理带容差的浮点数近似比较。
- **强制双精度方根**：新增 `sqrtD`、`cbrtD`、`rootD` 函数，用于强制绕过 CAS 符号提升，直接获取双精度或复数数值解。
- **位运算重构**：移除了位运算中隐式的 `BaseNum` 转换，并将按位取反 `~x` 严格定义为 `-x-1`。

### 类型系统与系统工具
- **鸭子类型契约 (Duck Typing)**：增强了运行时的渐进式类型断言，新增 `iterable`、`callable`、`indexable`、`hashable` 等行为契约，以及 `str`、`whole`、`exact` 等类型注解。
- **动态字节码编译**：新增 `compileFile()` 与 `compileCode()` 函数，允许在运行时将外部脚本或字符串动态编译为字节码闭包并执行。
- **系统级优化**：实现了安全的 Ctrl+C 线程中断机制；为 Windows 平台嵌入了应用程序图标与版本元数据。
- **VS Code 插件升级**：配套的语言服务插件现已支持悬停提示 (Hover Provider)、文档符号大纲 (Document Symbols) 以及代码片段 (Snippets)。

### 破坏性更新 (Breaking Changes)
- **真正的布尔类型**：引入了原生的 `bool` 类型。所有的谓词函数现在返回 `bool`（`true`/`false`），而不再是双精度浮点数 (`1.0`/`0.0`)。
- **元素级函数重命名**：原有的 `*S` 系列标量广播函数（如 `addS`, `mulS` 等）已全面升级并重命名为 `*E` 系列（如 `addE`, `mulE` 等）。
- **`none` 关键字**：移除了旧版的 `none()` 内置函数，现在 `none` 已成为语言的原生关键字。
- **高阶函数参数顺序调整**：为完美适配统一函数调用语法 (UFCS)，`map`、`filter` 等高阶函数的参数顺序已调整，现在函数参数需放置在容器参数之后（例如 `map(data, func)`）。

---

## 构建指南

本项目要求使用支持 C++20 的主流编译器，以及 CMake 3.15 或以上版本进行构建。

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

*注意：在使用 MSVC 构建时，底层 CMake 脚本将默认启用 `/MT` 运行时静态链接与全局链接时代码生成 (LTCG / `/GL`)，以压榨极限性能。*

---

## 命令行接口使用方法

    JunkCalculator2                    # 运行交互式 REPL 会话
    JunkCalculator2 script.jc2         # 执行目标脚本
    JunkCalculator2 --run script.jc2   # 执行目标脚本（显式传递）
    JunkCalculator2 script.jc2 -d      # 执行脚本，并在执行时打印虚拟机字节码反汇编流
    JunkCalculator2 script.jc2 --debug # 开启交互式步进调试器模式运行
    JunkCalculator2 script.jc2 --profile # 运行结束后输出 VM 执行火焰图指令性能报告

*脚本路径上下文：`run` 与 `import` 指令执行时，将动态地使执行文件所在目录压入路径栈。因此，在脚本内部请求相对物理路径资源时，解析规则始终基于脚本自身所在目录，而脱离终端命令执行点的影响。*

---

## 项目代码结构

    +-- src/
    |   +-- main.cpp                引擎入口，CLI 解析及工作区环境控制
    |   +-- frontend/               前端编译组件 (Lexer, Parser, Compiler, AST, Highlight)
    |   +-- vm/                     虚拟机核心 (VM, Bytecode, BuiltinRegistry, 中断控制)
    |   +-- memory/                 内存与类型系统 (Value 动态类型, GcHeap 垃圾回收)
    |   +-- math/                   基础数学库 (BigInt, Fraction, Complex, Matrix, Base)
    |   +-- cas/                    计算机代数系统 (Symbolic, Integration, Factorization, Groebner)
    |   +-- modules/                原生 C++ 扩展模块 (Image, Probability, JSON, Socket 等)
    +-- lib/                        标准 JC2 业务层逻辑库开发区
    +-- examples/                   内置展示用项目示例
    +-- jc2-language/               配套 Visual Studio Code 插件支持

---

## 许可证 (License)

MIT License
