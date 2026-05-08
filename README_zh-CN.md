<div align="right">
  <a href="README.md">English</a> | <strong>简体中文</strong>
</div>

# Junk Calculator 2.3.3.0

![Version](https://img.shields.io/badge/Version-v2.3.3.0-orange.svg?style=flat-square)
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

## v2.3.3.0 版本更新说明

v2.3.3.0 重点重构了编译器的作用域系统与内存语义，引入了全新的闭包状态修饰符，并扩展了命令行接口（CLI）与相关工具链，进一步提升了语言内核的安全性与运行稳定性。

### ⚠️ 破坏性更新与迁移指南 (Breaking Changes)

本次更新统一了底层变量模型的处理逻辑，旧版本代码可能需要进行以下迁移：

1. **废弃 `global` 关键字**
   - **变更原因**：引入了更结构化的向上寻址机制。
   - **迁移方法**：将旧代码中的所有 `global` 关键字替换为新版引入的 `ref` 修饰符。在函数内部需引用或修改外部变量时，统一使用 `ref x = ...`。
2. **函数声明语法脱糖（Desugaring）**
   - **影响说明**：取消显式函数的特权作用域。所有 `func() = {}` 在编译时严格等价于 `func = () => {}` 的匿名闭包赋值。函数名现在完全等同于普通变量，并遵循相同的自动局部遮蔽（Auto-local shadowing）规则。
3. **修饰符绑定范围缩至标识符级别**
   - **影响说明**：在解构声明中，若需修改上层变量或设置持久状态，必须为每个特定的变量单独标记修饰符。例如：`[ref a, state b, c] = [1, 2, 3]`。
4. **内置函数命名规范化**
   - **影响说明**：为了与标准命名规范保持一致并防止名称冲突，集合构造函数与矩阵元素访问函数已被重命名。
   - **迁移方法**：
     - 将代码中的 `Set(...)` 替换为 `set(...)`。
     - 将矩阵读取 `get(A, r, c)` 替换为 `getElement(A, r, c)`。
     - 将矩阵写入 `set(A, r, c, val)` 替换为 `setElement(A, r, c, val)`。

---

### 编译器与核心作用域重构
本次更新正式确立了由普通局部赋值 (Auto-local)、持久状态 (`state`) 与引用修改 (`ref`) 构成的三态闭包作用域框架：
- **新增 `ref` 修饰符（上层作用域穿透）**：打破默认的局部屏蔽（Auto-local）规则。允许在闭包内直接寻址并修改外层（或全局）作用域中的同名变量。`ref` 具有严格的存在性校验，仅允许修改已存在的变量。若试图穿透修改未声明的变量，虚拟机会立即拦截并抛出运行时错误，彻底杜绝拼写错误（Typo）意外污染全局环境。
- **新增 `state` 修饰符（私有持久状态）**：赋予闭包变量局部的持久化记忆能力。被 `state` 标记的变量严格在其闭包生命周期内仅初始化一次。后续对该函数的调用将保留并允许修改这一状态，完美实现状态机机制且不污染外部空间。若无显式赋值（如 `state x`），则默认安全初始化为 `none`。
- **完善解构赋值语义**：优化 LHS（赋值左侧）寻址优先级，使 `ref` 和 `state` 能够正常融入多维数组与字典的解构语法。
- **修复递归闭包**：函数脱糖为匿名闭包后，底层自动通过引用捕获自身名称，支持无缝、安全的闭包自递归调用。
- **表达式拓展**：恢复对逗号表达式（Comma Expression）的完整支持，并调整相关解析优先级以兼容新的作用域修饰符。

### 内存安全与 VM 引擎优化
- **专属全局修饰指令**：为支持 `ref` 严格校验，底层 VM 新增 `OP_SET_GLOBAL_REF` 操作码，在 O(1) 的单次哈希寻址复杂度内完成变量存在性校验与读写，实现安全性的同时保持零性能损耗。
- **防循环引用（Cyclic Reference）**：为深拷贝 `clone()` 和深度冻结 `val()` 增加对象引用路径检测，防止在操作自引用容器（如自包含的字典或列表）时触发栈溢出或内存爆满。
- **精简指令集**：移除底层虚拟机冗余的函数调用操作码（Opcodes），优化指令执行管线。

### 命令行接口 (CLI) 与工具链
- **新增 `jc` 别名**：通过 CMake 构建时将自动生成 `jc`（Linux/macOS）及 `jc.bat`（Windows）包装程序，作为 `JunkCalculator2` 原指令的极简别名。
- **扩展 CLI 启动参数**：
  - 新增 `-e` / `--eval <code>` 参数，支持通过命令行传入单行代码直接求值并退出。
  - 新增 `-q` / `--quiet` 静默启动模式，隐藏 REPL 横幅与交互提示符，便于将 JC2 与标准系统管道（Pipeline）无缝集成执行数据流处理。
- **重构 Help 帮助系统**：
  - 帮助文档数据全面迁移为独立 JSON 文件进行后端驱动。
  - 支持在系统终端直接检索特定子主题（例如 `jc --help scope`）。
  - REPL 内置帮助查询新增基于编辑距离（Levenshtein distance）的模糊查词匹配。
- **VS Code 插件升级**：插件现已对接系统的 JSON 接口文档解析，为原生函数和关键字提供了完整的智能代码补全（Auto-completion）。

### 数学引擎 (CAS) 与文档
- **CAS 评估优化**：在符号计算引擎中引入 `poly-exp` 闭式计算快捷处理规则，防止在极限计算、多项式微分或积分时因递归过深导致的崩溃。
- **完成文档全覆盖**：更新了系统内置的文档数据结构，全面补充了系统常量、控制函数以及类魔术方法（如 `__str__`、`__add__`）的规范说明与示例代码。

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
