#include <iostream>
#include <string>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include "Lexer.h"
#include "Parser.h"
#include "Value.h"
#include "HelpText.h"
#include "Highlight.h"
#include "Compiler.h"
#include "VM.h"
#include "Module.h"
#include "BuiltinRegistry.h"
#include "modules/json_module.h"
#include "modules/image_module.h"
#include "modules/prob_module.h"

// 延续字符串判定
static bool endsWithContinuation(const std::string& line) {
    size_t e = line.find_last_not_of(" \t\r\n");
    if (e == std::string::npos) return false;
    if (e >= 1 && line[e - 1] == '|' && line[e] == '>') return true;
    if (e >= 1 && line[e - 1] == '&' && line[e] == '&') return true;
    if (e >= 1 && line[e - 1] == '|' && line[e] == '|') return true;
    char c = line[e];
    return (c == '+' || c == '-' || c == '*' || c == '/' || c == '\\' ||
        c == '%' || c == '^' || c == ',' || c == '=');
}

std::string getExecutableDir() {
    namespace fs = std::filesystem;
#ifdef _WIN32
    char buf[2048];
    if (GetModuleFileNameA(nullptr, buf, sizeof(buf))) {
        return fs::path(buf).parent_path().string();
    }
#endif
    return fs::current_path().string();
}

void printHelp() {
    auto it = jc::BuiltinHelp.find("main");
    if (it != jc::BuiltinHelp.end()) std::cout << "\n" << it->second << std::endl;
}

// 在 main.cpp 中找到 printHelpTopic 并修改为：
void printHelpTopic(const std::string& topic) {
    std::string key = topic;
    size_t s = key.find_first_not_of(" \t");
    size_t e = key.find_last_not_of(" \t");
    if (s != std::string::npos) key = key.substr(s, e - s + 1);
    else key.clear();

    // ★ 1. 优先搜索脚本动态注册的库
    auto itDyn = jc::DynamicHelp.find(key);
    if (itDyn != jc::DynamicHelp.end()) {
        std::cout << "\n" << itDyn->second << std::endl;
        return;
    }

    // ★ 2. 搜索 C++ 静态库（且转为小写）
    std::transform(key.begin(), key.end(), key.begin(),
        [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });

    auto it = jc::BuiltinHelp.find(key);
    if (it != jc::BuiltinHelp.end()) std::cout << "\n" << it->second << std::endl;
    else std::cout << "\n  No detailed help available for topic: '" << topic << "'\n";
}

// 核心 VM 实例和全局上下文
jc::VM vm;
std::vector<std::shared_ptr<jc::CompiledFunction>> allFunctions;
bool g_showDisasm = false;  // ★ 新增：字节码反汇编开关
bool g_autoDebug = false;
bool g_profile = false;

// ★ 执行一段任意多行/单行代码的统一接口
jc::Value evalCode(const std::string& code, bool isFile = false) {
    jc::Lexer lexer(code);
    auto tokens = lexer.tokenize();
    jc::Parser parser(tokens);
    auto ast = parser.parse();

    jc::Compiler compiler;
    compiler.setFunctionIndexOffset(static_cast<int>(allFunctions.size()));
    jc::Chunk chunk = compiler.compile(ast.get());

    if (g_showDisasm) {
        chunk.disassemble(isFile ? "Script Chunk" : "REPL Chunk");
    }

    auto& newFns = compiler.getCompiledFunctions();
    int rootLocalCount = 0;
    if (!newFns.empty()) {
        rootLocalCount = newFns[0]->localCount;
    }
    // ★ 不再强制 localCount = 8

    for (auto& fn : newFns) allFunctions.push_back(fn);
    vm.setCompiledFunctions(allFunctions);

    auto evalFn = std::make_shared<jc::CompiledFunction>();
    evalFn->name = isFile ? "<script>" : "<eval>";
    evalFn->arity = 0;
    evalFn->maxArity = 0;
    evalFn->localCount = rootLocalCount;
    evalFn->chunk = chunk;

    int evalIdx = static_cast<int>(allFunctions.size());
    if (g_autoDebug) {
        if (jc::VM::activeVM) {
            jc::VM::activeVM->triggerDebugger(); // ★ 一进虚拟机立刻触发下一行暂停！
        }
    }
    allFunctions.push_back(evalFn);
    vm.setCompiledFunctions(allFunctions);

    return vm.callVMFunction(evalIdx, {});
}

void runScript(const std::string& filepath) {
    std::string resolvedPath = jc::helpers::safeResolvePath(filepath);
    if (!std::filesystem::exists(resolvedPath))
        resolvedPath = jc::helpers::safeResolvePath(filepath + ".jc2");
    if (!std::filesystem::exists(resolvedPath)) {
        std::cerr << "   IO Error: Cannot open script '" << filepath << "'." << std::endl;
        return;
    }
    std::ifstream file(resolvedPath);
    if (!file.is_open()) {
        std::cerr << "   IO Error: Cannot open script '" << filepath << "'." << std::endl;
        return;
    }

    // ★ 一次性读取整个文件
    std::string code, line;
    while (std::getline(file, line)) code += line + "\n";
    file.close();

    jc::helpers::g_scriptDirStack.push_back(
        std::filesystem::path(resolvedPath).parent_path().string());

    try {
        jc::Value result = evalCode(code, true);
        if (!result.isNone()) vm.setGlobal("ANS", result);
    }
    catch (const std::exception& ex) {
        std::cerr << jc::col(jc::Ansi::BRIGHT_RED)
            << "   Error in '" << resolvedPath << "': "    // ★ 修改：附带文件名
            << ex.what() << jc::col(jc::Ansi::RESET) << std::endl;
    }
    if (g_profile && jc::VM::activeVM) {
        jc::VM::activeVM->printProfileReport();
    }

    jc::helpers::g_scriptDirStack.pop_back();
}

void saveWorkspace(const std::string& filename) {
    namespace fs = std::filesystem;

    // 正确调用 C++ 原生的 getWorkspace
    jc::BuiltinRegistry reg; reg.registerAll();
    std::string wp = std::get<std::string>(reg.getBuiltins()["getWorkspace"]({}).data);

    fs::path dir(wp);
    if (!fs::exists(dir)) fs::create_directories(dir);
    std::ofstream out((dir / (filename + ".jc2")).string());

    int count = 0;
    for (const auto& [name, value] : vm.getGlobals()) {
        if (name == "PI" || name == "E" || name == "i" || name == "I" || name == "ANS" || name == "true" || name == "false") continue;
        out << name << " = " << value.toJC2Expression() << "\n";
        count++;
    }
    out.close();
    std::cout << "   Saved " << count << " variables to " << (dir / (filename + ".jc2")).string() << std::endl;
}

void loadWorkspace(const std::string& filename) {
    namespace fs = std::filesystem;

    jc::BuiltinRegistry reg; reg.registerAll();
    std::string wp = std::get<std::string>(reg.getBuiltins()["getWorkspace"]({}).data);

    std::string path = (fs::path(wp) / (filename + ".jc2")).string();
    if (!fs::exists(path)) { std::cerr << "   IO Error: Workspace not found.\n"; return; }

    vm.clearGlobals();
    runScript(path);
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    std::system("chcp 65001 > nul");
#endif
    jc::enableAnsiColors();
    std::string exeDir = getExecutableDir();
    // ===== 初始化超级虚拟机 =====
    jc::BuiltinRegistry registry;
    registry.registerAll();
    for (const auto& [name, fn] : registry.getBuiltins()) {
        const auto& arities = registry.getArity().find(name)->second;
        vm.registerBuiltin(name, fn, arities);
        // ★ 我们把内置方法只留给原生表处理！彻底释放 Globals 字典空间供用户自由重载调用！
    }

    // 初始化系统常量 (普通赋予即可！它无法通过系统的 delete 指令销毁，但你能将 PI 暂时盖为别的值)
    vm.setGlobal("PI", jc::Value(3.14159265358979323846));
    vm.setGlobal("E", jc::Value(2.71828182845904523536));
    vm.setGlobal("i", jc::Value(jc::Complex(0.0, 1.0)));
    vm.setGlobal("I", jc::Value(jc::Complex(0.0, 1.0)));
    vm.setGlobal("true", jc::Value(1.0));
    vm.setGlobal("false", jc::Value(0.0));
    vm.setGlobal("none", jc::Value::none());

    // 绑定虚拟机外包服务给系统级运行时回调！
    jc::helpers::setGlobalCallback = [](const std::string& name, const jc::Value& val) { vm.setGlobal(name, val); };
    jc::helpers::getGlobalCallback = [](const std::string& name) -> jc::Value {
        auto it = vm.getGlobals().find(name);
        return (it != vm.getGlobals().end()) ? it->second : jc::Value::none();
        };
    jc::helpers::evalCallback = [](const std::string& code) -> jc::Value { return evalCode(code, false); };
    jc::helpers::runFileCallback = [](const std::string& path) { runScript(path); }; // ★ 提供标准库文件挂载服务
    jc::helpers::callFunctionCallback = nullptr;
    jc::helpers::resolvePathCallback = [exeDir](const std::string& path) -> std::string {
        namespace fs = std::filesystem;
        fs::path p(path);
        if (p.is_absolute()) return p.string();
        std::vector<std::string> c = {
            fs::weakly_canonical(fs::current_path() / p).string(),
            (fs::current_path() / "data" / p).string(),
            (fs::path(exeDir) / p).string(),
            (fs::path(exeDir) / "data" / p).string(),
            (fs::path(exeDir) / "lib" / p).string()
        };
        for (const auto& cp : c) if (fs::exists(cp)) return cp;
        return fs::weakly_canonical(fs::current_path() / p).string();
        };

    // ★ 清洁版命令行参数解析
    std::string scriptPath = "";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-d") {
            g_showDisasm = true;
        }
        else if (arg == "--debug") {    // ★ 拦截 --debug 启动项
            g_autoDebug = true;
        }
        else if (arg == "--run") {
            continue; // 跳过 --run 标记
        }
        else if (arg == "--help" || arg == "-h") {
            printHelp();
            return 0;
        }
        else if (arg == "--profile") {
            g_profile = true;
            vm.enableProfiler(true);
        }
        else {
            if (scriptPath.empty()) {
                scriptPath = arg;
            }
            else {
                std::cerr << "Unknown argument or multiple scripts provided: " << arg << std::endl;
                return 1;
            }
        }
    }

    // 有脚本路径则执行脚本并退出
    if (!scriptPath.empty()) {
        runScript(scriptPath);
        return 0;
    }

    std::cout << jc::col(jc::Ansi::BRIGHT_CYAN)
        << "=================================================\n"
        << "   Junk Calculator 2.2.1\n"
        << "   Developed by Yu Liangyang, Tsinghua University\n"
        << "=================================================\n" << jc::col(jc::Ansi::RESET)
        << "Type " << jc::col(jc::Ansi::BRIGHT_YELLOW) << "'help'" << jc::col(jc::Ansi::RESET) << " for a list of commands." << std::endl;

    while (true) {
        std::string input;
        std::cout << "\n" << jc::col(jc::Ansi::BOLD) << jc::col(jc::Ansi::BRIGHT_CYAN) << "JC2> " << jc::col(jc::Ansi::RESET);
        std::getline(std::cin, input);

        size_t start = input.find_first_not_of(" \t");
        size_t end = input.find_last_not_of(" \t");
        if (start == std::string::npos) continue;
        input = input.substr(start, end - start + 1);

        int braces = 0, parens = 0, brackets = 0;
        for (char c : input) {
            if (c == '{') braces++; else if (c == '}') braces--;
            else if (c == '(') parens++; else if (c == ')') parens--;
            else if (c == '[') brackets++; else if (c == ']') brackets--;
        }
        while (braces > 0 || parens > 0 || brackets > 0 || endsWithContinuation(input)) {
            std::string line;
            std::cout << jc::col(jc::Ansi::BRIGHT_CYAN) << "...  " << jc::col(jc::Ansi::RESET);
            if (!std::getline(std::cin, line)) break;
            input += "\n" + line;
            for (char c : line) {
                if (c == '{') braces++; else if (c == '}') braces--;
                else if (c == '(') parens++; else if (c == ')') parens--;
                else if (c == '[') brackets++; else if (c == ']') brackets--;
            }
        }

        if (input == "color on") { jc::colorsEnabled = true; continue; }
        if (input == "color off") { jc::colorsEnabled = false; continue; }

        // ★ 随时开关 Disassembly 打印
        if (input == "-d on") {
            g_showDisasm = true;
            std::cout << "Bytecode disassembly enabled.\n";
            continue;
        }
        if (input == "-d off") {
            g_showDisasm = false;
            std::cout << "Bytecode disassembly disabled.\n";
            continue;
        }

        // ★ 随时开关全局单步 Debugger
        if (input == "--debug on") {
            g_autoDebug = true;
            std::cout << "Interactive Step-Debugger enabled. (Will break on next evaluated line)\n";
            continue;
        }
        if (input == "--debug off") {
            g_autoDebug = false;
            if (jc::VM::activeVM) jc::VM::activeVM->disableDebugger(); // ★ 强制拉闸
            std::cout << "Interactive Step-Debugger disabled.\n";
            continue;
        }
        if (input == "--profile on") {
            g_profile = true;
            if (jc::VM::activeVM) jc::VM::activeVM->enableProfiler(true);
            std::cout << "Profiler enabled. Will print report after execution.\n";
            continue;
        }
        if (input == "--profile off") {
            g_profile = false;
            if (jc::VM::activeVM) jc::VM::activeVM->enableProfiler(false);
            std::cout << "Profiler disabled.\n";
            continue;
        }
        if (input == "exit" || input == "quit") break;
        if (input == "help") { printHelp(); continue; }
        if (input.substr(0, 5) == "help ") { printHelpTopic(input.substr(5)); continue; }
        if (input == "clear") { vm.clearGlobals(); std::cout << "All variables cleared.\n"; continue; }
        if (input.substr(0, 5) == "save ") { saveWorkspace(input.substr(5)); continue; }
        if (input.substr(0, 5) == "load ") { loadWorkspace(input.substr(5)); continue; }

        try {
            jc::Value result = evalCode(input, false);
            if (!result.isNone()) {
                vm.setGlobal("ANS", result);
                std::string typeColor;
                std::visit([&typeColor](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, double> || std::is_same_v<T, jc::BigInt> || std::is_same_v<T, jc::Fraction>)
                        typeColor = jc::col(jc::Ansi::BRIGHT_YELLOW);
                    else if constexpr (std::is_same_v<T, jc::Complex>)
                        typeColor = jc::col(jc::Ansi::BRIGHT_MAGENTA);
                    else if constexpr (std::is_same_v<T, std::string>)
                        typeColor = jc::col(jc::Ansi::BRIGHT_GREEN);
                    else if constexpr (std::is_same_v<T, jc::RealMatrix> || std::is_same_v<T, jc::ComplexMatrix>)
                        typeColor = jc::col(jc::Ansi::WHITE);
                    else if constexpr (std::is_same_v<T, jc::StringMatrix>)
                        typeColor = jc::col(jc::Ansi::GREEN);
                    else if constexpr (std::is_same_v<T, jc::BaseNum>)
                        typeColor = jc::col(jc::Ansi::BRIGHT_CYAN);
                    else if constexpr (std::is_same_v<T, std::shared_ptr<jc::FunctionClosure>>)
                        typeColor = jc::col(jc::Ansi::BRIGHT_BLUE);
                    else if constexpr (std::is_same_v<T, jc::Dict> || std::is_same_v<T, jc::List> || std::is_same_v<T, jc::Set>)
                        typeColor = jc::col(jc::Ansi::CYAN);
                    else if constexpr (std::is_same_v<T, std::shared_ptr<jc::ClassDefinition>>)
                        typeColor = jc::col(jc::Ansi::BRIGHT_BLUE);
                    else if constexpr (std::is_same_v<T, std::shared_ptr<jc::Instance>>)
                        typeColor = jc::col(jc::Ansi::BRIGHT_CYAN);
                    }, result.data);
                std::cout << typeColor << result << jc::col(jc::Ansi::RESET) << std::endl;
            }
            if (g_profile && jc::VM::activeVM) {
                jc::VM::activeVM->printProfileReport();
            }
        }
        catch (const std::exception& e) {
            std::cerr << jc::col(jc::Ansi::BRIGHT_RED) << "   " << e.what() << jc::col(jc::Ansi::RESET) << std::endl;
        }
    }

    std::cout << "\nGoodbye!" << std::endl;
    return 0;
}
