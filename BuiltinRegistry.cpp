#include "BuiltinRegistry.h"
#include "SymEval.h"
#include "Integration.h"
#include "Factorization.h"
#include "Highlight.h"          // ★ highlightCode(), colorsEnabled
#include "Module.h"
#include "VM.h"
#include "GcHeap.h"
#include "HelpText.h"           // ★ BuiltinHelp, DynamicHelp
#ifdef _MSC_VER
#pragma warning(disable: 4702)
#endif
#include <algorithm>
#include <cctype>
#include <chrono>               // ★ clock() — high_resolution_clock
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <numeric>              // ★ std::gcd (如果用到)
#include <random>
#include <sstream>
#include <thread>               // ★ sleep() — std::this_thread::sleep_for
#include <filesystem>           // ★ Phase 2: file I/O
#include <fstream>              // ★ Phase 2: file I/O

namespace jc {
    // 替代 Evaluator 中的路径状态
    static std::string g_workspacePath = "";
    // 获取当前路径
    static std::string g_cwd() {
        if (!helpers::g_scriptDirStack.empty()) return helpers::g_scriptDirStack.back();
        return std::filesystem::current_path().string();
    }

    // =================================================================
    // 容器元素键生成器（保证内容相同的 Set/Dict 无论插入顺序如何，都能生成相同的键用于去重）
    // =================================================================
    std::string setValueKey(const Value& v) {
        static thread_local std::vector<const void*> visited;
        std::ostringstream oss;
        oss << v.data.index() << ":";

        if (std::holds_alternative<std::monostate>(v.data)) {
            oss << "none";
        }
        else if (std::holds_alternative<List>(v.data)) {
            auto& l = std::get<List>(v.data);
            PrintGuard guard(visited, l.id());
            if (guard.isCycle) { oss << "CYCLE"; return oss.str(); }
            oss << "[";
            for (const auto& e : l.raw()) {
                oss << setValueKey(e) << ",";
            }
            oss << "]";
        }
        else if (std::holds_alternative<Dict>(v.data)) {
            auto& d = std::get<Dict>(v.data);
            PrintGuard guard(visited, d.id());
            if (guard.isCycle) { oss << "CYCLE"; return oss.str(); }
            std::vector<std::string> pairs;
            for (const auto& [k, val] : d.getEntries()) {
                pairs.push_back(setValueKey(k) + ":" + setValueKey(val));
            }
            std::sort(pairs.begin(), pairs.end());
            oss << "{";
            for (const auto& p : pairs) oss << p << ",";
            oss << "}";
        }
        else if (std::holds_alternative<Set>(v.data)) {
            auto& s = std::get<Set>(v.data);
            PrintGuard guard(visited, s.id());
            if (guard.isCycle) { oss << "CYCLE"; return oss.str(); }
            std::vector<std::string> elems;
            for (const auto& [k, val] : s.raw()) {
                elems.push_back(setValueKey(val));
            }
            std::sort(elems.begin(), elems.end());
            oss << "Set{";
            for (const auto& e : elems) oss << e << ",";
            oss << "}";
        }
        else if (std::holds_alternative<std::shared_ptr<Instance>>(v.data)) {
            auto inst = std::get<std::shared_ptr<Instance>>(v.data);
            auto [found, res] = helpers::tryCallDunder(inst, "__hash__");
            if (found) {
                oss << res.toString();
            } else {
                oss << inst.get();
            }
        }
        else {
            oss << v;
        }
        return oss.str();
    }

    // =================================================================
// 符号函数坍缩器：递归求值所有参数已为纯数字的 SymFunc 节点
// sin(0) → 0,  cos(PI) → -1,  etc.
// =================================================================
    static Value casValToValue(const CASVal& v) {
        return std::visit([](auto&& arg) -> Value { return Value(arg); }, v);
    }

    static bool isConstantExpr(const SymExpr& expr) {
        if (!expr.ptr) return true;
        switch (expr.ptr->getType()) {
            case SymType::NUM: return true;
            case SymType::VAR: {
                auto name = std::static_pointer_cast<SymVar>(expr.ptr)->name;
                return name == "PI" || name == "E" || name == "i" || name == "I";
            }
            case SymType::ADD:
                for (auto& arg : std::static_pointer_cast<SymAdd>(expr.ptr)->args)
                    if (!isConstantExpr(SymExpr(arg))) return false;
                return true;
            case SymType::MUL:
                for (auto& arg : std::static_pointer_cast<SymMul>(expr.ptr)->args)
                    if (!isConstantExpr(SymExpr(arg))) return false;
                return true;
            case SymType::POW: {
                auto p = std::static_pointer_cast<SymPow>(expr.ptr);
                return isConstantExpr(SymExpr(p->base)) && isConstantExpr(SymExpr(p->exp));
            }
            case SymType::FUNC:
                for (auto& arg : std::static_pointer_cast<SymFunc>(expr.ptr)->args)
                    if (!isConstantExpr(SymExpr(arg))) return false;
                return true;
        }
        return false;
    }

    template<typename MapType>
    static SymExpr collapseSymFuncs(const SymExpr& expr, const MapType& fns) {
        jc::checkInterrupt();
        if (!expr.ptr) return expr;

        switch (expr.ptr->getType()) {
        case SymType::NUM:
        case SymType::VAR:
            return expr;

        case SymType::ADD: {
            auto add = std::static_pointer_cast<SymAdd>(expr.ptr);
            SymExpr result(BigInt(0));
            for (auto& arg : add->args)
                result = result + collapseSymFuncs(SymExpr(arg), fns);
            return result;
        }

        case SymType::MUL: {
            auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
            SymExpr result(BigInt(1));
            for (auto& arg : mul->args)
                result = result * collapseSymFuncs(SymExpr(arg), fns);
            return result;
        }

        case SymType::POW: {
            auto pow = std::static_pointer_cast<SymPow>(expr.ptr);
            return collapseSymFuncs(SymExpr(pow->base), fns) ^
                collapseSymFuncs(SymExpr(pow->exp), fns);
        }

        case SymType::FUNC: {
            auto func = std::static_pointer_cast<SymFunc>(expr.ptr);
            std::vector<SymExpr> newArgs;
            std::vector<Value> vals;
            bool allNumeric = true;
            for (auto& arg : func->args) {
                SymExpr collapsed = collapseSymFuncs(SymExpr(arg), fns);
                newArgs.push_back(collapsed);
                
                if (isConstantExpr(collapsed)) {
                    try {
                        std::map<std::string, Value> emptyEnv;
                        SymbolicFuncResolver resolver = [&fns](const std::string& name, const std::vector<Value>& fnArgs) -> Value {
                            auto it = fns.find(name);
                            if (it != fns.end()) return it->second(fnArgs);
                            throw std::runtime_error("Function not found");
                        };
                        vals.push_back(evalUniversal(collapsed.ptr, emptyEnv, resolver));
                    } catch (...) {
                        allNumeric = false;
                    }
                } else {
                    allNumeric = false;
                }
            }

            // 所有参数都是纯数字 → 尝试调用实际函数求值
            if (allNumeric) {
                auto it = fns.find(func->name);
                if (it != fns.end()) {
                    try {
                        Value result = it->second(vals);
                        return result.asSymbolic();
                    }
                    catch (const std::runtime_error& e) {
                        std::string msg = e.what();
                        // 数学错误：传播给用户
                        if (msg.find("Math Error") != std::string::npos)
                            throw;
                        if (msg.find("CAS Error") != std::string::npos)
                            throw;
                        // 类型不兼容：保留符号形式
                    }
                }
            }

            // 无法求值，保留为符号函数节点
            std::vector<std::shared_ptr<SymNode>> ptrs;
            for (auto& a : newArgs) ptrs.push_back(a.ptr);
            return SymExpr(std::make_shared<SymFunc>(func->name, std::move(ptrs)));
        }
        }
        return expr;
    }

using namespace helpers;

void BuiltinRegistry::registerAll() {
    registerMath();
    registerComplex();
    registerFraction();
    registerPolySolver();
    registerMatrixOps();
    registerDecompositions();
    registerLinearSolvers();
    registerVectors();
    registerNumberTheory();
    registerBase();
    registerStatistics();
    registerRandom();
    registerSystemUtils();
    registerControlFlow();
    registerStringFunctions();
    registerArrayFunctions();
    registerStringMatrix();
    registerDictFunctions();
    registerListConversion();
    registerIntrospection();
    registerFormatType();
    registerHigherOrder();
    registerCalculus();        // ★ Phase 2
    registerCAS();             // ★ CAS
    registerFileIO();          // ★ Phase 2
    registerErrorHandling();   // ★ Phase 2
    registerSystemShell();
    registerTypeChecks();
    registerSetFunctions();
}

// =================================================================
// [1] 基础数学函数
// =================================================================
void BuiltinRegistry::registerMath() {

    auto regMath = [&](const std::string& name, std::set<int> arities, NativeCallable fn) {
        reg(name, std::move(arities), [name, fn](const std::vector<Value>& args) -> Value {
            // 扫描：是否有任何参数是符号表达式？
            bool hasSymbolic = false;
            for (const auto& a : args) {
                if (a.isSymbolic()) { hasSymbolic = true; break; }
            }
            // 如果有，将所有参数统一提升为 SymExpr，打包成 SymFunc 节点
            if (hasSymbolic) {
                std::vector<std::shared_ptr<SymNode>> symArgs;
                symArgs.reserve(args.size());
                for (const auto& a : args) {
                    symArgs.push_back(a.asSymbolic().ptr);
                }
                // 在构建 AST 时直接将根式转换为分数幂，统一底层数学表达
                if (name == "sqrt" && symArgs.size() == 1) {
                    return Value(SymExpr(symArgs[0]) ^ SymExpr(Fraction(1, 2)));
                }
                if (name == "cbrt" && symArgs.size() == 1) {
                    return Value(SymExpr(symArgs[0]) ^ SymExpr(Fraction(1, 3)));
                }
                if (name == "root" && symArgs.size() == 2) {
                    return Value(SymExpr(symArgs[0]) ^ (SymExpr(BigInt(1)) / SymExpr(symArgs[1])));
                }
                return Value(SymExpr(std::make_shared<SymFunc>(name, std::move(symArgs))));
            }
            // 否则正常执行数值计算
            return fn(args);
            });
        };

    // 我们在此插入您要求的常量工厂和泛类型构造：
    reg("pi", { 0 }, [](const std::vector<Value>&) -> Value { return Value(3.14159265358979323846); });
    reg("e", { 0 }, [](const std::vector<Value>&) -> Value { return Value(2.71828182845904523536); });
    reg("i", { 0 }, [](const std::vector<Value>&) -> Value { return Value(Complex(0.0, 1.0)); });
    reg("none", { 0 }, [](const std::vector<Value>&) -> Value { return Value::none(); });
    reg("complex", { 1, 2 }, [this](const std::vector<Value>& args) -> Value {
        auto evalIfSym = [this](Value v) {
            if (v.isSymbolic()) {
                auto it = builtins.find("evalf");
                if (it != builtins.end()) return it->second({v});
            }
            return v;
        };
        if (args.size() == 1) {
            Value val = evalIfSym(args[0]);
            // complex(x) → Complex(x, 0) 或保留已有复数
            if (std::holds_alternative<Complex>(val.data))
                return val;
            return Value(Complex(val.asDouble(), 0.0));
        }
        // complex(a, b) → Complex(a, b)
        return Value(Complex(evalIfSym(args[0]).asDouble(), evalIfSym(args[1]).asDouble()));
        });

    reg("double", { 1 }, [this](const std::vector<Value>& args) -> Value {
        Value val = args[0];
        if (val.isSymbolic()) {
            auto it = builtins.find("evalf");
            if (it != builtins.end()) val = it->second({val});
        }
        // 任意数值类型 → double（复数仅虚部为 0 时允许）
        if (std::holds_alternative<Complex>(val.data)) {
            const auto& c = std::get<Complex>(val.data);
            if (!Tol::isEq(c.imag, 0.0))
                throw std::runtime_error("Type Error: Cannot convert complex with nonzero imaginary part to double.");
            return Value(c.real);
        }
        return Value(val.asDouble());
        });

    reg("int", { 1 }, [this](const std::vector<Value>& args) -> Value {
        Value val = args[0];
        if (val.isSymbolic()) {
            auto it = builtins.find("evalf");
            if (it != builtins.end()) val = it->second({val});
        }
        // 截断取整（向零方向）
        if (std::holds_alternative<BigInt>(val.data))
            return val;
        if (std::holds_alternative<Fraction>(val.data)) {
            const auto& f = std::get<Fraction>(val.data);
            return Value(f.getNum() / f.getDen());  // BigInt 除法自动截断
        }
        if (std::holds_alternative<Complex>(val.data)) {
            const auto& c = std::get<Complex>(val.data);
            if (!Tol::isEq(c.imag, 0.0))
                throw std::runtime_error("Type Error: Cannot convert complex with nonzero imaginary part to int.");
            return Value(BigInt(static_cast<int64_t>(std::trunc(c.real))));
        }
        if (std::holds_alternative<double>(val.data)) {
            double v = std::get<double>(val.data);
            if (!std::isfinite(v))
                throw std::runtime_error("Type Error: Cannot convert non-finite value to int.");
            return Value(BigInt(static_cast<int64_t>(std::trunc(v))));
        }
        if (std::holds_alternative<std::string>(val.data)) {
            // 字符串解析为整数
            const auto& s = std::get<std::string>(val.data);
            try { return Value(BigInt(s)); }
            catch (...) {
                throw std::runtime_error("Type Error: Cannot parse '" + s + "' as integer.");
            }
        }
        return Value(val.asBigInt());
        });

    reg("matrix", {}, [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2)
            throw std::runtime_error("Runtime Error: matrix(rows, cols [, ...]) expects at least 2 args.");
        int r = static_cast<int>(std::round(args[0].asDouble()));
        int c = static_cast<int>(std::round(args[1].asDouble()));
        if (r <= 0 || c <= 0)
            throw std::runtime_error("Runtime Error: matrix() dimensions must be positive.");

        // 无元素 → 零矩阵
        if (static_cast<int>(args.size()) == 2)
            return Value(RealMatrix(r, c));

        int total = r * c;
        if (static_cast<int>(args.size()) - 2 != total)
            throw std::runtime_error("Runtime Error: matrix() element count mismatch: "
                "expected " + std::to_string(total) + ", got " +
                std::to_string(args.size() - 2) + ".");

        // 类型检测
        bool hasString = false, hasComplex = false;
        for (int i = 2; i < static_cast<int>(args.size()); ++i) {
            if (std::holds_alternative<std::string>(args[i].data))
                hasString = true;
            else if (std::holds_alternative<Complex>(args[i].data))
                hasComplex = true;
        }

        if (hasString) {
            // StringMatrix: 所有元素转字符串
            std::vector<std::string> flat;
            flat.reserve(total);
            for (int i = 0; i < total; ++i) {
                const auto& v = args[i + 2];
                if (std::holds_alternative<std::string>(v.data))
                    flat.push_back(std::get<std::string>(v.data));
                else {
                    std::ostringstream oss; oss << v;
                    flat.push_back(oss.str());
                }
            }
            return Value(StringMatrix(r, c, flat));
        }

        if (hasComplex) {
            // ComplexMatrix
            std::vector<Complex> flat;
            flat.reserve(total);
            for (int i = 0; i < total; ++i)
                flat.push_back(args[i + 2].asComplex());
            return Value(ComplexMatrix(r, c, flat));
        }

        // RealMatrix
        std::vector<double> flat;
        flat.reserve(total);
        for (int i = 0; i < total; ++i)
            flat.push_back(args[i + 2].asDouble());
        return Value(RealMatrix(r, c, flat));
        });

    regMath("sin", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matSin());
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matSin());
        if (std::holds_alternative<Complex>(args[0].data)) return Value(sin(std::get<Complex>(args[0].data)));
        return Value(std::sin(args[0].asDouble()));
    });
    regMath("cos", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matCos());
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matCos());
        if (std::holds_alternative<Complex>(args[0].data)) return Value(cos(std::get<Complex>(args[0].data)));
        return Value(std::cos(args[0].asDouble()));
    });
    regMath("tan", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matTan());
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matTan());
        if (std::holds_alternative<Complex>(args[0].data)) return Value(tan(std::get<Complex>(args[0].data)));
        return Value(std::tan(args[0].asDouble()));
    });
    regMath("exp", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matExp());
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matExp());
        if (std::holds_alternative<Complex>(args[0].data)) return Value(exp(std::get<Complex>(args[0].data)));
        return Value(std::exp(args[0].asDouble()));
    });
    regMath("sinh", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matSinh());
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matSinh());
        if (std::holds_alternative<Complex>(args[0].data)) return Value(sinh(std::get<Complex>(args[0].data)));
        return Value(std::sinh(args[0].asDouble()));
    });
    regMath("cosh", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matCosh());
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matCosh());
        if (std::holds_alternative<Complex>(args[0].data)) return Value(cosh(std::get<Complex>(args[0].data)));
        return Value(std::cosh(args[0].asDouble()));
    });
    regMath("tanh", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matTanh());
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matTanh());
        if (std::holds_alternative<Complex>(args[0].data)) return Value(tanh(std::get<Complex>(args[0].data)));
        return Value(std::tanh(args[0].asDouble()));
    });
    regMath("cot", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) {
            auto m = std::get<RealMatrix>(args[0].data).matTan();
            std::vector<double> flat = m.rawData();
            for(auto& v : flat) v = 1.0 / v;
            return Value(RealMatrix(m.getRows(), m.getCols(), flat));
        }
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) {
            auto m = std::get<ComplexMatrix>(args[0].data).matTan();
            std::vector<Complex> flat = m.rawData();
            for(auto& v : flat) v = Complex(1.0, 0.0) / v;
            return Value(ComplexMatrix(m.getRows(), m.getCols(), flat));
        }
        if (std::holds_alternative<Complex>(args[0].data)) return Value(Complex(1.0, 0.0) / tan(std::get<Complex>(args[0].data)));
        return Value(1.0 / std::tan(args[0].asDouble()));
    });
    regMath("sec", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) {
            auto m = std::get<RealMatrix>(args[0].data).matCos();
            std::vector<double> flat = m.rawData();
            for(auto& v : flat) v = 1.0 / v;
            return Value(RealMatrix(m.getRows(), m.getCols(), flat));
        }
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) {
            auto m = std::get<ComplexMatrix>(args[0].data).matCos();
            std::vector<Complex> flat = m.rawData();
            for(auto& v : flat) v = Complex(1.0, 0.0) / v;
            return Value(ComplexMatrix(m.getRows(), m.getCols(), flat));
        }
        if (std::holds_alternative<Complex>(args[0].data)) return Value(Complex(1.0, 0.0) / cos(std::get<Complex>(args[0].data)));
        return Value(1.0 / std::cos(args[0].asDouble()));
    });
    regMath("csc", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) {
            auto m = std::get<RealMatrix>(args[0].data).matSin();
            std::vector<double> flat = m.rawData();
            for(auto& v : flat) v = 1.0 / v;
            return Value(RealMatrix(m.getRows(), m.getCols(), flat));
        }
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) {
            auto m = std::get<ComplexMatrix>(args[0].data).matSin();
            std::vector<Complex> flat = m.rawData();
            for(auto& v : flat) v = Complex(1.0, 0.0) / v;
            return Value(ComplexMatrix(m.getRows(), m.getCols(), flat));
        }
        if (std::holds_alternative<Complex>(args[0].data)) return Value(Complex(1.0, 0.0) / sin(std::get<Complex>(args[0].data)));
        return Value(1.0 / std::sin(args[0].asDouble()));
    });

    regMath("log", { 1, 2 }, [](const std::vector<Value>& args) -> Value {
        if (args.size() == 1) {
            if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(matLog(std::get<RealMatrix>(args[0].data)));
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(matLog(std::get<ComplexMatrix>(args[0].data)));
            if (std::holds_alternative<Complex>(args[0].data)) return Value(log(std::get<Complex>(args[0].data)));
            double x = args[0].asDouble();
            if (x == 0) throw std::runtime_error("Math Error: Logarithm of zero.");
            if (x < 0) return Value(log(Complex(x, 0.0)));
            return Value(std::log(x));
        }
        Complex base = args[0].asComplex(), x = args[1].asComplex();
        return Value(log(x) / log(base));
    });

    regMath("ln", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(matLog(std::get<RealMatrix>(args[0].data)));
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(matLog(std::get<ComplexMatrix>(args[0].data)));
        if (std::holds_alternative<Complex>(args[0].data)) return Value(log(std::get<Complex>(args[0].data)));
        double x = args[0].asDouble();
        if (x == 0) throw std::runtime_error("Math Error: Logarithm of zero.");
        if (x < 0) return Value(log(Complex(x, 0.0)));
        return Value(std::log(x));
    });

    regMath("sqrt", { 1 }, [](const std::vector<Value>& args) -> Value {
        return args[0] ^ Value(Fraction(1, 2));
    });

    regMath("cbrt", { 1 }, [](const std::vector<Value>& args) -> Value {
        return args[0] ^ Value(Fraction(1, 3));
    });

    reg("matpow", { 2 }, [](const std::vector<Value>& args) -> Value {
        return Value(matPow(args[0].asComplexMatrix(), args[1].asComplexMatrix()));
    });

    regMath("asin", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<Complex>(args[0].data)) return Value(asin(std::get<Complex>(args[0].data)));
        double x = args[0].asDouble();
        if (x < -1.0 || x > 1.0) return Value(asin(Complex(x, 0.0)));
        return Value(std::asin(x));
    });
    regMath("acos", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<Complex>(args[0].data)) return Value(acos(std::get<Complex>(args[0].data)));
        double x = args[0].asDouble();
        if (x < -1.0 || x > 1.0) return Value(acos(Complex(x, 0.0)));
        return Value(std::acos(x));
    });
    regMath("atan", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<Complex>(args[0].data)) return Value(atan(std::get<Complex>(args[0].data)));
        return Value(std::atan(args[0].asDouble()));
    });
    regMath("atan2", { 2 }, [](const std::vector<Value>& args) -> Value {
        return Value(std::atan2(args[0].asDouble(), args[1].asDouble()));
        });

    regMath("asinh", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<Complex>(args[0].data)) {
            Complex z = std::get<Complex>(args[0].data);
            return Value(log(z + sqrt(z * z + Complex(1.0, 0.0))));
        }
        return Value(std::asinh(args[0].asDouble()));
    });
    regMath("acosh", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<Complex>(args[0].data)) {
            Complex z = std::get<Complex>(args[0].data);
            return Value(log(z + sqrt(z * z - Complex(1.0, 0.0))));
        }
        double x = args[0].asDouble();
        if (x < 1.0) {
            Complex z(x, 0.0);
            return Value(log(z + sqrt(z * z - Complex(1.0, 0.0))));
        }
        return Value(std::acosh(x));
    });
    regMath("atanh", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<Complex>(args[0].data)) {
            Complex z = std::get<Complex>(args[0].data);
            return Value(Complex(0.5, 0.0) * log((Complex(1.0, 0.0) + z) / (Complex(1.0, 0.0) - z)));
        }
        double x = args[0].asDouble();
        if (x <= -1.0 || x >= 1.0) {
            Complex z(x, 0.0);
            return Value(Complex(0.5, 0.0) * log((Complex(1.0, 0.0) + z) / (Complex(1.0, 0.0) - z)));
        }
        return Value(std::atanh(x));
    });

    regMath("erf", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(std::erf(args[0].asDouble()));
        });
    // 通用高精度数值积分器 (Simpson's 1/3 Rule)
    auto numInteg = [](auto f, double a, double b) -> double {
        int n = std::max(1000, static_cast<int>(std::abs(b - a) * 1000));
        if (n % 2 != 0) n++;
        if (n > 100000) n = 100000; // 限制最大迭代次数防止卡死
        double h = (b - a) / n;
        double sum = f(a) + f(b);
        for (int i = 1; i < n; i += 2) { jc::checkInterrupt(); sum += 4 * f(a + i * h); }
        for (int i = 2; i < n - 1; i += 2) { jc::checkInterrupt(); sum += 2 * f(a + i * h); }
        return sum * h / 3.0;
    };

    regMath("fresnel_s", { 1 }, [numInteg](const std::vector<Value>& args) -> Value {
        double x = args[0].asDouble();
        return Value(numInteg([](double t) { return std::sin(1.57079632679489661923 * t * t); }, 0.0, x));
        });
    regMath("fresnel_c", { 1 }, [numInteg](const std::vector<Value>& args) -> Value {
        double x = args[0].asDouble();
        return Value(numInteg([](double t) { return std::cos(1.57079632679489661923 * t * t); }, 0.0, x));
        });
    regMath("Si", { 1 }, [numInteg](const std::vector<Value>& args) -> Value {
        double x = args[0].asDouble();
        return Value(numInteg([](double t) { return t == 0.0 ? 1.0 : std::sin(t) / t; }, 0.0, x));
        });
    regMath("Ci", { 1 }, [numInteg](const std::vector<Value>& args) -> Value {
        double x = args[0].asDouble();
        if (x <= 0.0) throw std::runtime_error("Math Error: Ci(x) is only real for x > 0.");
        double gamma = 0.577215664901532860606; // Euler-Mascheroni constant
        return Value(gamma + std::log(x) + numInteg([](double t) { return t == 0.0 ? 0.0 : (std::cos(t) - 1.0) / t; }, 0.0, x));
        });
    regMath("Ei", { 1 }, [numInteg](const std::vector<Value>& args) -> Value {
        double x = args[0].asDouble();
        if (x == 0.0) throw std::runtime_error("Math Error: Ei(0) is undefined.");
        double gamma = 0.577215664901532860606; // Euler-Mascheroni constant
        return Value(gamma + std::log(std::abs(x)) + numInteg([](double t) { return t == 0.0 ? 1.0 : (std::exp(t) - 1.0) / t; }, 0.0, x));
        });
    regMath("Li", { 1 }, [numInteg](const std::vector<Value>& args) -> Value {
        double x = args[0].asDouble();
        if (x <= 0.0 || x == 1.0) throw std::runtime_error("Math Error: Li(x) is defined for x > 0 and x != 1.");
        double lnx = std::log(x);
        double gamma = 0.577215664901532860606;
        return Value(gamma + std::log(std::abs(lnx)) + numInteg([](double t) { return t == 0.0 ? 1.0 : (std::exp(t) - 1.0) / t; }, 0.0, lnx));
        });

    regMath("abs", { 1 }, [](const std::vector<Value>& args) -> Value {
        // ★ Dunder 钩子: __abs__
        if (std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) {
            auto inst = std::get<std::shared_ptr<Instance>>(args[0].data);
            auto [found, result] = tryCallDunder(inst, "__abs__");
            if (found) return result;
        }
        if (std::holds_alternative<BigInt>(args[0].data)) return Value(std::get<BigInt>(args[0].data).abs());
        if (std::holds_alternative<Complex>(args[0].data)) return Value(std::get<Complex>(args[0].data).modulus());
        if (std::holds_alternative<Fraction>(args[0].data)) return Value(std::get<Fraction>(args[0].data).abs());
        return Value(std::abs(args[0].asDouble()));
        });

    regMath("pow", { 2 }, [](const std::vector<Value>& args) -> Value {
        Value x = args[0];
        Value y = args[1];

        // 强制把指数降为 double 或 complex，彻底切断精确方根逻辑
        Value numY = y.isComplex() ? Value(y.asComplex()) : Value(y.asDouble());

        // 把降维后的指数扔给底层的 operator^
        return x ^ numY;
        });

    regMath("root", { 2 }, [](const std::vector<Value>& args) -> Value {
        return args[0] ^ (Value(BigInt(1)) / args[1]);
    });

    // 通用取整分发器
    auto roundDispatch = [](const std::vector<Value>& args, const std::string& name,
        std::function<double(double)> baseFn) -> Value {
            if (args.size() < 1 || args.size() > 2)
                throw std::runtime_error("Runtime Error: " + name + "() expects 1 or 2 arguments.");
            int n = 0;
            bool hasN = (args.size() == 2);
            if (hasN) n = static_cast<int>(std::round(args[1].asDouble()));
            double factor = std::pow(10.0, n);
            auto fn = [baseFn, factor](double x) -> double { return baseFn(x * factor) / factor; };
            if (std::holds_alternative<Complex>(args[0].data)) {
                const Complex& c = std::get<Complex>(args[0].data);
                Complex result(fn(c.real), fn(c.imag));
                if (jc::Tol::isEq(result.imag, 0.0)) {
                    if (!hasN || n <= 0) return Value(BigInt(static_cast<int64_t>(result.real)));
                    return Value(result.real);
                }
                return Value(result);
            }
            if (std::holds_alternative<RealMatrix>(args[0].data)) {
                const RealMatrix& m = std::get<RealMatrix>(args[0].data);
                std::vector<double> flat;
                flat.reserve(static_cast<size_t>(m.getRows()) * m.getCols());
                for (int i = 0; i < m.getRows(); ++i)
                    for (int j = 0; j < m.getCols(); ++j)
                        flat.push_back(fn(m(i, j)));
                return Value(RealMatrix(m.getRows(), m.getCols(), flat));
            }
            if (std::holds_alternative<ComplexMatrix>(args[0].data)) {
                const ComplexMatrix& m = std::get<ComplexMatrix>(args[0].data);
                std::vector<Complex> flat;
                flat.reserve(static_cast<size_t>(m.getRows()) * m.getCols());
                for (int i = 0; i < m.getRows(); ++i)
                    for (int j = 0; j < m.getCols(); ++j)
                        flat.push_back(Complex(fn(m(i, j).real), fn(m(i, j).imag)));
                return Value(ComplexMatrix(m.getRows(), m.getCols(), flat));
            }
            double result = fn(args[0].asDouble());
            if (!hasN || n <= 0) return Value(BigInt(static_cast<int64_t>(result)));
            return Value(result);
        };

    regMath("round", { 1, 2 }, [roundDispatch](const std::vector<Value>& args) -> Value { return roundDispatch(args, "round", [](double x) { return std::round(x); }); });
    regMath("floor", { 1, 2 }, [roundDispatch](const std::vector<Value>& args) -> Value { return roundDispatch(args, "floor", [](double x) { return std::floor(x); }); });
    regMath("ceil", { 1, 2 }, [roundDispatch](const std::vector<Value>& args) -> Value { return roundDispatch(args, "ceil", [](double x) { return std::ceil(x); }); });
    regMath("trunc", { 1, 2 }, [roundDispatch](const std::vector<Value>& args) -> Value { return roundDispatch(args, "trunc", [](double x) { return std::trunc(x); }); });

    regMath("sgn", { 1 }, [](const std::vector<Value>& args) -> Value { double x = args[0].asDouble(); return Value(x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0)); });
    regMath("deg", { 1 }, [](const std::vector<Value>& args) -> Value { return Value(args[0].asDouble() / Complex::PI * 180.0); });
    regMath("rad", { 1 }, [](const std::vector<Value>& args) -> Value { return Value(args[0].asDouble() / 180.0 * Complex::PI); });
    
    reg("idiv", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (args[0].isBigInt() && args[1].isBigInt()) {
            BigInt a = std::get<BigInt>(args[0].data), b = std::get<BigInt>(args[1].data);
            if (b.isZero()) throw std::runtime_error("Math Error: Division by zero.");
            return Value(a / b);
        }
        double a = args[0].asDouble(), b = args[1].asDouble();
        if (b == 0.0) throw std::runtime_error("Math Error: Division by zero.");
        return Value(std::trunc(a / b));
    });

}

// =================================================================
// [2] 复数属性
// =================================================================
void BuiltinRegistry::registerComplex() {
    reg("Re", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<Complex>(args[0].data)) return Value(std::get<Complex>(args[0].data).real); return args[0]; });
    reg("Im", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<Complex>(args[0].data)) return Value(std::get<Complex>(args[0].data).imag); return Value(0.0); });
    reg("arg", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<Complex>(args[0].data)) return Value(std::get<Complex>(args[0].data).argument()); return Value(args[0].asDouble() >= 0 ? 0.0 : Complex::PI); });
    reg("conj", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<Complex>(args[0].data)) return Value(std::get<Complex>(args[0].data).conjugate()); return args[0]; });
}

// =================================================================
// [3] 分数
// =================================================================
void BuiltinRegistry::registerFraction() {
    reg("frac", { 2 }, [](const std::vector<Value>& args) -> Value {
        BigInt n = args[0].isBigInt() ? std::get<BigInt>(args[0].data) : BigInt(static_cast<int64_t>(std::round(args[0].asDouble())));
        BigInt d = args[1].isBigInt() ? std::get<BigInt>(args[1].data) : BigInt(static_cast<int64_t>(std::round(args[1].asDouble())));
        return Value(Fraction(n, d));
    });
    reg("num", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<Fraction>(args[0].data)) return Value(std::get<Fraction>(args[0].data).getNum()); return args[0]; });
    reg("den", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<Fraction>(args[0].data)) return Value(std::get<Fraction>(args[0].data).getDen()); return Value(1.0); });
}

// =================================================================
// [4] 多项式求解
// =================================================================
void BuiltinRegistry::registerPolySolver() {
    reg("solve", { 2, 3, 4, 5 }, [](const std::vector<Value>& args) -> Value {
        std::vector<Complex> roots;
        if (args.size() == 2) roots = Complex::solveDegreeOne(args[0].asComplex(), args[1].asComplex());
        else if (args.size() == 3) roots = Complex::solveDegreeTwo(args[0].asComplex(), args[1].asComplex(), args[2].asComplex());
        else if (args.size() == 4) roots = Complex::solveDegreeThree(args[0].asComplex(), args[1].asComplex(), args[2].asComplex(), args[3].asComplex());
        else roots = Complex::solveDegreeFour(args[0].asComplex(), args[1].asComplex(), args[2].asComplex(), args[3].asComplex(), args[4].asComplex());
        return Value(ComplexMatrix(static_cast<int>(roots.size()), 1, roots));
    });

}

// =================================================================
// [5] 矩阵运算
// =================================================================
void BuiltinRegistry::registerMatrixOps() {

    auto matrixDispatch1 = [](const Value& arg, auto func) -> Value {
        if (std::holds_alternative<RealMatrix>(arg.data)) return Value(func(std::get<RealMatrix>(arg.data)));
        if (std::holds_alternative<ComplexMatrix>(arg.data)) return Value(func(std::get<ComplexMatrix>(arg.data)));
        throw std::runtime_error("Type Error: Expected a matrix.");
    };

    // --- 标量广播 ---
    reg("addS", { 2 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double c = args[1].asDouble(); std::vector<double> flat = m.rawData(); for (auto& v : flat) v += c; return Value(RealMatrix(m.getRows(), m.getCols(), flat)); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); Complex c = args[1].asComplex(); std::vector<Complex> flat = m.rawData(); for (auto& v : flat) v = v + c; return Value(ComplexMatrix(m.getRows(), m.getCols(), flat)); } throw std::runtime_error("Type Error: addS() requires a matrix."); });
    reg("subS", { 2 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double c = args[1].asDouble(); std::vector<double> flat = m.rawData(); for (auto& v : flat) v -= c; return Value(RealMatrix(m.getRows(), m.getCols(), flat)); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); Complex c = args[1].asComplex(); std::vector<Complex> flat = m.rawData(); for (auto& v : flat) v = v - c; return Value(ComplexMatrix(m.getRows(), m.getCols(), flat)); } throw std::runtime_error("Type Error: subS() requires a matrix."); });
    reg("mulS", { 2 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double c = args[1].asDouble(); std::vector<double> flat = m.rawData(); for (auto& v : flat) v *= c; return Value(RealMatrix(m.getRows(), m.getCols(), flat)); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); Complex c = args[1].asComplex(); std::vector<Complex> flat = m.rawData(); for (auto& v : flat) v = v * c; return Value(ComplexMatrix(m.getRows(), m.getCols(), flat)); } throw std::runtime_error("Type Error: mulS() requires a matrix."); });
    reg("divS", { 2 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double c = args[1].asDouble(); if (jc::Tol::isEq(c, 0.0)) throw std::runtime_error("Math Error: Division by zero."); std::vector<double> flat = m.rawData(); for (auto& v : flat) v /= c; return Value(RealMatrix(m.getRows(), m.getCols(), flat)); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); Complex c = args[1].asComplex(); if (jc::Tol::isEq(c.modulus(), 0.0)) throw std::runtime_error("Math Error: Division by zero."); std::vector<Complex> flat = m.rawData(); for (auto& v : flat) v = v / c; return Value(ComplexMatrix(m.getRows(), m.getCols(), flat)); } throw std::runtime_error("Type Error: divS() requires a matrix."); });
    reg("powS", { 2 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double c = args[1].asDouble(); std::vector<double> flat = m.rawData(); for (auto& v : flat) v = std::pow(v, c); return Value(RealMatrix(m.getRows(), m.getCols(), flat)); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); Complex c = args[1].asComplex(); std::vector<Complex> flat = m.rawData(); for (auto& v : flat) v = v ^ c; return Value(ComplexMatrix(m.getRows(), m.getCols(), flat)); } throw std::runtime_error("Type Error: powS() requires a matrix."); });
    reg("modS", { 2 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double c = args[1].asDouble(); if (jc::Tol::isEq(c, 0.0)) throw std::runtime_error("Math Error: Modulo by zero."); std::vector<double> flat = m.rawData(); for (auto& v : flat) { v = std::fmod(v, c); if (v < 0) v += std::abs(c); } return Value(RealMatrix(m.getRows(), m.getCols(), flat)); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); double c = args[1].asDouble(); if (jc::Tol::isEq(c, 0.0)) throw std::runtime_error("Math Error: Modulo by zero."); std::vector<Complex> flat = m.rawData(); for (auto& v : flat) { double re = std::fmod(v.real, c); double im = std::fmod(v.imag, c); if (re < 0) re += std::abs(c); if (im < 0) im += std::abs(c); v = Complex(re, im); } return Value(ComplexMatrix(m.getRows(), m.getCols(), flat)); } throw std::runtime_error("Type Error: modS() requires a matrix."); });

    // --- 性质 ---
    reg("det", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).determinant()); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).determinant()); throw std::runtime_error("Type Error: det() requires a matrix."); });
    reg("inv", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).inverse()); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).inverse()); throw std::runtime_error("Type Error: inv() requires a matrix."); });
    reg("trans", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).transpose()); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).transpose()); if (std::holds_alternative<StringMatrix>(args[0].data)) return Value(std::get<StringMatrix>(args[0].data).transpose()); throw std::runtime_error("Type Error: trans() requires a matrix."); });
    reg("gauss", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).gaussianElimination().first); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).gaussianElimination().first); throw std::runtime_error("Type Error: gauss() requires a matrix."); });

    reg("rank", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { return matrixDispatch1(args[0], [](const auto& m) { return static_cast<double>(m.rank()); }); });
    reg("tr", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { return matrixDispatch1(args[0], [](const auto& m) { return m.trace(); }); });
    reg("norm", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { return matrixDispatch1(args[0], [](const auto& m) { return m.norm(); }); });
    reg("cond", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { return matrixDispatch1(args[0], [](const auto& m) { return m.condition(); }); });
    reg("adj", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { return matrixDispatch1(args[0], [](const auto& m) { return m.adjugate(); }); });
    reg("perm", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { return matrixDispatch1(args[0], [](const auto& m) { return m.permanent(); }); });
    reg("sum", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value {
        // ★ 新增：无缝支持 List 容器
        if (std::holds_alternative<List>(args[0].data)) {
            const auto& L = std::get<List>(args[0].data);
            Value s = Value(0.0);
            for (const auto& e : L.raw()) {
                s = s + helpers::anyToVal(e);  // 利用 Value 的重载 +
            }
            return s;
        }
        return matrixDispatch1(args[0], [](const auto& m) { return m.sum(); });
        });

    reg("prod", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value {
        // ★ 新增：无缝支持 List 容器
        if (std::holds_alternative<List>(args[0].data)) {
            const auto& L = std::get<List>(args[0].data);
            Value p = Value(1.0);
            for (const auto& e : L.raw()) {
                p = p * helpers::anyToVal(e);  // 利用 Value 的重载 *
            }
            return p;
        }
        return matrixDispatch1(args[0], [](const auto& m) { return m.product(); });
        });    
    reg("null", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { return matrixDispatch1(args[0], [](const auto& m) { return m.nullSpace(); }); });
    reg("orth", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { return matrixDispatch1(args[0], [](const auto& m) { return m.orthogonalize(); }); });
    reg("ctrans", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { return matrixDispatch1(args[0], [](const auto& m) { return m.conjugateTranspose(); }); });
    reg("mpow", { 2 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { int n = static_cast<int>(std::round(args[1].asDouble())); return matrixDispatch1(args[0], [n](const auto& m) { return m.power(n); }); });

    // --- 维度 ---
    reg("row", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(static_cast<double>(std::get<RealMatrix>(args[0].data).getRows()));
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(static_cast<double>(std::get<ComplexMatrix>(args[0].data).getRows()));
        if (std::holds_alternative<StringMatrix>(args[0].data)) return Value(static_cast<double>(std::get<StringMatrix>(args[0].data).getRows()));
        throw std::runtime_error("Type Error: row() requires a matrix.");
    });
    reg("col", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(static_cast<double>(std::get<RealMatrix>(args[0].data).getCols()));
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(static_cast<double>(std::get<ComplexMatrix>(args[0].data).getCols()));
        if (std::holds_alternative<StringMatrix>(args[0].data)) return Value(static_cast<double>(std::get<StringMatrix>(args[0].data).getCols()));
        throw std::runtime_error("Type Error: col() requires a matrix.");
    });
    builtins["rows"] = builtins["row"]; builtinArity["rows"] = builtinArity["row"];
    builtins["cols"] = builtins["col"]; builtinArity["cols"] = builtinArity["col"];

    // --- 元素/行列访问 ---
    reg("get", { 3 }, [](const std::vector<Value>& args) -> Value { int r = static_cast<int>(std::round(args[1].asDouble())), c = static_cast<int>(std::round(args[2].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data)(r, c)); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data)(r, c)); if (std::holds_alternative<StringMatrix>(args[0].data)) return Value(std::get<StringMatrix>(args[0].data)(r, c)); throw std::runtime_error("Type Error: get() requires a matrix."); });
    reg("set", { 4 }, [](const std::vector<Value>& args) -> Value { int r = static_cast<int>(std::round(args[1].asDouble())), c = static_cast<int>(std::round(args[2].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix res = std::get<RealMatrix>(args[0].data); res(r, c) = args[3].asDouble(); return Value(res); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix res = std::get<ComplexMatrix>(args[0].data); res(r, c) = args[3].asComplex(); return Value(res); } if (std::holds_alternative<StringMatrix>(args[0].data)) { StringMatrix res = std::get<StringMatrix>(args[0].data); if (std::holds_alternative<std::string>(args[3].data)) res(r, c) = std::get<std::string>(args[3].data); else { std::ostringstream oss; oss << args[3]; res(r, c) = oss.str(); } return Value(res); } throw std::runtime_error("Type Error: set() requires a matrix."); });

    // 行列操作（简写宏化）
    #define ROW_COL_OP(NAME, BODY) reg(NAME, { 2 }, [](const std::vector<Value>& args) -> Value { \
        int idx = static_cast<int>(std::round(args[1].asDouble())); \
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).BODY); \
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).BODY); \
        if (std::holds_alternative<StringMatrix>(args[0].data)) return Value(std::get<StringMatrix>(args[0].data).BODY); \
        throw std::runtime_error("Type Error: requires a matrix."); })

    ROW_COL_OP("getR", getRow(idx));
    ROW_COL_OP("getC", getCol(idx));
    ROW_COL_OP("delR", deleteRow(idx));
    ROW_COL_OP("delC", deleteCol(idx));
    #undef ROW_COL_OP

    reg("swapR", { 3 }, [](const std::vector<Value>& args) -> Value { int r1 = static_cast<int>(std::round(args[1].asDouble())), r2 = static_cast<int>(std::round(args[2].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); m.swapRows(r1, r2); return Value(m); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); m.swapRows(r1, r2); return Value(m); } if (std::holds_alternative<StringMatrix>(args[0].data)) { StringMatrix m = std::get<StringMatrix>(args[0].data); m.swapRows(r1, r2); return Value(m); } throw std::runtime_error("Type Error: requires a matrix."); });
    reg("swapC", { 3 }, [](const std::vector<Value>& args) -> Value { int c1 = static_cast<int>(std::round(args[1].asDouble())), c2 = static_cast<int>(std::round(args[2].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); m.swapCols(c1, c2); return Value(m); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); m.swapCols(c1, c2); return Value(m); } if (std::holds_alternative<StringMatrix>(args[0].data)) { StringMatrix m = std::get<StringMatrix>(args[0].data); m.swapCols(c1, c2); return Value(m); } throw std::runtime_error("Type Error: requires a matrix."); });
    reg("multiR", { 3 }, [](const std::vector<Value>& args) -> Value { int r = static_cast<int>(std::round(args[1].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); m.multiplyRow(r, args[2].asDouble()); return Value(m); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); m.multiplyRow(r, args[2].asComplex()); return Value(m); } throw std::runtime_error("Type Error: requires a matrix."); });
    reg("multiC", { 3 }, [](const std::vector<Value>& args) -> Value { int c = static_cast<int>(std::round(args[1].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double s = args[2].asDouble(); for (int r = 0; r < m.getRows(); ++r) m(r, c) = m(r, c) * s; return Value(m); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); Complex s = args[2].asComplex(); for (int r = 0; r < m.getRows(); ++r) m(r, c) = m(r, c) * s; return Value(m); } throw std::runtime_error("Type Error: requires a matrix."); });
    reg("addR", { 4 }, [](const std::vector<Value>& args) -> Value { int r1 = static_cast<int>(std::round(args[1].asDouble())), r2 = static_cast<int>(std::round(args[2].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); m.addRows(r1, r2, args[3].asDouble()); return Value(m); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); m.addRows(r1, r2, args[3].asComplex()); return Value(m); } throw std::runtime_error("Type Error: requires a matrix."); });
    reg("addC", { 4 }, [](const std::vector<Value>& args) -> Value { int c1 = static_cast<int>(std::round(args[1].asDouble())), c2 = static_cast<int>(std::round(args[2].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) { RealMatrix m = std::get<RealMatrix>(args[0].data); double s = args[3].asDouble(); for (int r = 0; r < m.getRows(); ++r) m(r, c1) = m(r, c1) + s * m(r, c2); return Value(m); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { ComplexMatrix m = std::get<ComplexMatrix>(args[0].data); Complex s = args[3].asComplex(); for (int r = 0; r < m.getRows(); ++r) m(r, c1) = m(r, c1) + s * m(r, c2); return Value(m); } throw std::runtime_error("Type Error: requires a matrix."); });

    // --- 结构 ---
    reg("reshape", { 3 }, [](const std::vector<Value>& args) -> Value { int r = static_cast<int>(std::round(args[1].asDouble())), c = static_cast<int>(std::round(args[2].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).reshape(r, c)); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).reshape(r, c)); if (std::holds_alternative<StringMatrix>(args[0].data)) return Value(std::get<StringMatrix>(args[0].data).reshape(r, c)); throw std::runtime_error("Type Error: reshape() requires a matrix."); });
    reg("sub", { 3 }, [](const std::vector<Value>& args) -> Value { int r = static_cast<int>(std::round(args[1].asDouble())), c = static_cast<int>(std::round(args[2].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).subMatrix(r, c)); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).subMatrix(r, c)); throw std::runtime_error("Type Error: requires a matrix."); });
    reg("cof", { 3 }, [](const std::vector<Value>& args) -> Value { int r = static_cast<int>(std::round(args[1].asDouble())), c = static_cast<int>(std::round(args[2].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).cofactor(r, c)); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).cofactor(r, c)); throw std::runtime_error("Type Error: requires a matrix."); });
    reg("Acof", { 3 }, [](const std::vector<Value>& args) -> Value { int r = static_cast<int>(std::round(args[1].asDouble())), c = static_cast<int>(std::round(args[2].asDouble())); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).algebraicCofactor(r, c)); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).algebraicCofactor(r, c)); throw std::runtime_error("Type Error: requires a matrix."); });

    // --- 拼接 ---
    reg("integR", { 2 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data) && std::holds_alternative<RealMatrix>(args[1].data)) return Value(std::get<RealMatrix>(args[0].data).integR(std::get<RealMatrix>(args[1].data))); if (std::holds_alternative<StringMatrix>(args[0].data) && std::holds_alternative<StringMatrix>(args[1].data)) return Value(std::get<StringMatrix>(args[0].data).integR(std::get<StringMatrix>(args[1].data))); return Value(args[0].asComplexMatrix().integR(args[1].asComplexMatrix())); });
    reg("integC", { 2 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data) && std::holds_alternative<RealMatrix>(args[1].data)) return Value(std::get<RealMatrix>(args[0].data).integC(std::get<RealMatrix>(args[1].data))); if (std::holds_alternative<StringMatrix>(args[0].data) && std::holds_alternative<StringMatrix>(args[1].data)) return Value(std::get<StringMatrix>(args[0].data).integC(std::get<StringMatrix>(args[1].data))); return Value(args[0].asComplexMatrix().integC(args[1].asComplexMatrix())); });
    reg("integD", { 2 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data) && std::holds_alternative<RealMatrix>(args[1].data)) return Value(std::get<RealMatrix>(args[0].data).integD(std::get<RealMatrix>(args[1].data))); return Value(args[0].asComplexMatrix().integD(args[1].asComplexMatrix())); });

    // --- 生成器 ---
    reg("id", { 1 }, [](const std::vector<Value>& args) -> Value { int n = static_cast<int>(std::round(args[0].asDouble())); if (n < 1) throw std::runtime_error("Runtime Error: Size must be positive."); return Value(RealMatrix::identity(n)); });
    reg("ones", { 1, 2 }, [](const std::vector<Value>& args) -> Value { if (args.size() == 1) { int n = static_cast<int>(std::round(args[0].asDouble())); return Value(RealMatrix::ones(n, n)); } int r = static_cast<int>(std::round(args[0].asDouble())), c = static_cast<int>(std::round(args[1].asDouble())); return Value(RealMatrix::ones(r, c)); });
    reg("zeros", { 1, 2 }, [](const std::vector<Value>& args) -> Value { if (args.size() == 1) { int n = static_cast<int>(std::round(args[0].asDouble())); return Value(RealMatrix::zeros(n, n)); } int r = static_cast<int>(std::round(args[0].asDouble())), c = static_cast<int>(std::round(args[1].asDouble())); return Value(RealMatrix::zeros(r, c)); });
}

// =================================================================
// [6] 矩阵分解与特征值
// =================================================================
void BuiltinRegistry::registerDecompositions() {
    reg("qr_Q", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).qrDecomposition().first); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).qrDecomposition().first); throw std::runtime_error("Type Error: requires a matrix."); });
    reg("qr_R", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).qrDecomposition().second); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).qrDecomposition().second); throw std::runtime_error("Type Error: requires a matrix."); });
    reg("lu_L", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).luDecomposition().L); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).luDecomposition().L); throw std::runtime_error("Type Error: requires a matrix."); });
    reg("lu_U", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).luDecomposition().U); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).luDecomposition().U); throw std::runtime_error("Type Error: requires a matrix."); });
    reg("lu_P", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).luDecomposition().P); if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).luDecomposition().P); throw std::runtime_error("Type Error: requires a matrix."); });
    reg("eig", { 1 }, [](const std::vector<Value>& args) -> Value { std::vector<Complex> vals; if (std::holds_alternative<RealMatrix>(args[0].data)) vals = computeEigenvalues(std::get<RealMatrix>(args[0].data)); else if (std::holds_alternative<ComplexMatrix>(args[0].data)) vals = computeEigenvalues(std::get<ComplexMatrix>(args[0].data)); else throw std::runtime_error("Type Error: requires a matrix."); return Value(ComplexMatrix(static_cast<int>(vals.size()), 1, vals)); });
    reg("eigvec", { 1 }, [](const std::vector<Value>& args) -> Value { ComplexMatrix A = std::holds_alternative<RealMatrix>(args[0].data) ? std::get<RealMatrix>(args[0].data).toComplexMatrix() : std::get<ComplexMatrix>(args[0].data); auto vals = computeEigenvalues(A); return Value(computeEigenvectors(A, vals)); });
    reg("diag", { 1 }, [](const std::vector<Value>& args) -> Value { ComplexMatrix A = std::holds_alternative<RealMatrix>(args[0].data) ? std::get<RealMatrix>(args[0].data).toComplexMatrix() : std::get<ComplexMatrix>(args[0].data); auto [P, D] = diagonalize(A); return Value(D); });
    reg("diagP", { 1 }, [](const std::vector<Value>& args) -> Value { ComplexMatrix A = std::holds_alternative<RealMatrix>(args[0].data) ? std::get<RealMatrix>(args[0].data).toComplexMatrix() : std::get<ComplexMatrix>(args[0].data); auto [P, D] = diagonalize(A); return Value(P); });
}

// =================================================================
// [7] 线性方程组
// =================================================================
void BuiltinRegistry::registerLinearSolvers() {
    reg("lsolve", { 2 }, [](const std::vector<Value>& args) -> Value {
        ComplexMatrix A = args[0].asComplexMatrix(), b = args[1].asComplexMatrix();
        if (A.getRows() != b.getRows()) throw std::runtime_error("Math Error: Row count mismatch.");
        if (b.getCols() != 1) throw std::runtime_error("Math Error: b must be Nx1.");
        int n = A.getCols();
        ComplexMatrix aug = A.integR(b);
        int rankA = A.rank(), rankAug = aug.rank();
        if (rankA != rankAug) { ComplexMatrix AH = A.conjugateTranspose(); ComplexMatrix nA = AH * A, nb = AH * b; ComplexMatrix a2 = nA.integR(nb); auto [r2, s2] = a2.gaussianElimination(); int n2 = nA.getCols(); std::vector<Complex> sol(n2); for (int i = 0; i < n2; ++i) sol[i] = r2(i, n2); return Value(ComplexMatrix(n2, 1, sol)); }
        auto [rref, swaps] = aug.gaussianElimination();
        std::vector<int> pivotCols; for (int i = 0; i < A.getRows(); ++i) for (int j = 0; j < n; ++j) { if (!ComplexMatrix::isEssentiallyZero(rref(i, j))) { pivotCols.push_back(j); break; } }
        std::vector<Complex> particular(n, Complex(0, 0)); for (int p = 0; p < static_cast<int>(pivotCols.size()); ++p) particular[pivotCols[p]] = rref(p, n);
        return Value(ComplexMatrix(n, 1, particular));
    });
    reg("linfo", { 2 }, [](const std::vector<Value>& args) -> Value {
        ComplexMatrix A = args[0].asComplexMatrix(), b = args[1].asComplexMatrix();
        if (A.getRows() != b.getRows()) throw std::runtime_error("Math Error: Row count mismatch.");
        int m = A.getRows(), n = A.getCols(); ComplexMatrix aug = A.integR(b); int rA = A.rank(), rAug = aug.rank();
        std::cout << "Equations: " << m << "  Variables: " << n << "  rank(A): " << rA << "  rank([A|b]): " << rAug << std::endl;
        if (rA != rAug) std::cout << "Status: NO SOLUTION -> lsolve gives least squares." << std::endl;
        else if (rA == n) std::cout << "Status: UNIQUE SOLUTION" << std::endl;
        else std::cout << "Status: INFINITE SOLUTIONS  Free vars: " << (n - rA) << std::endl;
        return Value::none();
    });
    reg("lstsq", { 2 }, [](const std::vector<Value>& args) -> Value { ComplexMatrix A = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); ComplexMatrix AH = A.conjugateTranspose(); ComplexMatrix aug = (AH * A).integR(AH * b); auto [rref, sw] = aug.gaussianElimination(); int n = (AH * A).getCols(); std::vector<Complex> sol(n); for (int i = 0; i < n; ++i) sol[i] = rref(i, n); return Value(ComplexMatrix(n, 1, sol)); });
    reg("residual", { 3 }, [](const std::vector<Value>& args) -> Value { return Value(args[2].asComplexMatrix() - args[0].asComplexMatrix() * args[1].asComplexMatrix()); });
}

// =================================================================
// [8] 向量引擎
// =================================================================
void BuiltinRegistry::registerVectors() {
    auto assertVec = [](const Value& v, const std::string& f) { if (std::holds_alternative<RealMatrix>(v.data)) { if (std::get<RealMatrix>(v.data).getCols() != 1) throw std::runtime_error(f + "() expects Nx1 column vector."); } else if (std::holds_alternative<ComplexMatrix>(v.data)) { if (std::get<ComplexMatrix>(v.data).getCols() != 1) throw std::runtime_error(f + "() expects Nx1 column vector."); } else throw std::runtime_error(f + "() requires a matrix."); };

    reg("dim", { 1 }, [assertVec](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<List>(args[0].data))
            return Value(static_cast<double>(std::get<List>(args[0].data).size()));
        assertVec(args[0], "dim");
        if (std::holds_alternative<RealMatrix>(args[0].data))
            return Value(static_cast<double>(std::get<RealMatrix>(args[0].data).getRows()));
        return Value(static_cast<double>(std::get<ComplexMatrix>(args[0].data).getRows()));
        });    
    reg("dot", { 2 }, [assertVec](const std::vector<Value>& args) -> Value { assertVec(args[0], "dot"); assertVec(args[1], "dot"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); if (a.getRows() != b.getRows()) throw std::runtime_error("Math Error: Dimension mismatch."); return Value((a.conjugateTranspose() * b)(0, 0)); });
    reg("vnorm", { 1 }, [assertVec](const std::vector<Value>& args) -> Value { assertVec(args[0], "vnorm"); ComplexMatrix v = args[0].asComplexMatrix(); return Value(std::sqrt((v.conjugateTranspose() * v)(0, 0).real)); });
    reg("normalize", { 1 }, [assertVec](const std::vector<Value>& args) -> Value { assertVec(args[0], "normalize"); ComplexMatrix v = args[0].asComplexMatrix(); double len = std::sqrt((v.conjugateTranspose() * v)(0, 0).real); if (jc::Tol::isEq(len, 0.0)) throw std::runtime_error("Math Error: Cannot normalize a zero vector."); return Value(v / Complex(len)); });
    reg("cross", { 2 }, [assertVec](const std::vector<Value>& args) -> Value { assertVec(args[0], "cross"); assertVec(args[1], "cross"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); if (a.getRows() != 3 || b.getRows() != 3) throw std::runtime_error("Math Error: Cross product is 3D only."); std::vector<Complex> r = { a(1,0)*b(2,0)-a(2,0)*b(1,0), a(2,0)*b(0,0)-a(0,0)*b(2,0), a(0,0)*b(1,0)-a(1,0)*b(0,0) }; return Value(ComplexMatrix(3, 1, r)); });
    reg("angle", { 2 }, [assertVec](const std::vector<Value>& args) -> Value { assertVec(args[0], "angle"); assertVec(args[1], "angle"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); double nA = std::sqrt((a.conjugateTranspose()*a)(0,0).real), nB = std::sqrt((b.conjugateTranspose()*b)(0,0).real); if (jc::Tol::isEq(nA, 0.0) || jc::Tol::isEq(nB, 0.0)) throw std::runtime_error("Math Error: Zero vector."); double ct = (a.conjugateTranspose()*b)(0,0).real/(nA*nB); ct = std::max(-1.0, std::min(1.0, ct)); return Value(std::acos(ct)); });
    reg("sproj", { 2 }, [assertVec](const std::vector<Value>& args) -> Value { assertVec(args[0], "sproj"); assertVec(args[1], "sproj"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); double nB = std::sqrt((b.conjugateTranspose()*b)(0,0).real); if (jc::Tol::isEq(nB, 0.0)) throw std::runtime_error("Math Error: Zero vector."); return Value((a.conjugateTranspose()*b)(0,0).real/nB); });
    reg("vproj", { 2 }, [assertVec](const std::vector<Value>& args) -> Value { assertVec(args[0], "vproj"); assertVec(args[1], "vproj"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); Complex dBB = (b.conjugateTranspose()*b)(0,0); if (jc::Tol::isEq(dBB.modulus(), 0.0)) throw std::runtime_error("Math Error: Zero vector."); return Value(b * ((a.conjugateTranspose()*b)(0,0)/dBB)); });
    reg("triple", { 3 }, [assertVec](const std::vector<Value>& args) -> Value { assertVec(args[0], "triple"); assertVec(args[1], "triple"); assertVec(args[2], "triple"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(), c = args[2].asComplexMatrix(); if (a.getRows()!=3||b.getRows()!=3||c.getRows()!=3) throw std::runtime_error("Math Error: 3D only."); std::vector<Complex> bc = { b(1,0)*c(2,0)-b(2,0)*c(1,0), b(2,0)*c(0,0)-b(0,0)*c(2,0), b(0,0)*c(1,0)-b(1,0)*c(0,0) }; return Value(a(0,0)*bc[0]+a(1,0)*bc[1]+a(2,0)*bc[2]); });
    reg("isperp", { 2 }, [assertVec](const std::vector<Value>& args) -> Value { assertVec(args[0], "isperp"); assertVec(args[1], "isperp"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); double innerScale = a.norm()*b.norm(); return Value(Tol::clean((a.conjugateTranspose()*b)(0,0).modulus(), innerScale)==0.0?1.0:0.0); });
    reg("isparallel", { 2 }, [assertVec](const std::vector<Value>& args) -> Value { assertVec(args[0], "isparallel"); assertVec(args[1], "isparallel"); ComplexMatrix a = args[0].asComplexMatrix(), b = args[1].asComplexMatrix(); return Value(a.integR(b).rank()<=1?1.0:0.0); });
}

// =================================================================
// [9] 数论
// =================================================================
void BuiltinRegistry::registerNumberTheory() {
    auto toBigInt = [](const Value& v) -> BigInt {
        return v.isBigInt() ? std::get<BigInt>(v.data) : BigInt(static_cast<int64_t>(std::round(v.asDouble())));
    };

    reg("factorial", { 1 }, [](const std::vector<Value>& args) -> Value { return Value(BigInt::factorial(static_cast<int64_t>(std::round(args[0].asDouble())))); });
    reg("fib", { 1 }, [](const std::vector<Value>& args) -> Value { return Value(BigInt::fibonacci(static_cast<int64_t>(std::round(args[0].asDouble())))); });
    reg("gcd", { 2 }, [toBigInt](const std::vector<Value>& args) -> Value { return Value(BigInt::gcd(toBigInt(args[0]), toBigInt(args[1]))); });
    reg("lcm", { 2 }, [toBigInt](const std::vector<Value>& args) -> Value { return Value(BigInt::lcm(toBigInt(args[0]), toBigInt(args[1]))); });
    reg("digits", { 1 }, [](const std::vector<Value>& args) -> Value { if (!args[0].isBigInt()) throw std::runtime_error("Type Error: expects BigInt."); return Value(static_cast<double>(std::get<BigInt>(args[0].data).digitCount())); });
    reg("isPrime", { 1 }, [toBigInt](const std::vector<Value>& args) -> Value { return Value(toBigInt(args[0]).isPrime() ? 1.0 : 0.0); });
    reg("nextPrime", { 1 }, [toBigInt](const std::vector<Value>& args) -> Value { return Value(toBigInt(args[0]).nextPrime()); });
    reg("nthPrime", { 1 }, [](const std::vector<Value>& args) -> Value { return Value(BigInt::nthPrime(static_cast<int64_t>(std::round(args[0].asDouble())))); });
    reg("primePi", { 1 }, [toBigInt](const std::vector<Value>& args) -> Value { return Value(BigInt(toBigInt(args[0]).primePi())); });
    reg("phi", { 1 }, [toBigInt](const std::vector<Value>& args) -> Value { return Value(toBigInt(args[0]).eulerPhi()); });
    reg("divisors", { 1 }, [toBigInt](const std::vector<Value>& args) -> Value { return Value(toBigInt(args[0]).divisorCount()); });
    reg("sigma", { 1, 2 }, [toBigInt](const std::vector<Value>& args) -> Value { int64_t k = (args.size()==2) ? static_cast<int64_t>(std::round(args[1].asDouble())) : 1; return Value(toBigInt(args[0]).divisorSum(k)); });
    reg("omega", { 1 }, [toBigInt](const std::vector<Value>& args) -> Value { return Value(BigInt(toBigInt(args[0]).omega())); });
    reg("bigOmega", { 1 }, [toBigInt](const std::vector<Value>& args) -> Value { return Value(BigInt(toBigInt(args[0]).bigOmega())); });
    reg("mobius", { 1 }, [toBigInt](const std::vector<Value>& args) -> Value { return Value(BigInt(toBigInt(args[0]).mobius())); });
    reg("isPerfect", { 1 }, [toBigInt](const std::vector<Value>& args) -> Value { return Value(toBigInt(args[0]).isPerfect() ? 1.0 : 0.0); });
    reg("mod", { 2 }, [toBigInt](const std::vector<Value>& args) -> Value {
        if (args[0].isBigInt() && args[1].isBigInt()) return Value(BigInt::mathMod(std::get<BigInt>(args[0].data), std::get<BigInt>(args[1].data)));
        if (std::holds_alternative<Fraction>(args[0].data)) { const auto& f = std::get<Fraction>(args[0].data); if (f.getDen() == BigInt(1)) return Value(BigInt::mathMod(f.getNum(), toBigInt(args[1]))); }
        double a = args[0].asDouble(), b = args[1].asDouble();
        if (jc::Tol::isEq(b, 0.0)) throw std::runtime_error("Math Error: Modulo by zero.");
        double r = std::fmod(a, b); if (r < 0) r += std::abs(b); return Value(r);
    });
    reg("modpow", { 3 }, [toBigInt](const std::vector<Value>& args) -> Value { return Value(BigInt::modPow(toBigInt(args[0]), toBigInt(args[1]), toBigInt(args[2]))); });
    reg("C", { 2 }, [](const std::vector<Value>& args) -> Value { int64_t n = static_cast<int64_t>(std::round(args[0].asDouble())), k = static_cast<int64_t>(std::round(args[1].asDouble())); if (n<0||k<0) throw std::runtime_error("Math Error: C(n,k) requires non-negative integers."); if (k>n) return Value(BigInt(0)); if (k>n-k) k = n-k; BigInt result(1); for (int64_t i = 0; i < k; ++i) { jc::checkInterrupt(); result = result*BigInt(n-i); result = result/BigInt(i+1); } return Value(result); });
    reg("A", { 2 }, [](const std::vector<Value>& args) -> Value { int64_t n = static_cast<int64_t>(std::round(args[0].asDouble())), k = static_cast<int64_t>(std::round(args[1].asDouble())); if (n<0||k<0) throw std::runtime_error("Math Error: A(n,k) requires non-negative integers."); if (k>n) return Value(BigInt(0)); BigInt result(1); for (int64_t i = 0; i < k; ++i) { jc::checkInterrupt(); result = result*BigInt(n-i); } return Value(result); });
    reg("catalan", { 1 }, [](const std::vector<Value>& args) -> Value { int64_t n = static_cast<int64_t>(std::round(args[0].asDouble())); if (n<0) throw std::runtime_error("Math Error: catalan(n) requires non-negative integer."); BigInt result(1); for (int64_t i = 0; i < n; ++i) { jc::checkInterrupt(); result = result*BigInt(2*n-i); result = result/BigInt(i+1); } result = result/BigInt(n+1); return Value(result); });
}

// =================================================================
// [10] 多进制与位运算
// =================================================================
void BuiltinRegistry::registerBase() {
    reg("base", { 2 }, [](const std::vector<Value>& args) -> Value { return Value(BaseNum(args[0].asBigInt(), static_cast<int>(std::round(args[1].asDouble())))); });
    reg("bnum", { 2 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: First arg must be a string."); return Value(BaseNum::fromString(std::get<std::string>(args[0].data), static_cast<int>(std::round(args[1].asDouble())))); });
    reg("changeBase", { 2 }, [](const std::vector<Value>& args) -> Value { return Value(BaseNum(args[0].asBigInt(), static_cast<int>(std::round(args[1].asDouble())))); });
    reg("data", { 1 }, [](const std::vector<Value>& args) -> Value { return Value(args[0].asBigInt()); });

    auto extractBin = [](const Value& v) -> BaseNum { if (std::holds_alternative<BaseNum>(v.data) && std::get<BaseNum>(v.data).getRadix() == 2) return std::get<BaseNum>(v.data); return BaseNum(v.asBigInt(), 2); };
    reg("bitand", { 2 }, [extractBin](const std::vector<Value>& args) -> Value { return Value(extractBin(args[0]).bitAnd(extractBin(args[1]))); });
    reg("bitor", { 2 }, [extractBin](const std::vector<Value>& args) -> Value { return Value(extractBin(args[0]).bitOr(extractBin(args[1]))); });
    reg("bitxor", { 2 }, [extractBin](const std::vector<Value>& args) -> Value { return Value(extractBin(args[0]).bitXor(extractBin(args[1]))); });
    reg("bitnot", { 1, 2 }, [extractBin](const std::vector<Value>& args) -> Value { if (args.size() == 1) return Value(extractBin(args[0]).bitNot()); int width = static_cast<int>(std::round(args[1].asDouble())); if (width <= 0) throw std::runtime_error("Math Error: Bit width must be positive."); return Value(extractBin(args[0]).bitNot(width)); });
    reg("bitshift", { 2 }, [extractBin](const std::vector<Value>& args) -> Value { int shift = static_cast<int>(std::round(args[1].asDouble())); return shift > 0 ? Value(extractBin(args[0]).shiftLeft(shift)) : Value(extractBin(args[0]).shiftRight(-shift)); });
}

// =================================================================
// [12] 统计（用静态 helper，无需 this）
// =================================================================
void BuiltinRegistry::registerStatistics() {
    reg("mean", { 1 }, [](const std::vector<Value>& args) -> Value { auto d = extractDS(args[0], "mean"); return Value(computeMean(d)); });
    reg("var", { 1 }, [](const std::vector<Value>& args) -> Value { auto d = extractDS(args[0], "var"); return Value(computeVar(d)); });
    reg("svar", { 1 }, [](const std::vector<Value>& args) -> Value { auto d = extractDS(args[0], "svar"); if (d.size()<2) throw std::runtime_error("Math Error: Sample variance requires at least 2 data points."); return Value(computeSvar(d)); });
    reg("std", { 1 }, [](const std::vector<Value>& args) -> Value { auto d = extractDS(args[0], "std"); return Value(computeStd(d)); });
    reg("sstd", { 1 }, [](const std::vector<Value>& args) -> Value { auto d = extractDS(args[0], "sstd"); if (d.size()<2) throw std::runtime_error("Math Error: Sample std requires at least 2 data points."); return Value(std::sqrt(computeSvar(d))); });
    reg("max", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<List>(args[0].data)) {
            const auto& L = std::get<List>(args[0].data);
            if (L.empty()) throw std::runtime_error("Math Error: Cannot compute max of empty list.");
            Value mx = anyToVal(L.raw()[0]);
            for (size_t i = 1; i < L.size(); ++i) {
                Value v = anyToVal(L.raw()[i]);
                // ★ 正确的 C++ 比较
                if (helpers::checkGreater(v, mx)) mx = v;
            }
            return mx;
        }
        auto d = extractDS(args[0], "max");
        double mx = d[0]; for (double v : d) if (v > mx) mx = v; return Value(mx);
        });

    reg("min", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<List>(args[0].data)) {
            const auto& L = std::get<List>(args[0].data);
            if (L.empty()) throw std::runtime_error("Math Error: Cannot compute min of empty list.");
            Value mn = anyToVal(L.raw()[0]);
            for (size_t i = 1; i < L.size(); ++i) {
                Value v = anyToVal(L.raw()[i]);
                // ★ 正确的 C++ 比较
                if (helpers::checkLess(v, mn)) mn = v;
            }
            return mn;
        }
        auto d = extractDS(args[0], "min");
        double mn = d[0]; for (double v : d) if (v < mn) mn = v; return Value(mn);
        });

    reg("span", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<List>(args[0].data)) {
            const auto& L = std::get<List>(args[0].data);
            if (L.empty()) throw std::runtime_error("Math Error: Cannot compute span of empty list.");
            Value mn = anyToVal(L.raw()[0]);
            Value mx = anyToVal(L.raw()[0]);
            for (size_t i = 1; i < L.size(); ++i) {
                Value v = anyToVal(L.raw()[i]);
                // ★ 正确的 C++ 比较
                if (helpers::checkGreater(v, mx)) mx = v;
                if (helpers::checkLess(v, mn)) mn = v;
            }
            // 利用 Value 的重载减法返回 span
            return mx - mn;
        }
        auto d = extractDS(args[0], "span");
        double mx = d[0], mn = d[0];
        for (double v : d) { if (v > mx) mx = v; if (v < mn) mn = v; }
        return Value(mx - mn);
        });

    reg("perc", { 2 }, [](const std::vector<Value>& args) -> Value {
        auto d = extractDS(args[0], "perc");
        if (d.empty()) throw std::runtime_error("Math Error: Cannot compute percentile of empty dataset.");
        double p = args[1].asDouble();
        if (p<0||p>100) throw std::runtime_error("Math Error: Percentile must be [0,100].");
        std::sort(d.begin(), d.end());
        int n = static_cast<int>(d.size());
        double pos = (p/100.0) * n;
        int kk = static_cast<int>(std::ceil(pos));
        if (kk <= 0) return Value(d[0]);
        if (kk >= n) return Value(d[n-1]);
        int m = kk - 1;
        double f = pos - kk;
        if (std::abs(f) > 1e-9) return Value(d[m]);
        if (kk < n) return Value((d[m] + d[kk]) / 2.0);
        return Value(d[m]);
    });
    reg("median", { 1 }, [this](const std::vector<Value>& args) -> Value { return builtins["perc"]({ args[0], Value(50.0) }); });

    reg("mode", { 1 }, [](const std::vector<Value>& args) -> Value {
        auto d = extractDS(args[0], "mode");
        if (d.empty()) throw std::runtime_error("Math Error: Cannot compute mode of empty dataset.");
        struct Bucket { double representative; int count; };
        std::vector<Bucket> buckets;
        for (double v : d) { bool found = false; for (auto& bkt : buckets) { if (jc::Tol::isEq(v, bkt.representative, 1e4)) { bkt.count++; found = true; break; } } if (!found) buckets.push_back({ v, 1 }); }
        int mx = 0; for (const auto& bkt : buckets) if (bkt.count > mx) mx = bkt.count;
        std::vector<double> modes; for (const auto& bkt : buckets) if (bkt.count == mx) modes.push_back(bkt.representative);
        std::sort(modes.begin(), modes.end());
        if (modes.size() == 1) return Value(modes[0]);
        return Value(RealMatrix(1, static_cast<int>(modes.size()), modes));
    });

    reg("cov", { 2 }, [](const std::vector<Value>& args) -> Value { auto X = extractDS(args[0], "cov"), Y = extractDS(args[1], "cov"); if (X.size()!=Y.size()) throw std::runtime_error("Math Error: Size mismatch."); return Value(computeCov(X, Y)); });
    reg("corr", { 2 }, [](const std::vector<Value>& args) -> Value { auto X = extractDS(args[0], "corr"), Y = extractDS(args[1], "corr"); if (X.size()!=Y.size()) throw std::runtime_error("Math Error: Size mismatch."); return Value(computeCorr(X, Y)); });
    reg("rsq", { 2 }, [](const std::vector<Value>& args) -> Value { auto X = extractDS(args[0], "rsq"), Y = extractDS(args[1], "rsq"); if (X.size()!=Y.size()) throw std::runtime_error("Math Error: Size mismatch."); double r = computeCorr(X, Y); return Value(r * r); });
    reg("regress", { 2 }, [](const std::vector<Value>& args) -> Value {
        auto X = extractDS(args[0], "regress"), Y = extractDS(args[1], "regress");
        if (X.size()!=Y.size()) throw std::runtime_error("Math Error: Size mismatch.");
        double vX = computeVar(X);
        if (jc::Tol::isEq(vX, 0.0)) throw std::runtime_error("Math Error: Zero variance in X.");
        double c = computeCov(X, Y);
        double b = c / vX, a = computeMean(Y) - b * computeMean(X);
        std::cout << "Linear Model: Y = " << a << " + " << b << " * X" << std::endl;
        std::cout << "Correlation r: " << computeCorr(X, Y) << std::endl;
        return Value(RealMatrix(1, 2, { a, b }));
    });
}

// =================================================================
// [13] 随机数
// =================================================================
void BuiltinRegistry::registerRandom() {
    reg("rand", { 0, 2 }, [](const std::vector<Value>& args) -> Value { static std::mt19937 gen(std::random_device{}()); if (args.size()==0) return Value(std::uniform_real_distribution<double>(0,1)(gen)); double lo = args[0].asDouble(), hi = args[1].asDouble(); return Value(std::uniform_real_distribution<double>(lo, hi)(gen)); });
    reg("randint", { 2 }, [](const std::vector<Value>& args) -> Value { static std::mt19937 gen(std::random_device{}()); return Value(static_cast<double>(std::uniform_int_distribution<int>(static_cast<int>(std::round(args[0].asDouble())), static_cast<int>(std::round(args[1].asDouble())))(gen))); });
    reg("randc", { 0, 2 }, [](const std::vector<Value>& args) -> Value { static std::mt19937 gen(std::random_device{}()); double lo=0,hi=1; if (args.size()==2){lo=args[0].asDouble();hi=args[1].asDouble();} std::uniform_real_distribution<double> dist(lo,hi); return Value(Complex(dist(gen),dist(gen))); });
    reg("randmat", { 2, 4 }, [](const std::vector<Value>& args) -> Value { static std::mt19937 gen(std::random_device{}()); int r,c; double lo=0,hi=1; if (args.size()==2){r=static_cast<int>(std::round(args[0].asDouble()));c=static_cast<int>(std::round(args[1].asDouble()));} else {r=static_cast<int>(std::round(args[0].asDouble()));c=static_cast<int>(std::round(args[1].asDouble()));lo=args[2].asDouble();hi=args[3].asDouble();} std::uniform_real_distribution<double> dist(lo,hi); std::vector<double> d(r*c); for (auto& v:d) v=dist(gen); return Value(RealMatrix(r,c,d)); });
    reg("randimat", { 2, 4 }, [](const std::vector<Value>& args) -> Value { static std::mt19937 gen(std::random_device{}()); int r,c,lo=0,hi=10; if (args.size()==2){r=static_cast<int>(std::round(args[0].asDouble()));c=static_cast<int>(std::round(args[1].asDouble()));} else {r=static_cast<int>(std::round(args[0].asDouble()));c=static_cast<int>(std::round(args[1].asDouble()));lo=static_cast<int>(std::round(args[2].asDouble()));hi=static_cast<int>(std::round(args[3].asDouble()));} std::uniform_int_distribution<int> dist(lo,hi); std::vector<double> d(r*c); for (auto& v:d) v=static_cast<double>(dist(gen)); return Value(RealMatrix(r,c,d)); });
    reg("randcmat", { 2, 4 }, [](const std::vector<Value>& args) -> Value { static std::mt19937 gen(std::random_device{}()); int r,c; double lo=0,hi=1; if (args.size()==2){r=static_cast<int>(std::round(args[0].asDouble()));c=static_cast<int>(std::round(args[1].asDouble()));} else {r=static_cast<int>(std::round(args[0].asDouble()));c=static_cast<int>(std::round(args[1].asDouble()));lo=args[2].asDouble();hi=args[3].asDouble();} std::uniform_real_distribution<double> dist(lo,hi); std::vector<Complex> d(r*c); for (auto& v:d) v=Complex(dist(gen),dist(gen)); return Value(ComplexMatrix(r,c,d)); });
    reg("magic", { 1 }, [](const std::vector<Value>& args) -> Value { return Value(RealMatrix::magic(static_cast<int>(std::round(args[0].asDouble())))); });
}

// =================================================================
// [14] 系统工具（无状态部分）
// =================================================================
void BuiltinRegistry::registerSystemUtils() {
    reg("buildIndex", { 0 }, [](const std::vector<Value>&) -> Value { BigInt::buildFileIndex(); return Value::none(); });
    builtins["loadPrimes"] = builtins["buildIndex"]; builtinArity["loadPrimes"] = builtinArity["buildIndex"];
    reg("mountPrimes", { 1 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Runtime Error: mountPrimes(\"path\") expects a string."); BigInt::setPrimeFilePath(std::get<std::string>(args[0].data)); return Value::none(); });
    reg("sysinfo", { 0 }, [](const std::vector<Value>&) -> Value { std::cout << "--- Junk Calculator System Info ---\n" << "Prime DB: " << (BigInt::getPrimeFilePath().empty() ? "(Dynamic Computation)" : BigInt::getPrimeFilePath()) << "\n" << "Indexed:  " << BigInt::totalPrimesInFile << " primes\n"; if (BigInt::totalPrimesInFile > 0) std::cout << "Max:      " << BigInt::largestPrimeInFile << "\n"; std::cout << "-----------------------------------" << std::endl; return Value::none(); });
    reg("gc", { 0, 1 }, [](const std::vector<Value>& args) -> Value {
        // 1. 清理符号表达式的弱引用池
        jc::SymExpr::cleanupPool();

        if (!VM::activeVM) return Value(0.0);

        // ★ gc(true) = 激进模式：先清掉 ANS 避免它充当隐形保护伞
        bool aggressive = (args.size() == 1 && helpers::isTruthy(args[0]));
        if (aggressive) {
            VM::activeVM->setGlobal("ANS", Value::none());
        }

        int freed = VM::activeVM->runGC();

        std::cout << "[GC] Collected " << freed << " unreachable object(s). "
            << "Tracked: " << GcHeap::get().trackedCount() << std::endl;
        return Value(static_cast<double>(freed));
        });

    reg("gcinfo", { 0 }, [](const std::vector<Value>&) -> Value {
        auto& heap = GcHeap::get();
        std::cout << "--- GC Status ---\n"
            << "  Tracked objects:     " << heap.trackedCount() << "\n"
            << "  Allocs since GC:     " << heap.allocsSinceGc() << "\n"
            << "  Next GC threshold:   " << heap.threshold() << "\n"
            << "-----------------" << std::endl;
        return Value::none();
        });

    reg("freeze", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<List>(args[0].data)) {
            List l = std::get<List>(args[0].data);
            l.freeze();
        } else if (std::holds_alternative<Dict>(args[0].data)) {
            Dict d = std::get<Dict>(args[0].data);
            d.freeze();
        } else if (std::holds_alternative<Set>(args[0].data)) {
            Set s = std::get<Set>(args[0].data);
            s.freeze();
        }
        return args[0];
        });

    reg("isFrozen", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<List>(args[0].data)) {
            return Value(std::get<List>(args[0].data).isFrozen() ? 1.0 : 0.0);
        } else if (std::holds_alternative<Dict>(args[0].data)) {
            return Value(std::get<Dict>(args[0].data).isFrozen() ? 1.0 : 0.0);
        } else if (std::holds_alternative<Set>(args[0].data)) {
            return Value(std::get<Set>(args[0].data).isFrozen() ? 1.0 : 0.0);
        }
        return Value(0.0);
        });

    reg("clone", { 1 }, [](const std::vector<Value>& args) -> Value {
        std::function<Value(const Value&)> deepCopy = [&](const Value& v) -> Value {
            if (std::holds_alternative<List>(v.data)) {
                List newList;
                for (const auto& e : std::get<List>(v.data).raw()) {
                    newList.push_back(deepCopy(e));
                }
                return Value(newList);
            }
            if (std::holds_alternative<Dict>(v.data)) {
                Dict newDict;
                for (const auto& [k, val] : std::get<Dict>(v.data).getEntries()) {
                    newDict.set(deepCopy(k), deepCopy(val));
                }
                return Value(newDict);
            }
            if (std::holds_alternative<Set>(v.data)) {
                Set newSet;
                for (const auto& [k, val] : std::get<Set>(v.data).raw()) {
                    newSet.insert(deepCopy(val));
                }
                return Value(newSet);
            }
            return v;
        };
        return deepCopy(args[0]);
        });

    reg("val", { 1 }, [](const std::vector<Value>& args) -> Value {
        std::function<Value(const Value&)> deepCopyAndFreeze = [&](const Value& v) -> Value {
            if (std::holds_alternative<List>(v.data)) {
                List newList;
                for (const auto& e : std::get<List>(v.data).raw()) {
                    newList.push_back(deepCopyAndFreeze(e));
                }
                newList.freeze();
                return Value(newList);
            }
            if (std::holds_alternative<Dict>(v.data)) {
                Dict newDict;
                for (const auto& [k, val] : std::get<Dict>(v.data).getEntries()) {
                    newDict.set(deepCopyAndFreeze(k), deepCopyAndFreeze(val));
                }
                newDict.freeze();
                return Value(newDict);
            }
            if (std::holds_alternative<Set>(v.data)) {
                Set newSet;
                for (const auto& [k, val] : std::get<Set>(v.data).raw()) {
                    newSet.insert(deepCopyAndFreeze(val));
                }
                newSet.freeze();
                return Value(newSet);
            }
            return v;
        };
        return deepCopyAndFreeze(args[0]);
        });

    reg("symconfig", { 0, 1 }, [](const std::vector<Value>& args) -> Value {
        if (args.empty()) {
            Dict d;
            d.set(Value("maxExpandTerms"), Value(static_cast<double>(SymConfig::maxExpandTerms)));
            d.set(Value("maxAstNodes"), Value(static_cast<double>(SymConfig::maxAstNodes)));
            d.set(Value("maxIterations"), Value(static_cast<double>(SymConfig::maxIterations)));
            d.set(Value("maxDepth"), Value(static_cast<double>(SymConfig::maxDepth)));
            d.set(Value("debugIntegration"), Value(SymConfig::debugIntegration ? 1.0 : 0.0));
            return Value(d);
        }
        if (std::holds_alternative<std::string>(args[0].data) && std::get<std::string>(args[0].data) == "default") {
            SymConfig::maxExpandTerms = 5000;
            SymConfig::maxAstNodes = 50000;
            SymConfig::maxIterations = 1000;
            SymConfig::maxDepth = 20;
            SymConfig::debugIntegration = false;
            return Value::none();
        }
        if (!std::holds_alternative<Dict>(args[0].data)) {
            throw std::runtime_error("Type Error: symconfig() expects a Dict or \"default\".");
        }
        Dict d = std::get<Dict>(args[0].data);
        if (d.has(Value("maxExpandTerms"))) SymConfig::maxExpandTerms = static_cast<int64_t>((*d.get(Value("maxExpandTerms"))).asDouble());
        if (d.has(Value("maxAstNodes"))) SymConfig::maxAstNodes = static_cast<int>((*d.get(Value("maxAstNodes"))).asDouble());
        if (d.has(Value("maxIterations"))) SymConfig::maxIterations = static_cast<int>((*d.get(Value("maxIterations"))).asDouble());
        if (d.has(Value("maxDepth"))) SymConfig::maxDepth = static_cast<int>((*d.get(Value("maxDepth"))).asDouble());
        if (d.has(Value("debugIntegration"))) SymConfig::debugIntegration = isTruthy(*d.get(Value("debugIntegration")));
        return Value::none();
        });

    reg("setSymLimit", { 1, 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("Type Error: setSymLimit() expects a string key.");
        std::string key = std::get<std::string>(args[0].data);
        
        if (args.size() == 1) {
            if (key == "default") {
                SymConfig::maxExpandTerms = 5000;
                SymConfig::maxAstNodes = 50000;
                SymConfig::maxIterations = 1000;
                SymConfig::maxDepth = 20;
                SymConfig::debugIntegration = false;
                return Value::none();
            }
            throw std::runtime_error("Runtime Error: setSymLimit() expects 2 arguments unless resetting with \"default\".");
        }

        if (std::holds_alternative<std::string>(args[1].data) && std::get<std::string>(args[1].data) == "default") {
            if (key == "maxExpandTerms") SymConfig::maxExpandTerms = 5000;
            else if (key == "maxAstNodes") SymConfig::maxAstNodes = 50000;
            else if (key == "maxIterations") SymConfig::maxIterations = 1000;
            else if (key == "maxDepth") SymConfig::maxDepth = 20;
            else if (key == "debugIntegration") SymConfig::debugIntegration = false;
            else throw std::runtime_error("Runtime Error: Unknown SymConfig key '" + key + "'.");
            return Value::none();
        }

        if (key == "debugIntegration") {
            SymConfig::debugIntegration = isTruthy(args[1]);
            return Value::none();
        }

        double val = args[1].asDouble();
        if (key == "maxExpandTerms") SymConfig::maxExpandTerms = static_cast<int64_t>(val);
        else if (key == "maxAstNodes") SymConfig::maxAstNodes = static_cast<int>(val);
        else if (key == "maxIterations") SymConfig::maxIterations = static_cast<int>(val);
        else if (key == "maxDepth") SymConfig::maxDepth = static_cast<int>(val);
        else throw std::runtime_error("Runtime Error: Unknown SymConfig key '" + key + "'.");
        return Value::none();
        });

    reg("__register_help", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw std::runtime_error("System Error: __register_help expects two strings.");
        }
        std::string topic = std::get<std::string>(args[0].data);
        std::string text = std::get<std::string>(args[1].data);
        jc::DynamicHelp[topic] = text; // 存入 C++ 内存池
        return Value::none();
        });
    // ★ 暴露给用户的原生 help() 内置函数
    reg("help", { 0, 1 }, [](const std::vector<Value>& args) -> Value {
        if (args.empty()) {
            auto it = jc::BuiltinHelp.find("main");
            if (it != jc::BuiltinHelp.end()) std::cout << "\n" << it->second << std::endl;
            return Value::none();
        }

        if (!std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("Type Error: help() expects a string topic.");

        std::string topic = std::get<std::string>(args[0].data);

        // 1. 优先查找脚本注册进来的动态函数库
        auto itDyn = jc::DynamicHelp.find(topic);
        if (itDyn != jc::DynamicHelp.end()) {
            std::cout << "\n" << itDyn->second << std::endl;
            return Value::none();
        }

        // 2. 其次查找 C++ 内置的文档 (支持忽略大小写)
        std::string key = topic;
        std::transform(key.begin(), key.end(), key.begin(),
            [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); });

        auto itBuiltin = jc::BuiltinHelp.find(key);
        if (itBuiltin != jc::BuiltinHelp.end()) {
            std::cout << "\n" << itBuiltin->second << std::endl;
            return Value::none();
        }

        std::cout << "\n  [System] No help found for topic: '" << topic << "'\n";
        return Value::none();
        });
}

// =================================================================
// [15] 控制流辅助（无状态部分）
// =================================================================
void BuiltinRegistry::registerControlFlow() {
    reg("print", {}, [](const std::vector<Value>& args) -> Value {
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) std::cout << " ";
            // ★ Dunder 钩子: __str__
            if (std::holds_alternative<std::shared_ptr<Instance>>(args[i].data)) {
                auto inst = std::get<std::shared_ptr<Instance>>(args[i].data);
                auto [found, result] = tryCallDunder(inst, "__str__");
                if (found) { std::cout << result; continue; }
            }
            std::cout << args[i];
        }
        std::cout << std::endl; return Value::none();
        });
    builtins["println"] = builtins["print"]; builtinArity["println"] = builtinArity["print"];
    reg("bool", { 1 }, [](const std::vector<Value>& args) -> Value {
        // ★ Dunder 钩子: __bool__
        if (std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) {
            auto inst = std::get<std::shared_ptr<Instance>>(args[0].data);
            auto [found, result] = tryCallDunder(inst, "__bool__");
            if (found) return Value(isTruthy(result) ? 1.0 : 0.0);
        }
        return Value(isTruthy(args[0]) ? 1.0 : 0.0);
        });
    reg("not", { 1 }, [](const std::vector<Value>& args) -> Value { return Value(isTruthy(args[0]) ? 0.0 : 1.0); });
    reg("and", { 2 }, [](const std::vector<Value>& args) -> Value { return Value((isTruthy(args[0]) && isTruthy(args[1])) ? 1.0 : 0.0); });
    reg("or", { 2 }, [](const std::vector<Value>& args) -> Value { return Value((isTruthy(args[0]) || isTruthy(args[1])) ? 1.0 : 0.0); });

    reg("seq", { 2, 3 }, [](const std::vector<Value>& args) -> Value {
        double start, step, end;
        if (args.size()==2) { start=args[0].asDouble(); end=args[1].asDouble(); step=(start<=end)?1.0:-1.0; }
        else { start=args[0].asDouble(); step=args[1].asDouble(); end=args[2].asDouble(); }
        if (Tol::isEq(step, 0.0)) throw std::runtime_error("Math Error: Step cannot be zero.");
        std::vector<double> vals;
        if (step>0) { for (double v=start; v<=end+Tol::EPS*100; v+=step) { jc::checkInterrupt(); vals.push_back(v); } }
        else { for (double v=start; v>=end-Tol::EPS*100; v+=step) { jc::checkInterrupt(); vals.push_back(v); } }
        if (vals.empty()) throw std::runtime_error("Math Error: seq() produced empty sequence.");
        return Value(RealMatrix(static_cast<int>(vals.size()), 1, vals));
    });

    reg("error", { 1 }, [](const std::vector<Value>& args) -> Value {
        std::string msg;
        if (std::holds_alternative<std::string>(args[0].data)) msg = std::get<std::string>(args[0].data);
        else { std::ostringstream oss; oss << args[0]; msg = oss.str(); }
        throw ErrorSignal{ msg };
    });
    reg("input", { 0, 1 }, [](const std::vector<Value>& args) -> Value {
        if (args.size()==1) { if (std::holds_alternative<std::string>(args[0].data)) std::cout << std::get<std::string>(args[0].data); else std::cout << args[0]; std::cout << std::flush; }
        std::string line;
        if (!std::getline(std::cin, line)) throw std::runtime_error("IO Error: Failed to read input.");
        return Value(line);
    });
    reg("clock", { 0 }, [](const std::vector<Value>&) -> Value {
        auto now = std::chrono::high_resolution_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        return Value(static_cast<double>(ms) / 1e6);
    });
    reg("sleep", { 1 }, [](const std::vector<Value>& args) -> Value {
        int ms = static_cast<int>(std::round(args[0].asDouble() * 1000));
        if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return Value::none();
    });
    reg("highlight", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: highlight() expects a string.");
        return Value(jc::highlightCode(std::get<std::string>(args[0].data)));
    });
    reg("color", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: color() expects \"on\" or \"off\".");
        std::string arg = std::get<std::string>(args[0].data);
        if (arg=="on") jc::colorsEnabled = true; else if (arg=="off") jc::colorsEnabled = false;
        else throw std::runtime_error("Runtime Error: color() expects \"on\" or \"off\".");
        return Value::none();
    });
    reg("debugInteg", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: debugInteg() expects \"on\" or \"off\".");
        std::string arg = std::get<std::string>(args[0].data);
        if (arg=="on") jc::SymConfig::debugIntegration = true; else if (arg=="off") jc::SymConfig::debugIntegration = false;
        else throw std::runtime_error("Runtime Error: debugInteg() expects \"on\" or \"off\".");
        return Value::none();
    });

    // =================================================================
    // [大一统泛型 API] add / remove / discard / clear
    // =================================================================

    reg("add", { 2, 3 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<Set>(args[0].data)) {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: add() on Set takes 2 args (set, val).");
            Set s = std::get<Set>(args[0].data);
            s.insert(args[1]);
            return Value(s);
        }
        else if (std::holds_alternative<List>(args[0].data)) {
            if (args.size() != 2) throw std::runtime_error("Runtime Error: add() on List takes 2 args (list, val).");
            List l = std::get<List>(args[0].data);
            l.push_back(valToAny(args[1]));
            return Value(l);
        }
        else if (std::holds_alternative<Dict>(args[0].data) || std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) {
            if (args.size() != 3) throw std::runtime_error("Runtime Error: add() on Dict/Instance takes 3 args (obj, key, val).");
            Dict d = helpers::getDictMap(args[0], "add");
            d.set(args[1], args[2]);
            return args[0]; // 返回原对象
        }
        throw std::runtime_error("Type Error: add() expects a Set, List, Dict, or Instance.");
        });

    reg("remove", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<Set>(args[0].data)) {
            Set s = std::get<Set>(args[0].data);
            if (!s.erase(args[1])) throw std::runtime_error("Runtime Error: Element not found in Set.");
            return Value(s);
        }
        else if (std::holds_alternative<List>(args[0].data)) {
            List l = std::get<List>(args[0].data);
            int idx = static_cast<int>(std::round(args[1].asDouble()));
            l.removeAt(idx);
            return Value(l);
        }
        else if (std::holds_alternative<Dict>(args[0].data) || std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) {
            Dict d = helpers::getDictMap(args[0], "remove");
            if (!d.remove(args[1])) throw std::runtime_error("Runtime Error: Key not found.");
            return args[0];
        }
        throw std::runtime_error("Type Error: remove() expects a Set, List, Dict, or Instance.");
        });

    reg("discard", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<Set>(args[0].data)) {
            Set s = std::get<Set>(args[0].data);
            s.erase(args[1]);
            return Value(s);
        }
        else if (std::holds_alternative<Dict>(args[0].data) || std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) {
            Dict d = helpers::getDictMap(args[0], "discard");
            d.remove(args[1]); // 静默处理
            return args[0];
        }
        throw std::runtime_error("Type Error: discard() expects a Set, Dict, or Instance.");
        });

    reg("clear", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<Set>(args[0].data)) {
            Set s = std::get<Set>(args[0].data); s.clear(); return Value(s);
        }
        else if (std::holds_alternative<List>(args[0].data)) {
            List l = std::get<List>(args[0].data); l.clear(); return Value(l);
        }
        else if (std::holds_alternative<Dict>(args[0].data) || std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) {
            Dict d = helpers::getDictMap(args[0], "clear");
            d.clear();
            return args[0];
        }
        throw std::runtime_error("Type Error: clear() expects a Set, List, Dict, or Instance.");
        });
}

// =================================================================
// [16] 字符串
// =================================================================
void BuiltinRegistry::registerStringFunctions() {
    reg("str", { 1 }, [](const std::vector<Value>& args) -> Value {
        // ★ Dunder 钩子: __str__
        if (std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) {
            auto inst = std::get<std::shared_ptr<Instance>>(args[0].data);
            auto [found, result] = tryCallDunder(inst, "__str__");
            if (found) return result;
        }
        if (std::holds_alternative<std::string>(args[0].data)) return args[0];
        std::ostringstream oss; oss << args[0]; return Value(oss.str());
        });
    reg("len", { 1 }, [](const std::vector<Value>& args) -> Value {
        // ★ Dunder 钩子: __len__
        if (std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) {
            auto inst = std::get<std::shared_ptr<Instance>>(args[0].data);
            auto [found, result] = tryCallDunder(inst, "__len__");
            if (found) return result;
            return Value(static_cast<double>(inst->fields.size()));
        }
        if (std::holds_alternative<std::string>(args[0].data)) return Value(static_cast<double>(std::get<std::string>(args[0].data).size()));
        if (std::holds_alternative<RealMatrix>(args[0].data)) { const auto& m = std::get<RealMatrix>(args[0].data); if (m.getCols() == 1) return Value(static_cast<double>(m.getRows())); if (m.getRows() == 1) return Value(static_cast<double>(m.getCols())); return Value(static_cast<double>(m.getRows() * m.getCols())); }
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) { const auto& m = std::get<ComplexMatrix>(args[0].data); if (m.getCols() == 1) return Value(static_cast<double>(m.getRows())); if (m.getRows() == 1) return Value(static_cast<double>(m.getCols())); return Value(static_cast<double>(m.getRows() * m.getCols())); }
        if (std::holds_alternative<StringMatrix>(args[0].data)) return Value(static_cast<double>(std::get<StringMatrix>(args[0].data).getRows() * std::get<StringMatrix>(args[0].data).getCols()));
        if (std::holds_alternative<Dict>(args[0].data)) return Value(static_cast<double>(std::get<Dict>(args[0].data).size()));
        if (std::holds_alternative<List>(args[0].data)) return Value(static_cast<double>(std::get<List>(args[0].data).size()));
        if (std::holds_alternative<Set>(args[0].data)) return Value(static_cast<double>(std::get<Set>(args[0].data).size()));
        throw std::runtime_error("Type Error: len() expects a string, vector, matrix, dict, or list.");
        });
    reg("eval", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("Type Error: eval() expects a string.");
        if (!evalCallback)
            throw std::runtime_error("Runtime Error: eval() not available in this context.");
        return evalCallback(std::get<std::string>(args[0].data));
        });

    reg("substr", { 2, 3 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: substr() expects a string."); const std::string& s = std::get<std::string>(args[0].data); int n=static_cast<int>(s.size()); int start=static_cast<int>(std::round(args[1].asDouble())); if (start<0) start=n+start; if (start<0||start>n) throw std::runtime_error("Runtime Error: substr() start index out of range."); if (args.size()==2) return Value(s.substr(start)); int length=static_cast<int>(std::round(args[2].asDouble())); if (length<0) throw std::runtime_error("Runtime Error: substr() length must be non-negative."); return Value(s.substr(start, length)); });
    reg("charAt", { 2 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: charAt() expects a string."); const std::string& s = std::get<std::string>(args[0].data); int n=static_cast<int>(s.size()); int idx=static_cast<int>(std::round(args[1].asDouble())); if (idx<0) idx=n+idx; if (idx<0||idx>=n) throw std::runtime_error("Runtime Error: charAt() index out of range."); return Value(std::string(1, s[idx])); });
    reg("upper", { 1 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: upper() expects a string."); std::string s = std::get<std::string>(args[0].data); std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) -> char { return static_cast<char>(std::toupper(c)); }); return Value(s); });
    reg("lower", { 1 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: lower() expects a string."); std::string s = std::get<std::string>(args[0].data); std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) -> char { return static_cast<char>(std::tolower(c)); }); return Value(s); });
    reg("trim", { 1 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: trim() expects a string."); std::string s = std::get<std::string>(args[0].data); size_t a=s.find_first_not_of(" \t\r\n"); size_t b=s.find_last_not_of(" \t\r\n"); if (a==std::string::npos) return Value(std::string("")); return Value(s.substr(a, b-a+1)); });
    reg("find", { 2, 3 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)||!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("Type Error: find() expects two strings."); const std::string& s=std::get<std::string>(args[0].data); const std::string& sub=std::get<std::string>(args[1].data); size_t start=0; if (args.size()==3) start=static_cast<size_t>(std::round(args[2].asDouble())); size_t pos=s.find(sub, start); return Value(pos==std::string::npos?-1.0:static_cast<double>(pos)); });
    reg("contains", { 2 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)||!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("Type Error: contains() expects two strings."); return Value(std::get<std::string>(args[0].data).find(std::get<std::string>(args[1].data))!=std::string::npos?1.0:0.0); });
    reg("replace", { 3 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)||!std::holds_alternative<std::string>(args[1].data)||!std::holds_alternative<std::string>(args[2].data)) throw std::runtime_error("Type Error: replace() expects three strings."); std::string s=std::get<std::string>(args[0].data); const std::string& from=std::get<std::string>(args[1].data); const std::string& to=std::get<std::string>(args[2].data); if (from.empty()) return Value(s); size_t pos=0; while ((pos=s.find(from, pos))!=std::string::npos) { s.replace(pos, from.size(), to); pos+=to.size(); } return Value(s); });
    reg("repeat", { 2 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: repeat() expects a string."); const std::string& s = std::get<std::string>(args[0].data); int n=static_cast<int>(std::round(args[1].asDouble())); if (n<0) throw std::runtime_error("Runtime Error: repeat() count must be non-negative."); std::string result; result.reserve(s.size()*n); for (int i=0;i<n;++i) result+=s; return Value(result); });
    reg("concat", {}, [](const std::vector<Value>& args) -> Value { std::ostringstream oss; for (const auto& a : args) oss << a; return Value(oss.str()); });
    reg("startsWith", { 2 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)||!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("Type Error: startsWith() expects two strings."); const std::string& s=std::get<std::string>(args[0].data); const std::string& prefix=std::get<std::string>(args[1].data); return Value(s.size()>=prefix.size()&&s.compare(0,prefix.size(),prefix)==0?1.0:0.0); });
    reg("endsWith", { 2 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)||!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("Type Error: endsWith() expects two strings."); const std::string& s=std::get<std::string>(args[0].data); const std::string& suffix=std::get<std::string>(args[1].data); return Value(s.size()>=suffix.size()&&s.compare(s.size()-suffix.size(),suffix.size(),suffix)==0?1.0:0.0); });
    reg("split", { 2 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)||!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("Type Error: split() expects two strings."); const std::string& s=std::get<std::string>(args[0].data); const std::string& delim=std::get<std::string>(args[1].data); if (delim.empty()) throw std::runtime_error("Runtime Error: split() delimiter cannot be empty."); List result; size_t start=0,pos; while ((pos=s.find(delim,start))!=std::string::npos) { result.push_back(valToAny(Value(s.substr(start,pos-start)))); start=pos+delim.size(); } result.push_back(valToAny(Value(s.substr(start)))); return Value(result); });
    reg("ord", { 1 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: ord() expects a string."); const std::string& s=std::get<std::string>(args[0].data); if (s.empty()) throw std::runtime_error("Runtime Error: ord() requires a non-empty string."); return Value(static_cast<double>(static_cast<unsigned char>(s[0]))); });
    reg("chr", { 1 }, [](const std::vector<Value>& args) -> Value { int code=static_cast<int>(std::round(args[0].asDouble())); if (code<0||code>127) throw std::runtime_error("Runtime Error: chr() code must be 0–127."); return Value(std::string(1, static_cast<char>(code))); });
    reg("parseNum", { 1 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: parseNum() expects a string."); const std::string& s=std::get<std::string>(args[0].data); size_t a=s.find_first_not_of(" \t\r\n"); if (a==std::string::npos) throw std::runtime_error("Math Error: Cannot parse empty string as number."); size_t b=s.find_last_not_of(" \t\r\n"); std::string trimmed=s.substr(a,b-a+1); try { if (trimmed.find('.')!=std::string::npos||trimmed.find('e')!=std::string::npos||trimmed.find('E')!=std::string::npos) return Value(std::stod(trimmed)); return Value(BigInt(trimmed)); } catch (...) { throw std::runtime_error("Math Error: Cannot parse '"+trimmed+"' as a number."); } });
}

// =================================================================
// [17] 数组引擎
// =================================================================
void BuiltinRegistry::registerArrayFunctions() {
    auto expectContainer = [](const std::string& name) -> Value {
        throw std::runtime_error("Type Error: " + name + "() expects a List or a Matrix (Real/Complex/String).");
        };

    reg("first", { 1 }, [expectContainer](const std::vector<Value>& args) -> Value {
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                if (arg.empty()) throw std::runtime_error("Runtime Error: first() on empty list.");
                return anyToVal(arg.raw()[0]);
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                if (arg.getRows() * arg.getCols() == 0) throw std::runtime_error("Runtime Error: first() on empty vector.");
                return Value(arg.rawData()[0]);
            }
            return expectContainer("first");
            }, args[0].data);
        });

    reg("last", { 1 }, [expectContainer](const std::vector<Value>& args) -> Value {
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                if (arg.empty()) throw std::runtime_error("Runtime Error: last() on empty list.");
                return anyToVal(arg.raw().back());
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                int n = arg.getRows() * arg.getCols();
                if (n == 0) throw std::runtime_error("Runtime Error: last() on empty vector.");
                return Value(arg.rawData()[n - 1]);
            }
            return expectContainer("last");
            }, args[0].data);
        });

    reg("pop", { 1 }, [expectContainer](const std::vector<Value>& args) -> Value {
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                // ★ List: TRUE destructive O(1) pop — mutates original via shared_ptr
                List L = arg; // shallow copy: same underlying data
                if (L.empty()) throw std::runtime_error("Runtime Error: pop() on empty list.");
                Value val = anyToVal(L.raw().back());
                L.raw().pop_back();
                return val;
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                // Matrix (value semantics): non-destructive, returns last element
                int n = arg.getRows() * arg.getCols();
                if (n == 0) throw std::runtime_error("Runtime Error: pop() on empty vector.");
                return Value(arg.rawData()[n - 1]);
            }
            return expectContainer("pop");
            }, args[0].data);
        });

    reg("shift", { 1 }, [expectContainer](const std::vector<Value>& args) -> Value {
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                // ★ List: destructive O(n) shift — mutates original via shared_ptr
                List L = arg;
                if (L.empty()) throw std::runtime_error("Runtime Error: shift() on empty list.");
                Value val = anyToVal(L.raw().front());
                L.raw().erase(L.raw().begin());
                return val;
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                int n = arg.getRows() * arg.getCols();
                if (n == 0) throw std::runtime_error("Runtime Error: shift() on empty vector.");
                return Value(arg.rawData()[0]);
            }
            return expectContainer("shift");
            }, args[0].data);
        });

    reg("push", { 2 }, [expectContainer](const std::vector<Value>& args) -> Value {
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                List L = arg; L.push_back(valToAny(args[1])); return Value(L);
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                auto v = arg.rawData();
                using Elem = std::decay_t<decltype(v[0])>;
                if constexpr (std::is_same_v<Elem, double>) v.push_back(args[1].asDouble());
                else if constexpr (std::is_same_v<Elem, Complex>) v.push_back(args[1].asComplex());
                else { std::ostringstream oss; oss << args[1]; v.push_back(std::holds_alternative<std::string>(args[1].data) ? std::get<std::string>(args[1].data) : oss.str()); }
                int r = (arg.getCols() == 1 && arg.getRows() > 1) ? static_cast<int>(v.size()) : 1;
                int c = (arg.getCols() == 1 && arg.getRows() > 1) ? 1 : static_cast<int>(v.size());
                return Value(T(r, c, v));
            }
            return expectContainer("push");
            }, args[0].data);
        });

    reg("prepend", { 2 }, [expectContainer](const std::vector<Value>& args) -> Value {
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                List L = arg; L.insert(0, valToAny(args[1])); return Value(L);
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                auto v = arg.rawData();
                using Elem = std::decay_t<decltype(v[0])>;
                if constexpr (std::is_same_v<Elem, double>) v.insert(v.begin(), args[1].asDouble());
                else if constexpr (std::is_same_v<Elem, Complex>) v.insert(v.begin(), args[1].asComplex());
                else { std::ostringstream oss; oss << args[1]; v.insert(v.begin(), std::holds_alternative<std::string>(args[1].data) ? std::get<std::string>(args[1].data) : oss.str()); }
                int r = (arg.getCols() == 1 && arg.getRows() > 1) ? static_cast<int>(v.size()) : 1;
                int c = (arg.getCols() == 1 && arg.getRows() > 1) ? 1 : static_cast<int>(v.size());
                return Value(T(r, c, v));
            }
            return expectContainer("prepend");
            }, args[0].data);
        });

    reg("insert", { 3 }, [expectContainer](const std::vector<Value>& args) -> Value {
        int idx = static_cast<int>(std::round(args[1].asDouble()));
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                List L = arg; L.insert(idx, valToAny(args[2])); return Value(L);
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                auto v = arg.rawData();
                int i = idx < 0 ? static_cast<int>(v.size()) + idx : idx;
                if (i < 0 || i > static_cast<int>(v.size())) throw std::runtime_error("Runtime Error: insert() index out of range.");
                using Elem = std::decay_t<decltype(v[0])>;
                if constexpr (std::is_same_v<Elem, double>) v.insert(v.begin() + i, args[2].asDouble());
                else if constexpr (std::is_same_v<Elem, Complex>) v.insert(v.begin() + i, args[2].asComplex());
                else { std::ostringstream oss; oss << args[2]; v.insert(v.begin() + i, std::holds_alternative<std::string>(args[2].data) ? std::get<std::string>(args[2].data) : oss.str()); }
                int r = (arg.getCols() == 1 && arg.getRows() > 1) ? static_cast<int>(v.size()) : 1;
                int c = (arg.getCols() == 1 && arg.getRows() > 1) ? 1 : static_cast<int>(v.size());
                return Value(T(r, c, v));
            }
            return expectContainer("insert");
            }, args[0].data);
        });

    reg("removeAt", { 2 }, [expectContainer](const std::vector<Value>& args) -> Value {
        int idx = static_cast<int>(std::round(args[1].asDouble()));
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                List L = arg; L.removeAt(idx); return Value(L);
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                auto v = arg.rawData();
                if (v.empty()) throw std::runtime_error("Runtime Error: removeAt() on empty vector.");
                int i = idx < 0 ? static_cast<int>(v.size()) + idx : idx;
                if (i < 0 || i >= static_cast<int>(v.size())) throw std::runtime_error("Runtime Error: removeAt() index out of range.");
                v.erase(v.begin() + i);
                int r = (arg.getCols() == 1 && arg.getRows() > 1) ? static_cast<int>(v.size()) : 1;
                int c = (arg.getCols() == 1 && arg.getRows() > 1) ? 1 : static_cast<int>(v.size());
                if (r * c == 0) return Value(T(1, 0));
                return Value(T(r, c, v));
            }
            return expectContainer("removeAt");
            }, args[0].data);
        });

    reg("slice", { 2, 3 }, [expectContainer](const std::vector<Value>& args) -> Value {
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List> || std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                auto getLen = [](const auto& v) { if constexpr (std::is_same_v<T, List>) return v.size(); else return v.rawData().size(); };
                int n = static_cast<int>(getLen(arg));
                int start = static_cast<int>(std::round(args[1].asDouble()));
                if (start < 0) start = n + start;
                start = std::max(0, std::min(start, n));
                int end = n;
                if (args.size() == 3) {
                    end = static_cast<int>(std::round(args[2].asDouble()));
                    if (end < 0) end = n + end;
                    end = std::max(0, std::min(end, n));
                }
                if constexpr (std::is_same_v<T, List>) {
                    List result; for (int i = start; i < end; ++i) result.push_back(arg.raw()[i]); return Value(result);
                }
                else {
                    auto v = arg.rawData();
                    std::vector<std::decay_t<decltype(v[0])>> retV;
                    if (start < end) retV.assign(v.begin() + start, v.begin() + end);
                    int cr = (arg.getCols() == 1 && arg.getRows() > 1) ? static_cast<int>(retV.size()) : 1;
                    int cc = (arg.getCols() == 1 && arg.getRows() > 1) ? 1 : static_cast<int>(retV.size());
                    if (cr * cc == 0) return Value(T(1, 0));
                    return Value(T(cr, cc, retV));
                }
            }
            return expectContainer("slice");
            }, args[0].data);
        });

    reg("reverse", { 1 }, [expectContainer](const std::vector<Value>& args) -> Value {
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) {
                std::string s = arg; std::reverse(s.begin(), s.end()); return Value(s);
            }
            else if constexpr (std::is_same_v<T, List>) {
                List L = arg; std::reverse(L.raw().begin(), L.raw().end()); return Value(L);
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                auto v = arg.rawData(); std::reverse(v.begin(), v.end());
                return Value(T(arg.getRows(), arg.getCols(), v));
            }
            return expectContainer("reverse");
            }, args[0].data);
        });

    reg("flatten", { 1 }, [expectContainer](const std::vector<Value>& args) -> Value {
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                List result;
                std::function<void(const List&)> flattenList = [&](const List& L) {
                    for (const auto& e : L.raw()) {
                        Value elem = anyToVal(e);
                        if (std::holds_alternative<List>(elem.data)) flattenList(std::get<List>(elem.data));
                        else result.push_back(e);
                    }
                    };
                flattenList(arg);
                return Value(result);
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                int n = arg.getRows() * arg.getCols();
                return Value(T(1, n, arg.rawData()));
            }
            return expectContainer("flatten");
            }, args[0].data);
        });

    reg("unique", { 1 }, [expectContainer](const std::vector<Value>& args) -> Value {
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                List result;
                for (const auto& e : arg.raw()) {
                    Value elem = anyToVal(e); bool found = false;
                    for (const auto& r : result.raw()) {
                        Value rv = anyToVal(r);
                        if (elem.data.index() == rv.data.index()) {
                            std::ostringstream a, b; a << elem; b << rv;
                            if (a.str() == b.str()) { found = true; break; }
                        }
                    }
                    if (!found) result.push_back(e);
                }
                return Value(result);
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                auto v = arg.rawData();
                using Elem = std::decay_t<decltype(v[0])>;
                std::vector<Elem> result;
                for (const auto& x : v) {
                    bool found = false;
                    for (const auto& y : result) {
                        if constexpr (std::is_same_v<Elem, double>) { if (Tol::isEq(x, y, 1e4)) { found = true; break; } }
                        else { if (x == y) { found = true; break; } }
                    }
                    if (!found) result.push_back(x);
                }
                int cr = (arg.getCols() == 1 && arg.getRows() > 1) ? static_cast<int>(result.size()) : 1;
                int cc = (arg.getCols() == 1 && arg.getRows() > 1) ? 1 : static_cast<int>(result.size());
                if (cr * cc == 0) return Value(T(1, 0));
                return Value(T(cr, cc, result));
            }
            return expectContainer("unique");
            }, args[0].data);
        });

    reg("indexOf", { 2 }, [expectContainer](const std::vector<Value>& args) -> Value {
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                for (size_t i = 0; i < arg.size(); ++i) {
                    std::ostringstream a, b; a << anyToVal(arg.raw()[i]); b << args[1];
                    if (a.str() == b.str()) return Value(static_cast<double>(i));
                }
                return Value(-1.0);
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                auto v = arg.rawData();
                using Elem = std::decay_t<decltype(v[0])>;
                Elem target{};
                try {
                    if constexpr (std::is_same_v<Elem, double>) target = args[1].asDouble();
                    else if constexpr (std::is_same_v<Elem, Complex>) target = args[1].asComplex();
                    else target = std::holds_alternative<std::string>(args[1].data) ? std::get<std::string>(args[1].data) : args[1].toString();
                }
                catch (...) { return Value(-1.0); }

                for (size_t i = 0; i < v.size(); ++i) {
                    if constexpr (std::is_same_v<Elem, double>) { if (Tol::isEq(v[i], target, 1e4)) return Value(static_cast<double>(i)); }
                    else { if (v[i] == target) return Value(static_cast<double>(i)); }
                }
                return Value(-1.0);
            }
            return expectContainer("indexOf");
            }, args[0].data);
        });

    reg("count", { 2 }, [expectContainer](const std::vector<Value>& args) -> Value {
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                int c = 0; std::ostringstream bs; bs << args[1]; std::string bstr = bs.str();
                for (const auto& e : arg.raw()) { std::ostringstream as; as << anyToVal(e); if (as.str() == bstr) c++; }
                return Value(static_cast<double>(c));
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                auto v = arg.rawData();
                using Elem = std::decay_t<decltype(v[0])>;
                Elem target{}; int c = 0;
                try {
                    if constexpr (std::is_same_v<Elem, double>) target = args[1].asDouble();
                    else if constexpr (std::is_same_v<Elem, Complex>) target = args[1].asComplex();
                    else target = std::holds_alternative<std::string>(args[1].data) ? std::get<std::string>(args[1].data) : args[1].toString();
                }
                catch (...) { return Value(0.0); }
                for (const auto& x : v) {
                    if constexpr (std::is_same_v<Elem, double>) { if (Tol::isEq(x, target, 1e4)) c++; }
                    else { if (x == target) c++; }
                }
                return Value(static_cast<double>(c));
            }
            return expectContainer("count");
            }, args[0].data);
        });

    reg("join", { 2 }, [expectContainer](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("Type Error: delimiter must be a string.");
        const std::string& delim = std::get<std::string>(args[1].data);
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                std::ostringstream oss;
                for (size_t i = 0; i < arg.size(); ++i) { if (i > 0) oss << delim; oss << anyToVal(arg.raw()[i]); }
                return Value(oss.str());
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                std::ostringstream oss; auto v = arg.rawData();
                for (size_t i = 0; i < v.size(); ++i) {
                    if (i > 0) oss << delim;
                    if constexpr (std::is_same_v<std::decay_t<decltype(v[i])>, double>) {
                        double val = v[i]; double rounded = std::round(val);
                        if (Tol::isEq(val, rounded, 1e5) && std::abs(rounded) < 1e15 && rounded == std::trunc(rounded)) oss << static_cast<int64_t>(rounded);
                        else oss << val;
                    }
                    else { oss << Value(v[i]); }
                }
                return Value(oss.str());
            }
            return expectContainer("join");
            }, args[0].data);
        });

    auto applyMathVectorOp = [expectContainer](const Value& val, auto opBody) -> Value {
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                List result; if (arg.empty()) return Value(result);
                Value acc = anyToVal(arg.raw()[0]); result.push_back(valToAny(acc));
                for (size_t i = 1; i < arg.size(); ++i) { acc = opBody(acc, anyToVal(arg.raw()[i])); result.push_back(valToAny(acc)); }
                return Value(result);
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix>) {
                auto v = arg.rawData();
                if (v.empty()) return Value(T(arg.getRows(), arg.getCols(), v));
                for (size_t i = 1; i < v.size(); ++i) {
                    Value res = opBody(Value(v[i - 1]), Value(v[i]));
                    if constexpr (std::is_same_v<T, RealMatrix>) v[i] = res.asDouble();
                    else v[i] = res.asComplex();
                }
                return Value(T(arg.getRows(), arg.getCols(), v));
            }
            throw std::runtime_error("Type Error: cumsum/cumprod expects a numeric vector or list.");
            }, val.data);
        };

    reg("cumsum", { 1 }, [applyMathVectorOp](const std::vector<Value>& args) -> Value {
        return applyMathVectorOp(args[0], [](const Value& a, const Value& b) { return a + b; });
        });
    reg("cumprod", { 1 }, [applyMathVectorOp](const std::vector<Value>& args) -> Value {
        return applyMathVectorOp(args[0], [](const Value& a, const Value& b) { return a * b; });
        });

    reg("diffs", { 1 }, [](const std::vector<Value>& args) -> Value {
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                if (arg.size() < 2) throw std::runtime_error("Runtime Error: diffs() requires at least 2 elements.");
                List result;
                for (size_t i = 0; i < arg.size() - 1; ++i) result.push_back(valToAny(anyToVal(arg.raw()[i + 1]) - anyToVal(arg.raw()[i])));
                return Value(result);
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix>) {
                auto v = arg.rawData();
                if (v.size() < 2) throw std::runtime_error("Runtime Error: diffs() requires at least 2 elements.");
                std::vector<std::decay_t<decltype(v[0])>> d(v.size() - 1);
                for (size_t i = 0; i < d.size(); ++i) d[i] = v[i + 1] - v[i];
                return Value(T(1, static_cast<int>(d.size()), d));
            }
            throw std::runtime_error("Type Error: diffs() expects a numeric vector or list.");
            }, args[0].data);
        });

    // range, fill, linspace
    reg("range", { 1, 2, 3 }, [](const std::vector<Value>& args) -> Value {
        if (args.size() == 1) { int end = static_cast<int>(std::round(args[0].asDouble())); if (end <= 0) return Value(RealMatrix(1, 0)); std::vector<double> v(end); for (int i = 0; i < end; ++i) v[i] = static_cast<double>(i); return Value(RealMatrix(1, end, v)); }
        if (args.size() == 2) { int start = static_cast<int>(std::round(args[0].asDouble())); int end = static_cast<int>(std::round(args[1].asDouble())); if (start >= end) return Value(RealMatrix(1, 0)); int n = end - start; std::vector<double> v(n); for (int i = 0; i < n; ++i) v[i] = static_cast<double>(start + i); return Value(RealMatrix(1, n, v)); }
        double start = args[0].asDouble(), end = args[1].asDouble(), step = args[2].asDouble(); if (Tol::isEq(step, 0.0)) throw std::runtime_error("Math Error: step cannot be zero."); std::vector<double> v; if (step > 0) { for (double x = start; x < end - Tol::EPS * 100; x += step) v.push_back(x); }
        else { for (double x = start; x > end + Tol::EPS * 100; x += step) v.push_back(x); } int n = static_cast<int>(v.size()); if (n == 0) return Value(RealMatrix(1, 0)); return Value(RealMatrix(1, n, v));
        });
    reg("fill", { 2 }, [](const std::vector<Value>& args) -> Value { int n = static_cast<int>(std::round(args[1].asDouble())); if (n < 0) throw std::runtime_error("Runtime Error: count must be non-negative."); return Value(RealMatrix(1, n, std::vector<double>(n, args[0].asDouble()))); });
    reg("linspace", { 3 }, [](const std::vector<Value>& args) -> Value { double a = args[0].asDouble(), b = args[1].asDouble(); int n = static_cast<int>(std::round(args[2].asDouble())); if (n < 1) throw std::runtime_error("Runtime Error: requires n >= 1."); std::vector<double> v(n); if (n == 1) v[0] = a; else { for (int i = 0; i < n; ++i) v[i] = a + (b - a) * i / (n - 1); } return Value(RealMatrix(1, n, v)); });
}

// =================================================================
// [18] StringMatrix
// =================================================================
void BuiltinRegistry::registerStringMatrix() {
    reg("strmat", {}, [](const std::vector<Value>& args) -> Value { if (args.size()<2) throw std::runtime_error("Runtime Error: strmat() expects at least 2 arguments."); int r=static_cast<int>(std::round(args[0].asDouble())),c=static_cast<int>(std::round(args[1].asDouble())); if (r<0||c<0) throw std::runtime_error("Runtime Error: strmat() dimensions must be non-negative."); if (args.size()==2) return Value(StringMatrix(r,c)); if (static_cast<int>(args.size())-2!=r*c) throw std::runtime_error("Runtime Error: strmat() element count does not match dimensions."); std::vector<std::string> flat; flat.reserve(r*c); for (size_t i=2;i<args.size();++i) { if (std::holds_alternative<std::string>(args[i].data)) flat.push_back(std::get<std::string>(args[i].data)); else { std::ostringstream oss; oss<<args[i]; flat.push_back(oss.str()); } } return Value(StringMatrix(r,c,flat)); });
    reg("strvec", {}, [](const std::vector<Value>& args) -> Value { std::vector<std::string> flat; for (const auto& a:args) { if (std::holds_alternative<std::string>(a.data)) flat.push_back(std::get<std::string>(a.data)); else { std::ostringstream oss; oss<<a; flat.push_back(oss.str()); } } return Value(StringMatrix(static_cast<int>(flat.size()),1,flat)); });
    reg("strrow", {}, [](const std::vector<Value>& args) -> Value { std::vector<std::string> flat; for (const auto& a:args) { if (std::holds_alternative<std::string>(a.data)) flat.push_back(std::get<std::string>(a.data)); else { std::ostringstream oss; oss<<a; flat.push_back(oss.str()); } } return Value(StringMatrix(1,static_cast<int>(flat.size()),flat)); });
    reg("strfill", { 2 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: strfill() expects a string and count."); int n=static_cast<int>(std::round(args[1].asDouble())); if (n<0) throw std::runtime_error("Runtime Error: strfill() count must be non-negative."); return Value(StringMatrix(1,n,std::vector<std::string>(n,std::get<std::string>(args[0].data)))); });
    reg("strfind", { 2 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<StringMatrix>(args[0].data)||!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("Type Error: strfind() expects StringMatrix and string."); const auto& m=std::get<StringMatrix>(args[0].data); const std::string& target=std::get<std::string>(args[1].data); for (int i=0;i<m.getRows();++i) for (int j=0;j<m.getCols();++j) if (m(i,j)==target) return Value(RealMatrix(1,2,{static_cast<double>(i),static_cast<double>(j)})); return Value(-1.0); });
    reg("strjoin", { 2 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<StringMatrix>(args[0].data)||!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("Type Error: strjoin() expects StringMatrix and string."); const auto& m=std::get<StringMatrix>(args[0].data); const std::string& delim=std::get<std::string>(args[1].data); std::string result; bool first=true; for (int i=0;i<m.getRows();++i) for (int j=0;j<m.getCols();++j) { if (!first) result+=delim; result+=m(i,j); first=false; } return Value(result); });
    reg("strsort", { 1 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<StringMatrix>(args[0].data)) throw std::runtime_error("Type Error: strsort() expects a StringMatrix."); const auto& m=std::get<StringMatrix>(args[0].data); std::vector<std::string> flat=m.rawData(); std::sort(flat.begin(),flat.end()); return Value(StringMatrix(m.getRows(),m.getCols(),flat)); });
    reg("toStrMat", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<StringMatrix>(args[0].data)) return args[0]; if (std::holds_alternative<RealMatrix>(args[0].data)) { const auto& m=std::get<RealMatrix>(args[0].data); std::vector<std::string> flat; for (int i=0;i<m.getRows();++i) for (int j=0;j<m.getCols();++j) { std::ostringstream oss; oss<<Value(m(i,j)); flat.push_back(oss.str()); } return Value(StringMatrix(m.getRows(),m.getCols(),flat)); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { const auto& m=std::get<ComplexMatrix>(args[0].data); std::vector<std::string> flat; for (int i=0;i<m.getRows();++i) for (int j=0;j<m.getCols();++j) { std::ostringstream oss; oss<<Value(m(i,j)); flat.push_back(oss.str()); } return Value(StringMatrix(m.getRows(),m.getCols(),flat)); } throw std::runtime_error("Type Error: toStrMat() expects a matrix."); });
}

// =================================================================
// [19] Dict / Instance 属性大一统透视 API
// =================================================================
void BuiltinRegistry::registerDictFunctions() {
    reg("dict", {}, [](const std::vector<Value>& args) -> Value { Dict d; if (args.size() % 2 != 0) throw std::runtime_error("Runtime Error: dict() expects even number of arguments."); for (size_t i = 0; i < args.size(); i += 2) { d.set(args[i], args[i + 1]); } return Value(d); });

    reg("keys", { 1 }, [](const std::vector<Value>& args) -> Value {
        Dict d = helpers::getDictMap(args[0], "keys");
        auto ks = d.getKeys();
        List L;
        for (const auto& k : ks) L.push_back(k);
        return Value(L);
        });
    // ★ 设定属性拾取别名！完美融合 Instance
    builtins["getFields"] = builtins["keys"]; builtinArity["getFields"] = builtinArity["keys"];

    reg("values", { 1 }, [](const std::vector<Value>& args) -> Value {
        Dict d = helpers::getDictMap(args[0], "values");
        const auto& entries = d.getEntries();
        List L;
        for (const auto& [k, v] : entries) L.push_back(v);
        return Value(L);
        });

    reg("hasKey", { 2 }, [](const std::vector<Value>& args) -> Value {
        Dict d = helpers::getDictMap(args[0], "hasKey");
        return Value(d.has(args[1]) ? 1.0 : 0.0);
        });
    // ★ 设定查询别名
    builtins["hasField"] = builtins["hasKey"]; builtinArity["hasField"] = builtinArity["hasKey"];
    builtins["has"] = builtins["hasKey"]; builtinArity["has"] = builtinArity["hasKey"];

    reg("removeKey", { 2 }, [](const std::vector<Value>& args) -> Value {
        Dict d = helpers::getDictMap(args[0], "removeKey");
        if (!d.remove(args[1])) throw std::runtime_error("Runtime Error: Key not found.");
        return args[0];
        });

    reg("dictSize", { 1 }, [](const std::vector<Value>& args) -> Value {
        Dict d = helpers::getDictMap(args[0], "dictSize"); return Value(static_cast<double>(d.size()));
        });
    builtins["size"] = builtins["dictSize"]; builtinArity["size"] = builtinArity["dictSize"];

    reg("dictMerge", { 2 }, [](const std::vector<Value>& args) -> Value {
        Dict d1 = helpers::getDictMap(args[0], "dictMerge"); Dict d2 = helpers::getDictMap(args[1], "dictMerge");
        for (const auto& [k, v] : d2.getEntries()) d1.set(k, v);
        return args[0];
        });

    reg("dictPairs", { 1 }, [](const std::vector<Value>& args) -> Value {
        Dict d = helpers::getDictMap(args[0], "dictPairs");
        const auto& entries = d.getEntries();
        List L;
        for (const auto& [k, v] : entries) {
            List pair;
            pair.push_back(k);
            pair.push_back(v);
            pair.freeze();
            L.push_back(Value(pair));
        }
        return Value(L);
        });
}

// =================================================================
// [20] List & Conversion
// =================================================================
void BuiltinRegistry::registerListConversion() {
    reg("list", {}, [](const std::vector<Value>& args) -> Value { List L; for (const auto& a : args) L.push_back(valToAny(a)); return Value(L); });

    reg("toList", { 1 }, [](const std::vector<Value>& args) -> Value {
        return std::visit([&args](auto&& arg) -> Value {   // ★ 修改1: 捕获 &args
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) return Value(arg);
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                if (arg.getRows() == 1 || arg.getCols() == 1) {
                    List L; for (const auto& d : arg.rawData()) L.push_back(valToAny(Value(d))); return Value(L);
                }
                List rows;
                for (int i = 0; i < arg.getRows(); ++i) {
                    List row; for (int j = 0; j < arg.getCols(); ++j) row.push_back(valToAny(Value(arg(i, j)))); row.freeze(); rows.push_back(valToAny(Value(row)));
                }
                return Value(rows);
            }
            else if constexpr (std::is_same_v<T, std::string>) {
                List L; for (char c : arg) L.push_back(valToAny(Value(std::string(1, c)))); return Value(L);
            }
            else if constexpr (std::is_same_v<T, Set>) {
                List L; for (const auto& [key, val] : arg.raw()) L.push_back(val); return Value(L);
            }
            List L; L.push_back(valToAny(args[0])); return Value(L);  // ★ 修改2: 用 args[0] 替代 Value(arg)
            }, args[0].data);
        });

    reg("toStrVec", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<List>(args[0].data)) {
            const auto& L = std::get<List>(args[0].data); std::vector<std::string> flat;
            for (const auto& e : L.raw()) {
                Value v = anyToVal(e);
                if (std::holds_alternative<std::string>(v.data)) flat.push_back(std::get<std::string>(v.data));
                else { std::ostringstream oss; oss << v; flat.push_back(oss.str()); }
            }
            return Value(StringMatrix(static_cast<int>(flat.size()), 1, flat));
        }
        if (std::holds_alternative<StringMatrix>(args[0].data)) return args[0];
        throw std::runtime_error("Type Error: toStrVec() expects a List or StringMatrix.");
        });

    reg("toArray", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<List>(args[0].data)) throw std::runtime_error("Type Error: expects a List.");
        const auto& L = std::get<List>(args[0].data); std::vector<double> flat;
        for (const auto& e : L.raw()) flat.push_back(anyToVal(e).asDouble());
        return Value(RealMatrix(1, static_cast<int>(flat.size()), flat));
        });

    reg("toMatrix", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data) || std::holds_alternative<ComplexMatrix>(args[0].data) || std::holds_alternative<StringMatrix>(args[0].data)) return args[0];
        if (!std::holds_alternative<List>(args[0].data)) throw std::runtime_error("Type Error: expects a List or matrix.");
        const auto& L = std::get<List>(args[0].data);
        if (L.empty()) return Value(RealMatrix(0, 0));
        Value first = anyToVal(L.raw()[0]);
        bool isNested = std::holds_alternative<List>(first.data);
        auto isReal = [](const Value& v) { return std::holds_alternative<double>(v.data) || std::holds_alternative<BigInt>(v.data) || std::holds_alternative<Fraction>(v.data); };
        auto isNumeric = [](const Value& v) { return std::holds_alternative<double>(v.data) || std::holds_alternative<BigInt>(v.data) || std::holds_alternative<Fraction>(v.data) || std::holds_alternative<Complex>(v.data); };
        auto isStr = [](const Value& v) { return std::holds_alternative<std::string>(v.data); };
        auto valToStr = [](const Value& v) -> std::string { if (std::holds_alternative<std::string>(v.data)) return std::get<std::string>(v.data); std::ostringstream oss; oss << v; return oss.str(); };

        if (!isNested) {
            int n = static_cast<int>(L.size()); bool allReal = true, allNum = true, allStr = true;
            for (const auto& e : L.raw()) { Value v = anyToVal(e); if (!isReal(v)) allReal = false; if (!isNumeric(v)) allNum = false; if (!isStr(v)) allStr = false; }
            if (allStr) { std::vector<std::string> flat; for (const auto& e : L.raw()) flat.push_back(std::get<std::string>(anyToVal(e).data)); return Value(StringMatrix(1, n, flat)); }
            if (allReal) { std::vector<double> flat; for (const auto& e : L.raw()) flat.push_back(anyToVal(e).asDouble()); return Value(RealMatrix(1, n, flat)); }
            if (allNum) { std::vector<Complex> flat; for (const auto& e : L.raw()) flat.push_back(anyToVal(e).asComplex()); return Value(ComplexMatrix(1, n, flat)); }
            std::vector<std::string> flat; for (const auto& e : L.raw()) flat.push_back(valToStr(anyToVal(e))); return Value(StringMatrix(1, n, flat));
        }

        int rows = static_cast<int>(L.size()), cols = -1; bool allReal = true, allNum = true, allStr = true;
        std::vector<std::vector<Value>> grid;
        for (const auto& rowAny : L.raw()) {
            Value rowVal = anyToVal(rowAny);
            if (!std::holds_alternative<List>(rowVal.data)) throw std::runtime_error("Type Error: expects uniform List of Lists.");
            const auto& rowList = std::get<List>(rowVal.data);
            if (cols == -1) cols = static_cast<int>(rowList.size()); else if (static_cast<int>(rowList.size()) != cols) throw std::runtime_error("Type Error: rows must have equal length.");
            std::vector<Value> rowVec;
            for (const auto& e : rowList.raw()) { Value v = anyToVal(e); if (!isReal(v)) allReal = false; if (!isNumeric(v)) allNum = false; if (!isStr(v)) allStr = false; rowVec.push_back(v); }
            grid.push_back(std::move(rowVec));
        }
        if (cols <= 0) return Value(RealMatrix(0, 0));
        if (allStr) { std::vector<std::string> flat; for (const auto& row : grid) for (const auto& v : row) flat.push_back(std::get<std::string>(v.data)); return Value(StringMatrix(rows, cols, flat)); }
        if (allReal) { std::vector<double> flat; for (const auto& row : grid) for (const auto& v : row) flat.push_back(v.asDouble()); return Value(RealMatrix(rows, cols, flat)); }
        if (allNum) { std::vector<Complex> flat; for (const auto& row : grid) for (const auto& v : row) flat.push_back(v.asComplex()); return Value(ComplexMatrix(rows, cols, flat)); }
        std::vector<std::string> flat; for (const auto& row : grid) for (const auto& v : row) flat.push_back(valToStr(v)); return Value(StringMatrix(rows, cols, flat));
        });

    reg("zip", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<List>(args[0].data) || std::holds_alternative<List>(args[1].data)) {
            auto extractL = [](const Value& v) -> List {
                if (std::holds_alternative<List>(v.data)) return std::get<List>(v.data);
                List L;
                if (std::holds_alternative<RealMatrix>(v.data)) { for (double d : std::get<RealMatrix>(v.data).rawData()) L.push_back(valToAny(Value(d))); }
                else if (std::holds_alternative<ComplexMatrix>(v.data)) { for (const auto& c : std::get<ComplexMatrix>(v.data).rawData()) L.push_back(valToAny(Value(c))); }
                else if (std::holds_alternative<StringMatrix>(v.data)) { for (const auto& s : std::get<StringMatrix>(v.data).rawData()) L.push_back(valToAny(Value(s))); }
                else L.push_back(valToAny(v));
                return L;
                };
            List a = extractL(args[0]), b = extractL(args[1]);
            if (a.size() != b.size()) throw std::runtime_error("Math Error: zip() requires same length.");
            List result;
            for (size_t i = 0; i < a.size(); ++i) { List pair; pair.push_back(a.raw()[i]); pair.push_back(b.raw()[i]); pair.freeze(); result.push_back(valToAny(Value(pair))); }
            return Value(result);
        }

        bool hasString = std::holds_alternative<StringMatrix>(args[0].data) || std::holds_alternative<StringMatrix>(args[1].data);
        bool hasComplex = std::holds_alternative<ComplexMatrix>(args[0].data) || std::holds_alternative<ComplexMatrix>(args[1].data);

        auto getLenAndFetch = [](const Value& v, int i, bool toString, bool toComplex) -> Value {
            if (std::holds_alternative<StringMatrix>(v.data)) return Value(std::get<StringMatrix>(v.data).rawData()[i]);
            if (std::holds_alternative<ComplexMatrix>(v.data)) {
                if (toString) { std::ostringstream oss; oss << Value(std::get<ComplexMatrix>(v.data).rawData()[i]); return Value(oss.str()); }
                return Value(std::get<ComplexMatrix>(v.data).rawData()[i]);
            }
            if (std::holds_alternative<RealMatrix>(v.data)) {
                if (toString) { std::ostringstream oss; oss << Value(std::get<RealMatrix>(v.data).rawData()[i]); return Value(oss.str()); }
                if (toComplex) return Value(Complex(std::get<RealMatrix>(v.data).rawData()[i]));
                return Value(std::get<RealMatrix>(v.data).rawData()[i]);
            }
            return v;
            };

        auto getLen = [](const Value& v) {
            if (std::holds_alternative<StringMatrix>(v.data)) return std::get<StringMatrix>(v.data).rawData().size();
            if (std::holds_alternative<ComplexMatrix>(v.data)) return std::get<ComplexMatrix>(v.data).rawData().size();
            if (std::holds_alternative<RealMatrix>(v.data)) return std::get<RealMatrix>(v.data).rawData().size();
            return size_t(1);
            };

        int nA = static_cast<int>(getLen(args[0])), nB = static_cast<int>(getLen(args[1]));
        if (nA != nB) throw std::runtime_error("Math Error: zip() vectors must have same length.");
        int n = nA;

        if (hasString) {
            std::vector<std::string> flat(n * 2);
            for (int i = 0; i < n; ++i) { flat[i * 2] = std::get<std::string>(getLenAndFetch(args[0], i, true, false).data); flat[i * 2 + 1] = std::get<std::string>(getLenAndFetch(args[1], i, true, false).data); }
            return Value(StringMatrix(n, 2, flat));
        }
        if (hasComplex) {
            std::vector<Complex> flat(n * 2);
            for (int i = 0; i < n; ++i) { flat[i * 2] = getLenAndFetch(args[0], i, false, true).asComplex(); flat[i * 2 + 1] = getLenAndFetch(args[1], i, false, true).asComplex(); }
            return Value(ComplexMatrix(n, 2, flat));
        }
        std::vector<double> flat(n * 2);
        for (int i = 0; i < n; ++i) { flat[i * 2] = getLenAndFetch(args[0], i, false, false).asDouble(); flat[i * 2 + 1] = getLenAndFetch(args[1], i, false, false).asDouble(); }
        return Value(RealMatrix(n, 2, flat));
        });

    reg("cat", {}, [](const std::vector<Value>& args) -> Value {
        if (args.empty()) throw std::runtime_error("Runtime Error: cat() expects at least 1 argument.");
        bool hasList = false, hasStringMat = false, hasComplexMat = false;
        for (const auto& a : args) {
            if (std::holds_alternative<List>(a.data)) hasList = true;
            else if (std::holds_alternative<StringMatrix>(a.data) || std::holds_alternative<std::string>(a.data)) hasStringMat = true;
            else if (std::holds_alternative<ComplexMatrix>(a.data) || std::holds_alternative<Complex>(a.data)) hasComplexMat = true;
            else if (!std::holds_alternative<RealMatrix>(a.data) && !std::holds_alternative<double>(a.data) && !std::holds_alternative<BigInt>(a.data) && !std::holds_alternative<Fraction>(a.data)) {
                hasList = true;
            }
        }
        if (hasList) {
            List result;
            for (const auto& a : args) {
                if (std::holds_alternative<List>(a.data)) { for (const auto& e : std::get<List>(a.data).raw()) result.push_back(e); }
                else result.push_back(valToAny(a));
            }
            return Value(result);
        }
        if (hasStringMat) {
            std::vector<std::string> flat;
            for (const auto& a : args) {
                if (std::holds_alternative<StringMatrix>(a.data)) { auto d = std::get<StringMatrix>(a.data).rawData(); flat.insert(flat.end(), d.begin(), d.end()); }
                else if (std::holds_alternative<RealMatrix>(a.data)) { for (auto d : std::get<RealMatrix>(a.data).rawData()) { std::ostringstream oss; oss << Value(d); flat.push_back(oss.str()); } }
                else if (std::holds_alternative<ComplexMatrix>(a.data)) { for (auto d : std::get<ComplexMatrix>(a.data).rawData()) { std::ostringstream oss; oss << Value(d); flat.push_back(oss.str()); } }
                else { std::ostringstream oss; oss << a; flat.push_back(oss.str()); }
            }
            return Value(StringMatrix(1, static_cast<int>(flat.size()), flat));
        }
        if (hasComplexMat) {
            std::vector<Complex> flat;
            for (const auto& a : args) {
                if (std::holds_alternative<ComplexMatrix>(a.data)) { auto d = std::get<ComplexMatrix>(a.data).rawData(); flat.insert(flat.end(), d.begin(), d.end()); }
                else if (std::holds_alternative<RealMatrix>(a.data)) { for (auto d : std::get<RealMatrix>(a.data).rawData()) flat.push_back(Complex(d)); }
                else { flat.push_back(a.asComplex()); }
            }
            return Value(ComplexMatrix(1, static_cast<int>(flat.size()), flat));
        }
        std::vector<double> flat;
        for (const auto& a : args) {
            if (std::holds_alternative<RealMatrix>(a.data)) { auto d = std::get<RealMatrix>(a.data).rawData(); flat.insert(flat.end(), d.begin(), d.end()); }
            else { flat.push_back(a.asDouble()); }
        }
        return Value(RealMatrix(1, static_cast<int>(flat.size()), flat));
        });
}

// =================================================================
// [24] Class 内省
// =================================================================
void BuiltinRegistry::registerIntrospection() {
    reg("isinstance", { 1, 2 }, [](const std::vector<Value>& args) -> Value {
        if (args.size() == 1) {
            // 单参数：检测是否为任意类的实例
            return Value(std::holds_alternative<std::shared_ptr<Instance>>(args[0].data) ? 1.0 : 0.0);
        }
        // 双参数：检测是否为指定类（含继承链）的实例
        if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) return Value(0.0);
        if (!std::holds_alternative<std::shared_ptr<ClassDefinition>>(args[1].data))
            throw std::runtime_error("Type Error: isinstance() second argument must be a class.");
        auto inst = std::get<std::shared_ptr<Instance>>(args[0].data);
        auto cls = std::get<std::shared_ptr<ClassDefinition>>(args[1].data);
        auto c = inst->classDef;
        while (c) { if (c.get() == cls.get()) return Value(1.0); c = c->parent; }
        return Value(0.0);
        });    
    reg("getClass", { 1 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) throw std::runtime_error("Type Error: getClass() expects an instance."); return Value(std::get<std::shared_ptr<Instance>>(args[0].data)->classDef); });
    reg("getParent", { 1 }, [](const std::vector<Value>& args) -> Value { std::shared_ptr<ClassDefinition> cls; if (std::holds_alternative<std::shared_ptr<ClassDefinition>>(args[0].data)) cls=std::get<std::shared_ptr<ClassDefinition>>(args[0].data); else if (std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) cls=std::get<std::shared_ptr<Instance>>(args[0].data)->classDef; else throw std::runtime_error("Type Error: getParent() expects a class or instance."); if (!cls->parent) return Value::none(); return Value(cls->parent); });
}

// =================================================================
// format() + type()
// =================================================================
void BuiltinRegistry::registerFormatType() {
    reg("format", {}, [](const std::vector<Value>& args) -> Value {
        if (args.size() < 1) throw std::runtime_error("Runtime Error: format() expects at least 1 argument.");
        if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: format() first argument must be a format string.");
        std::vector<Value> pa = args;
        for (size_t i = 1; i < pa.size(); ++i) {
            if (std::holds_alternative<std::shared_ptr<Instance>>(pa[i].data)) {
                auto inst = std::get<std::shared_ptr<Instance>>(pa[i].data);
                auto [found, result] = tryCallDunder(inst, "__str__");
                if (found) pa[i] = result;
            }
        }
        std::string fmt = std::get<std::string>(pa[0].data); std::string result; size_t argIdx = 1;
        for (size_t i = 0; i < fmt.size(); ++i) {
            if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == '}') { if (argIdx >= pa.size()) throw std::runtime_error("Runtime Error: format() too few arguments."); std::ostringstream oss; oss << pa[argIdx++]; result += oss.str(); i += 1; }
            else if (fmt[i] == '{' && i + 1 < fmt.size() && fmt[i + 1] == ':') { size_t close = fmt.find('}', i); if (close == std::string::npos) throw std::runtime_error("Runtime Error: format() unclosed '{'."); std::string spec = fmt.substr(i + 2, close - i - 2); if (argIdx >= pa.size()) throw std::runtime_error("Runtime Error: format() too few arguments."); char align = '\0'; int width = 0; int precision = -1; char type = '\0'; size_t si = 0; if (si < spec.size() && (spec[si] == '<' || spec[si] == '>' || spec[si] == '^')) align = spec[si++]; while (si < spec.size() && spec[si] >= '0' && spec[si] <= '9') width = width * 10 + (spec[si++] - '0'); if (si < spec.size() && spec[si] == '.') { si++; precision = 0; while (si < spec.size() && spec[si] >= '0' && spec[si] <= '9')precision = precision * 10 + (spec[si++] - '0'); } if (si < spec.size()) type = spec[si++]; std::ostringstream oss; if (type == 'f' || type == 'e') { double v = pa[argIdx].asDouble(); if (precision >= 0)oss << std::fixed << std::setprecision(precision); if (type == 'e')oss << std::scientific; oss << v; } else if (type == 'd')oss << static_cast<int64_t>(std::round(pa[argIdx].asDouble())); else if (type == 'x')oss << std::hex << static_cast<int64_t>(std::round(pa[argIdx].asDouble())); else { if (precision >= 0)oss << std::fixed << std::setprecision(precision); oss << pa[argIdx]; } std::string valStr = oss.str(); if (width > 0 && static_cast<int>(valStr.size()) < width) { int pad = width - static_cast<int>(valStr.size()); if (align == '<')valStr += std::string(pad, ' '); else if (align == '^') { int l = pad / 2, r = pad - l; valStr = std::string(l, ' ') + valStr + std::string(r, ' '); } else valStr = std::string(pad, ' ') + valStr; } result += valStr; argIdx++; i = close; }
            else { result += fmt[i]; }
        }
        return Value(result);
        });

    reg("type", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(args[0].typeName());
        });
}

// =================================================================
// [HOF] 高阶函数（使用通用 callClosure）
// =================================================================

void BuiltinRegistry::registerHigherOrder() {

    reg("apply", { 2 }, [](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        std::vector<Value> unpackedArgs;
        const Value& argList = args[1];

        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                for (const auto& e : arg.raw()) unpackedArgs.push_back(std::any_cast<Value>(e));
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                if (arg.getRows() != 1 && arg.getCols() != 1) throw std::runtime_error("Type Error: map() expects 1D vector.");
                for (const auto& d : arg.rawData()) unpackedArgs.push_back(Value(d));
            }
            else if constexpr (std::is_same_v<T, std::string>) {
                for (char c : arg) unpackedArgs.push_back(Value(std::string(1, c)));
            }
            else {
                throw std::runtime_error("Type Error: apply() expects a function and an iterable argument list/vector.");
            }
            return safeCallFunction(cl, unpackedArgs);
            }, argList.data);
        });

    reg("map", { 2 }, [](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        if (!cl->acceptsArgCount(1)) throw std::runtime_error("Runtime Error: map() requires a single-parameter function.");

        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                List result;
                for (const auto& e : arg.raw()) { jc::checkInterrupt(); result.push_back(valToAny(safeCallFunction(cl, { anyToVal(e) }))); }
                return Value(result);
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                auto flat = arg.rawData();
                List fallback; bool typeConflict = false;
                bool hasString = false, hasComp = false;
                std::vector<double> rd; std::vector<Complex> rc; std::vector<std::string> rs;

                for (size_t i = 0; i < flat.size(); ++i) {
                    jc::checkInterrupt();
                    Value y = safeCallFunction(cl, { Value(flat[i]) });
                    if (i == 0) {
                        if (std::holds_alternative<std::string>(y.data)) hasString = true;
                        else if (std::holds_alternative<Complex>(y.data)) hasComp = true;
                        else if (!std::holds_alternative<double>(y.data) && !std::holds_alternative<BigInt>(y.data) && !std::holds_alternative<Fraction>(y.data)) typeConflict = true;
                    }
                    if (typeConflict) { fallback.push_back(valToAny(y)); }
                    else if (hasString) {
                        if (std::holds_alternative<std::string>(y.data)) rs.push_back(std::get<std::string>(y.data));
                        else { std::ostringstream oss; oss << y; rs.push_back(oss.str()); }
                    }
                    else if (hasComp) {
                        try { rc.push_back(y.asComplex()); }
                        catch (...) { typeConflict = true; fallback.clear(); for (auto r : rc) fallback.push_back(valToAny(Value(r))); fallback.push_back(valToAny(y)); }
                    }
                    else {
                        try { rd.push_back(y.asDouble()); }
                        catch (...) {
                            try { rc.clear(); for (auto d : rd) rc.push_back(Complex(d)); rc.push_back(y.asComplex()); hasComp = true; }
                            catch (...) { typeConflict = true; fallback.clear(); for (auto d : rd) fallback.push_back(valToAny(Value(d))); fallback.push_back(valToAny(y)); }
                        }
                    }
                }

                if (typeConflict) {
                    for (size_t i = fallback.size(); i < flat.size(); ++i) { jc::checkInterrupt(); fallback.push_back(valToAny(safeCallFunction(cl, { Value(flat[i]) }))); }
                    return Value(fallback);
                }
                if (hasString) return Value(StringMatrix(arg.getRows(), arg.getCols(), rs));
                if (hasComp) return Value(ComplexMatrix(arg.getRows(), arg.getCols(), rc));
                return Value(RealMatrix(arg.getRows(), arg.getCols(), rd));
            }
            throw std::runtime_error("Type Error: map() expects a vector/matrix/list.");
            }, args[1].data);
        });

    reg("filter", { 2 }, [](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        if (!cl->acceptsArgCount(1)) throw std::runtime_error("Runtime Error: filter() requires a single-parameter function.");

        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                List result;
                for (const auto& e : arg.raw()) { jc::checkInterrupt(); if (isTruthy(safeCallFunction(cl, { anyToVal(e) }))) result.push_back(e); }
                return Value(result);
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                auto flat = arg.rawData();
                using Elem = std::decay_t<decltype(flat[0])>;
                std::vector<Elem> result;
                for (const auto& x : flat) { jc::checkInterrupt(); if (isTruthy(safeCallFunction(cl, { Value(x) }))) result.push_back(x); }
                int n = static_cast<int>(result.size());
                if (n == 0) return Value(T(1, 0));
                return Value(T(1, n, result));
            }
            throw std::runtime_error("Type Error: filter() expects a vector/matrix/list.");
            }, args[1].data);
        });

    reg("reduce", { 2, 3 }, [](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        if (!cl->acceptsArgCount(2)) throw std::runtime_error("Runtime Error: reduce() requires a two-parameter function.");

        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                Value acc; size_t startIdx = 0;
                if (args.size() == 3) { acc = args[2]; }
                else { if (arg.empty()) throw std::runtime_error("Runtime Error: reduce() on empty."); acc = anyToVal(arg.raw()[0]); startIdx = 1; }
                for (size_t i = startIdx; i < arg.size(); ++i) { jc::checkInterrupt(); acc = safeCallFunction(cl, { acc, anyToVal(arg.raw()[i]) }); }
                return acc;
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                auto flat = arg.rawData();
                Value acc; size_t startIdx = 0;
                if (args.size() == 3) { acc = args[2]; }
                else { if (flat.empty()) throw std::runtime_error("Runtime Error: reduce() on empty."); acc = Value(flat[0]); startIdx = 1; }
                for (size_t i = startIdx; i < flat.size(); ++i) { jc::checkInterrupt(); acc = safeCallFunction(cl, { acc, Value(flat[i]) }); }
                return acc;
            }
            throw std::runtime_error("Type Error: reduce() expects a vector/matrix/list.");
            }, args[1].data);
        });

    reg("any", { 2 }, [](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        if (!cl->acceptsArgCount(1)) throw std::runtime_error("Runtime Error: any() requires a single-parameter function.");
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                for (const auto& e : arg.raw()) { jc::checkInterrupt(); if (isTruthy(safeCallFunction(cl, { anyToVal(e) }))) return Value(1.0); } return Value(0.0);
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                for (const auto& x : arg.rawData()) { jc::checkInterrupt(); if (isTruthy(safeCallFunction(cl, { Value(x) }))) return Value(1.0); } return Value(0.0);
            }
            throw std::runtime_error("Type Error: any() expects a vector/list.");
            }, args[1].data);
        });

    reg("all", { 2 }, [](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        if (!cl->acceptsArgCount(1)) throw std::runtime_error("Runtime Error: all() requires a single-parameter function.");
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, List>) {
                for (const auto& e : arg.raw()) { jc::checkInterrupt(); if (!isTruthy(safeCallFunction(cl, { anyToVal(e) }))) return Value(0.0); } return Value(1.0);
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                for (const auto& x : arg.rawData()) { jc::checkInterrupt(); if (!isTruthy(safeCallFunction(cl, { Value(x) }))) return Value(0.0); } return Value(1.0);
            }
            throw std::runtime_error("Type Error: all() expects a vector/list.");
            }, args[1].data);
        });

    reg("countIf", { 2 }, [](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        if (!cl->acceptsArgCount(1)) throw std::runtime_error("Runtime Error: countIf() requires a single-parameter function.");
        return std::visit([&](auto&& arg) -> Value {
            using T = std::decay_t<decltype(arg)>;
            int c = 0;
            if constexpr (std::is_same_v<T, List>) {
                for (const auto& e : arg.raw()) { jc::checkInterrupt(); if (isTruthy(safeCallFunction(cl, { anyToVal(e) }))) c++; } return Value(static_cast<double>(c));
            }
            else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                for (const auto& x : arg.rawData()) { jc::checkInterrupt(); if (isTruthy(safeCallFunction(cl, { Value(x) }))) c++; } return Value(static_cast<double>(c));
            }
            throw std::runtime_error("Type Error: countIf() expects a vector/list.");
            }, args[1].data);
        });

    reg("sort", { 1, 2 }, [](const std::vector<Value>& args) -> Value {
        if (args.size() == 1) {
            return std::visit([&](auto&& arg) -> Value {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, RealMatrix>) {
                    auto f = arg.rawData(); std::sort(f.begin(), f.end()); return Value(RealMatrix(1, static_cast<int>(f.size()), f));
                }
                else if constexpr (std::is_same_v<T, StringMatrix>) {
                    auto f = arg.rawData(); std::sort(f.begin(), f.end()); return Value(StringMatrix(1, static_cast<int>(f.size()), f));
                }
                else if constexpr (std::is_same_v<T, List>) {
                    List L = arg;
                    std::sort(L.raw().begin(), L.raw().end(), [](const Value& a, const Value& b) {
                        std::ostringstream oa, ob; oa << a; ob << b; return oa.str() < ob.str();
                        });
                    return Value(L);
                }
                throw std::runtime_error("Type Error: sort() without comparator expects an array or list.");
                }, args[0].data);
        }
        else if (args.size() == 2) {
            auto cmp = args[1].asFunction();
            if (!cmp->acceptsArgCount(2)) throw std::runtime_error("Runtime Error: sort() comparator must be a 2-parameter function.");
            return std::visit([&](auto&& arg) -> Value {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, List>) {
                    List L = arg;
                    std::sort(L.raw().begin(), L.raw().end(), [&](const Value& a, const Value& b) {
                        return isTruthy(safeCallFunction(cmp, { a, b }));
                        });
                    return Value(L);
                }
                else if constexpr (std::is_same_v<T, RealMatrix> || std::is_same_v<T, ComplexMatrix> || std::is_same_v<T, StringMatrix>) {
                    auto f = arg.rawData();
                    std::sort(f.begin(), f.end(), [&](const auto& a, const auto& b) {
                        return isTruthy(safeCallFunction(cmp, { Value(a), Value(b) }));
                        });
                    return Value(T(1, static_cast<int>(f.size()), f));
                }
                throw std::runtime_error("Type Error: sort() expects a vector or list.");
                }, args[0].data);
        }
        throw std::runtime_error("Runtime Error: sort() expects 1 or 2 arguments.");
        });

}

// =================================================================
// [Phase 2] 微积分引擎
// =================================================================
void BuiltinRegistry::registerCalculus() {

    // 通用 eval 辅助：调用单参数函数 f(x)
    auto evalFunc = [](const std::shared_ptr<FunctionClosure>& cl, double x) -> double {
        return safeCallFunction(cl, { Value(x) }).asDouble();
        };

    reg("solveE", { 2 }, [evalFunc](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction(); double x = args[1].asDouble(); double h = 1e-5;
        for (int i = 0; i < 1000; ++i) {
            jc::checkInterrupt();
            double y = evalFunc(cl, x);
            if (Tol::clean(y, std::max(1.0, std::abs(x)), 1e7) == 0.0) return Value(x);
            double df = (evalFunc(cl, x + h) - evalFunc(cl, x - h)) / (2 * h);
            if (Tol::isEq(df, 0.0)) x += 1e-4; else x -= y / df;
        }
        throw std::runtime_error("Math Error: Equation solver did not converge.");
        });

    reg("table", {}, [](const std::vector<Value>& args) -> Value {
        if (args.size() < 2) throw std::runtime_error("Runtime Error: table() expects at least 2 arguments.");
        auto cl = args[0].asFunction();
        int k = static_cast<int>(cl->paramNames.size());
        // ★ 如果参数名为空（VM 闭包可能如此），从 arity 推断
        if (k == 0 && cl->isNative()) {
            // 尝试从 maxArgs 推断
            k = cl->maxArgs();
            if (k == 0) k = 1; // 默认单参数
        }

        auto evalRow = [&](const std::vector<Value>& rowArgs,
            std::vector<double>& res_d, std::vector<Complex>& res_c, bool& hasComplex) {
                jc::checkInterrupt();
                Value y_val = safeCallFunction(cl, rowArgs);
                if (!hasComplex) {
                    try { res_d.push_back(y_val.asDouble()); }
                    catch (...) { hasComplex = true; for (double d : res_d) res_c.push_back(Complex(d)); res_c.push_back(y_val.asComplex()); }
                }
                else { res_c.push_back(y_val.asComplex()); }
            };

        if (args.size() == 4 && k == 1 &&
            !std::holds_alternative<RealMatrix>(args[1].data) &&
            !std::holds_alternative<ComplexMatrix>(args[1].data)) {
            double start = args[1].asDouble(), step = args[2].asDouble();
            int count = static_cast<int>(std::round(args[3].asDouble()));
            if (count <= 0) throw std::runtime_error("Math Error: count must be positive.");
            std::vector<double> rd; std::vector<Complex> rc; bool hc = false;
            for (int i = 0; i < count; ++i) evalRow({ Value(start + i * step) }, rd, rc, hc);
            if (hc) return Value(ComplexMatrix(count, 1, rc));
            return Value(RealMatrix(count, 1, rd));
        }
        if (args.size() == 2) {
            int N = 0; std::vector<double> rd; std::vector<Complex> rc; bool hc = false;
            if (std::holds_alternative<RealMatrix>(args[1].data)) {
                RealMatrix M = std::get<RealMatrix>(args[1].data);
                if (M.getCols() != k) throw std::runtime_error("Math Error: Matrix columns must match function parameter count.");
                N = M.getRows();
                for (int i = 0; i < N; ++i) { std::vector<Value> row; for (int j = 0; j < k; ++j) row.push_back(Value(M(i, j))); evalRow(row, rd, rc, hc); }
            }
            else if (std::holds_alternative<ComplexMatrix>(args[1].data)) {
                ComplexMatrix M = std::get<ComplexMatrix>(args[1].data);
                if (M.getCols() != k) throw std::runtime_error("Math Error: Matrix columns must match function parameter count.");
                N = M.getRows();
                for (int i = 0; i < N; ++i) { std::vector<Value> row; for (int j = 0; j < k; ++j) row.push_back(Value(M(i, j))); evalRow(row, rd, rc, hc); }
            }
            else throw std::runtime_error("Type Error: Expected a matrix.");
            if (N == 0) return Value(RealMatrix(0, 0));
            if (hc) return Value(ComplexMatrix(N, 1, rc));
            return Value(RealMatrix(N, 1, rd));
        }
        if (args.size() == static_cast<size_t>(k + 1)) {
            int N = -1;
            for (int i = 1; i <= k; ++i) {
                if (std::holds_alternative<RealMatrix>(args[i].data)) { if (std::get<RealMatrix>(args[i].data).getCols() != 1) throw std::runtime_error("Math Error: Arguments must be column vectors."); if (N == -1) N = std::get<RealMatrix>(args[i].data).getRows(); else if (N != std::get<RealMatrix>(args[i].data).getRows()) throw std::runtime_error("Math Error: Vectors must have same length."); }
                else if (std::holds_alternative<ComplexMatrix>(args[i].data)) { if (std::get<ComplexMatrix>(args[i].data).getCols() != 1) throw std::runtime_error("Math Error: Arguments must be column vectors."); if (N == -1) N = std::get<ComplexMatrix>(args[i].data).getRows(); else if (N != std::get<ComplexMatrix>(args[i].data).getRows()) throw std::runtime_error("Math Error: Vectors must have same length."); }
                else throw std::runtime_error("Type Error: Expected column vectors.");
            }
            if (N <= 0) return Value(RealMatrix(0, 0));
            std::vector<double> rd; std::vector<Complex> rc; bool hc = false;
            for (int r = 0; r < N; ++r) { std::vector<Value> row; for (int c = 1; c <= k; ++c) { if (std::holds_alternative<RealMatrix>(args[c].data)) row.push_back(Value(std::get<RealMatrix>(args[c].data)(r, 0))); else row.push_back(Value(std::get<ComplexMatrix>(args[c].data)(r, 0))); } evalRow(row, rd, rc, hc); }
            if (hc) return Value(ComplexMatrix(N, 1, rc));
            return Value(RealMatrix(N, 1, rd));
        }
        throw std::runtime_error("Runtime Error: Argument count mismatch.");
        });
}

// =================================================================
// [Phase 2] 文件 I/O 引擎
// =================================================================
void BuiltinRegistry::registerFileIO() {

    reg("readFile", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("Type Error: readFile() expects a string path.");
        std::string path = safeResolvePath(std::get<std::string>(args[0].data));
        std::ifstream file(path);
        if (!file.is_open()) throw std::runtime_error("IO Error: Cannot open file '" + path + "'.");
        std::ostringstream oss; oss << file.rdbuf(); file.close();
        return Value(oss.str());
        });

    reg("writeFile", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("Type Error: writeFile() expects a string path.");
        std::string path = safeResolvePath(std::get<std::string>(args[0].data));
        std::string content;
        if (std::holds_alternative<std::string>(args[1].data)) content = std::get<std::string>(args[1].data);
        else { std::ostringstream oss; oss << args[1]; content = oss.str(); }
        std::ofstream file(path);
        if (!file.is_open()) throw std::runtime_error("IO Error: Cannot write to file '" + path + "'.");
        file << content; file.close();
        return Value::none();
        });

    reg("appendFile", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("Type Error: appendFile() expects a string path.");
        std::string path = safeResolvePath(std::get<std::string>(args[0].data));
        std::string content;
        if (std::holds_alternative<std::string>(args[1].data)) content = std::get<std::string>(args[1].data);
        else { std::ostringstream oss; oss << args[1]; content = oss.str(); }
        std::ofstream file(path, std::ios::app);
        if (!file.is_open()) throw std::runtime_error("IO Error: Cannot append to file '" + path + "'.");
        file << content; file.close();
        return Value::none();
        });

    reg("readLines", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("Type Error: readLines() expects a string path.");
        std::string path = safeResolvePath(std::get<std::string>(args[0].data));
        std::ifstream file(path);
        if (!file.is_open()) throw std::runtime_error("IO Error: Cannot open file '" + path + "'.");
        List L; std::string line;
        while (std::getline(file, line)) {
            jc::checkInterrupt();
            if (!line.empty() && line.back() == '\r') line.pop_back();
            L.push_back(valToAny(Value(line)));
        }
        file.close(); return Value(L);
        });

    reg("writeLines", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("Type Error: writeLines() expects a string path.");
        if (!std::holds_alternative<List>(args[1].data))
            throw std::runtime_error("Type Error: writeLines() expects a List.");
        std::string path = safeResolvePath(std::get<std::string>(args[0].data));
        const auto& L = std::get<List>(args[1].data);
        std::ofstream file(path);
        if (!file.is_open()) throw std::runtime_error("IO Error: Cannot write to file '" + path + "'.");
        for (const auto& e : L.raw()) {
            Value v = anyToVal(e);
            if (std::holds_alternative<std::string>(v.data)) file << std::get<std::string>(v.data) << "\n";
            else file << v << "\n";
        }
        file.close(); return Value::none();
        });

    reg("fileExists", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("Type Error: fileExists() expects a string path.");
        return Value(std::filesystem::exists(safeResolvePath(std::get<std::string>(args[0].data))) ? 1.0 : 0.0);
        });

    reg("deleteFile", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("Type Error: deleteFile() expects a string path.");
        std::string path = safeResolvePath(std::get<std::string>(args[0].data));
        if (!std::filesystem::exists(path))
            throw std::runtime_error("IO Error: File '" + path + "' does not exist.");
        std::filesystem::remove(path);
        return Value::none();
        });

    reg("fileSize", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("Type Error: fileSize() expects a string path.");
        std::string path = safeResolvePath(std::get<std::string>(args[0].data));
        if (!std::filesystem::exists(path))
            throw std::runtime_error("IO Error: File '" + path + "' does not exist.");
        return Value(static_cast<double>(std::filesystem::file_size(path)));
        });

    reg("listDir", { 0, 1 }, [](const std::vector<Value>& args) -> Value {
        std::string dir;
        if (args.size() == 1) {
            if (!std::holds_alternative<std::string>(args[0].data))
                throw std::runtime_error("Type Error: listDir() expects a string path.");
            dir = safeResolvePath(std::get<std::string>(args[0].data));
        }
        else {
            dir = std::filesystem::current_path().string();
        }
        if (!std::filesystem::exists(dir))
            throw std::runtime_error("IO Error: Directory '" + dir + "' does not exist.");
        List L;
        for (const auto& entry : std::filesystem::directory_iterator(dir))
            L.push_back(valToAny(Value(entry.path().filename().string())));
        return Value(L);
        });

    // --- CSV ---
    reg("readCSV", { 1, 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: readCSV() expects a string path.");
        std::string path = safeResolvePath(std::get<std::string>(args[0].data));
        std::string delim = ",";
        if (args.size() == 2) { if (!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("Type Error: readCSV() delimiter must be a string."); delim = std::get<std::string>(args[1].data); }
        std::ifstream file(path); if (!file.is_open()) throw std::runtime_error("IO Error: Cannot open file '" + path + "'.");
        List rows; std::string line;
        while (std::getline(file, line)) { jc::checkInterrupt(); if (!line.empty() && line.back() == '\r') line.pop_back(); List row; size_t pos = 0, found; while ((found = line.find(delim, pos)) != std::string::npos) { row.push_back(valToAny(Value(line.substr(pos, found - pos)))); pos = found + delim.size(); } row.push_back(valToAny(Value(line.substr(pos)))); row.freeze(); rows.push_back(valToAny(Value(row))); }
        file.close(); return Value(rows);
        });

    reg("readCSVMat", { 1, 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: readCSVMat() expects a string path.");
        std::string path = safeResolvePath(std::get<std::string>(args[0].data));
        std::string delim = ",";
        if (args.size() == 2) { if (!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("Type Error: readCSVMat() delimiter must be a string."); delim = std::get<std::string>(args[1].data); }
        std::ifstream file(path); if (!file.is_open()) throw std::runtime_error("IO Error: Cannot open file '" + path + "'.");
        std::vector<std::vector<std::string>> rowsData; std::string line; size_t maxCols = 0;
        while (std::getline(file, line)) { jc::checkInterrupt(); if (!line.empty() && line.back() == '\r') line.pop_back(); std::vector<std::string> row; size_t pos = 0, found; while ((found = line.find(delim, pos)) != std::string::npos) { row.push_back(line.substr(pos, found - pos)); pos = found + delim.size(); } row.push_back(line.substr(pos)); if (row.size() > maxCols) maxCols = row.size(); rowsData.push_back(row); }
        file.close(); std::vector<std::string> flat; for (auto& row : rowsData) { row.resize(maxCols, ""); flat.insert(flat.end(), row.begin(), row.end()); }
        return Value(StringMatrix(static_cast<int>(rowsData.size()), static_cast<int>(maxCols), flat));
        });

    reg("parseCSVNum", { 1, 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: parseCSVNum() expects a string path.");
        std::string path = safeResolvePath(std::get<std::string>(args[0].data));
        std::string delim = ",";
        if (args.size() == 2) { if (!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("Type Error: parseCSVNum() delimiter must be a string."); delim = std::get<std::string>(args[1].data); }
        std::ifstream file(path); if (!file.is_open()) throw std::runtime_error("IO Error: Cannot open file '" + path + "'.");
        std::vector<std::vector<double>> rowsData; std::string line; size_t maxCols = 0;
        while (std::getline(file, line)) { jc::checkInterrupt(); if (!line.empty() && line.back() == '\r') line.pop_back(); if (line.empty()) continue; std::vector<double> row; size_t pos = 0, found; while ((found = line.find(delim, pos)) != std::string::npos) { try { row.push_back(std::stod(line.substr(pos, found - pos))); } catch (...) { row.push_back(0.0); } pos = found + delim.size(); } try { row.push_back(std::stod(line.substr(pos))); } catch (...) { row.push_back(0.0); } if (row.size() > maxCols) maxCols = row.size(); rowsData.push_back(row); }
        file.close(); if (rowsData.empty()) return Value(RealMatrix(0, 0));
        std::vector<double> flat; for (auto& row : rowsData) { row.resize(maxCols, 0.0); flat.insert(flat.end(), row.begin(), row.end()); }
        return Value(RealMatrix(static_cast<int>(rowsData.size()), static_cast<int>(maxCols), flat));
        });

    reg("writeCSV", { 2, 3 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: writeCSV() expects a string path.");
        std::string path = safeResolvePath(std::get<std::string>(args[0].data));
        std::string delim = ",";
        if (args.size() == 3) { if (!std::holds_alternative<std::string>(args[2].data)) throw std::runtime_error("Type Error: writeCSV() delimiter must be a string."); delim = std::get<std::string>(args[2].data); }
        std::ofstream file(path); if (!file.is_open()) throw std::runtime_error("IO Error: Cannot write to file '" + path + "'.");
        if (std::holds_alternative<RealMatrix>(args[1].data)) { const auto& m = std::get<RealMatrix>(args[1].data); for (int i = 0; i < m.getRows(); ++i) { for (int j = 0; j < m.getCols(); ++j) { if (j > 0) file << delim; double val = m(i, j); double rounded = std::round(val); if (Tol::isEq(val, rounded, 1e5) && std::abs(rounded) < 1e15 && rounded == std::trunc(rounded)) file << static_cast<int64_t>(rounded); else file << val; } file << "\n"; } }
        else if (std::holds_alternative<StringMatrix>(args[1].data)) { const auto& m = std::get<StringMatrix>(args[1].data); for (int i = 0; i < m.getRows(); ++i) { for (int j = 0; j < m.getCols(); ++j) { if (j > 0) file << delim; file << m(i, j); } file << "\n"; } }
        else if (std::holds_alternative<ComplexMatrix>(args[1].data)) { const auto& m = std::get<ComplexMatrix>(args[1].data); for (int i = 0; i < m.getRows(); ++i) { for (int j = 0; j < m.getCols(); ++j) { if (j > 0) file << delim; file << m(i, j); } file << "\n"; } }
        else if (std::holds_alternative<List>(args[1].data)) { for (const auto& e : std::get<List>(args[1].data).raw()) { file << anyToVal(e) << "\n"; } }
        else throw std::runtime_error("Type Error: writeCSV() expects a matrix or list.");
        file.close(); return Value::none();
        });
}

// =================================================================
// [Phase 2] 错误处理 (pcall, isError, assert)
// =================================================================
void BuiltinRegistry::registerErrorHandling() {

    reg("pcall", { 1 }, [](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        if (!cl->acceptsArgCount(0))
            throw std::runtime_error("Runtime Error: pcall() expects a zero-parameter function.");

        try {
            Value result = safeCallFunction(cl, {});
            List L; L.push_back(valToAny(Value(1.0))); L.push_back(valToAny(result));
            L.freeze(); return Value(L);
        }
        catch (const StackTracedException& ex) {
            // ★ 完美拿到纯净的出错理由字符串！无视底下挂着的多行追踪栈
            List L; L.push_back(valToAny(Value(0.0))); L.push_back(valToAny(Value(ex.rawMessage)));
            L.freeze(); return Value(L);
        }
        catch (const ErrorSignal& sig) {
            List L; L.push_back(valToAny(Value(0.0))); L.push_back(valToAny(Value(sig.message)));
            L.freeze(); return Value(L);
        }
        catch (const std::exception& ex) {
            List L; L.push_back(valToAny(Value(0.0))); L.push_back(valToAny(Value(std::string(ex.what()))));
            L.freeze(); return Value(L);
        }
        });

    reg("isError", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<List>(args[0].data)) return Value(0.0);
        const auto& L = std::get<List>(args[0].data);
        if (L.size() != 2) return Value(0.0);
        Value first = anyToVal(L.raw()[0]);
        if (std::holds_alternative<double>(first.data))
            return Value(Tol::isEq(std::get<double>(first.data), 0.0) ? 1.0 : 0.0);
        return Value(0.0);
        });

    reg("assert", { 1, 2, 3 }, [](const std::vector<Value>& args) -> Value {
        if (args.size() == 1) {
            if (!isTruthy(args[0])) throw std::runtime_error("Assertion Failed.");
            return Value(1.0);
        }
        if (args.size() == 2) {
            if (!isTruthy(args[0])) {
                std::string msg = "Assertion Failed";
                if (std::holds_alternative<std::string>(args[1].data))
                    msg += ": " + std::get<std::string>(args[1].data);
                else { std::ostringstream oss; oss << args[1]; msg += ": " + oss.str(); }
                throw std::runtime_error(msg);
            }
            return Value(1.0);
        }
        // assert(name, got, expected)
        std::string name;
        if (std::holds_alternative<std::string>(args[0].data)) name = std::get<std::string>(args[0].data);
        else { std::ostringstream oss; oss << args[0]; name = oss.str(); }
        Value got = args[1], expected = args[2];
        // 深度比较 — 优先字符串表示，再尝试数值
        bool pass = false;
        { std::ostringstream a, b; a << got; b << expected; pass = (a.str() == b.str()); }
        if (!pass) { try { pass = got.asComplex() == expected.asComplex(); } catch (...) {} }
        if (!pass) {
            std::ostringstream oss;
            oss << "Assertion Failed: [" << name << "]\n"
                << "       Expected: " << expected << "\n"
                << "       Got:      " << got;
            throw std::runtime_error(oss.str());
        }
        return Value(1.0);
        });
}

// =================================================================
// [Phase 3] 终极系统 Shell 与绘图 (取代 Evaluator 专属函数)
// =================================================================
void BuiltinRegistry::registerSystemShell() {

    reg("resetConst", { 0 }, [](const std::vector<Value>&) -> Value {
        if (helpers::setGlobalCallback) {
            helpers::setGlobalCallback("PI", Value(3.14159265358979323846));
            helpers::setGlobalCallback("E", Value(2.71828182845904523536));
            helpers::setGlobalCallback("i", Value(Complex(0.0, 1.0)));
            helpers::setGlobalCallback("I", Value(Complex(0.0, 1.0)));
            helpers::setGlobalCallback("true", Value(1.0));
            helpers::setGlobalCallback("false", Value(0.0));
            helpers::setGlobalCallback("none", Value::none());
        }
        std::cout << "System constants restored: PI, E, i, I, true, false" << std::endl;
        return Value::none();
        });

    reg("setWorkspace", { 1 }, [](const std::vector<Value>& args) -> Value {
        std::string p = std::get<std::string>(args[0].data);
        if (p == "default") {
            g_workspacePath = "";
        }
        else {
            namespace fs = std::filesystem;
            fs::path dir(p);
            if (!dir.is_absolute()) dir = fs::path(g_cwd()) / dir;
            if (!fs::exists(dir)) fs::create_directories(dir);
            g_workspacePath = fs::weakly_canonical(dir).string();
        }
        std::cout << "[System] Workspace set to: " << (g_workspacePath.empty() ? "./data" : g_workspacePath) << std::endl;
        return Value::none();
        });

    reg("getWorkspace", { 0 }, [](const std::vector<Value>&) -> Value {
        return Value(g_workspacePath.empty() ? (std::filesystem::current_path() / "data").string() : g_workspacePath);
        });

    reg("pwd", { 0 }, [](const std::vector<Value>&) -> Value {
        std::cout << "  Script dir:    " << g_cwd() << std::endl;
        std::cout << "  Workspace dir: " << (g_workspacePath.empty() ? (std::filesystem::current_path() / "data").string() : g_workspacePath) << std::endl;
        return Value::none();
        });

    reg("modules", { 0 }, [](const std::vector<Value>&) -> Value {
        auto& mods = jc::getNativeModules();
        if (mods.empty()) { std::cout << "  No native modules available.\n"; return Value::none(); }
        std::cout << "  Available native modules:\n";
        for (const auto& [m, val] : mods) std::cout << "    " << m << "\n";
        return Value::none();
        });

    reg("run", { 1 }, [](const std::vector<Value>& args) -> Value {
        std::string filepath = std::get<std::string>(args[0].data);
        std::string resolved = helpers::safeResolvePath(filepath);
        if (!std::filesystem::exists(resolved)) resolved = helpers::safeResolvePath(filepath + ".jc2");
        if (!std::filesystem::exists(resolved)) throw std::runtime_error("IO Error: Cannot open script '" + filepath + "'.");

        std::ifstream file(resolved);
        if (!file.is_open()) throw std::runtime_error("IO Error: Cannot read script.");
        std::string code, line;
        while (std::getline(file, line)) code += line + "\n";
        file.close();

        helpers::g_scriptDirStack.push_back(std::filesystem::path(resolved).parent_path().string());
        Value result = Value::none();
        if (helpers::evalCallback) result = helpers::evalCallback(code);
        helpers::g_scriptDirStack.pop_back();

        return result;
        });

    reg("imgPlot", { 7, 8 }, [](const std::vector<Value>& args) -> Value {
        auto inst = std::get<std::shared_ptr<Instance>>(args[0].data);
        auto& im = std::any_cast<std::shared_ptr<Image>&>(inst->nativeData);
        auto fn = args[0].asFunction(); // 修正：应该是 args[1]
        auto fn_actual = args[1].asFunction();
        double xMin = args[2].asDouble(), xMax = args[3].asDouble();
        double yMin = args[4].asDouble(), yMax = args[5].asDouble();
        Color c = Color::parse(std::get<std::string>(args[6].data));
        int thick = (args.size() == 8) ? static_cast<int>(std::round(args[7].asDouble())) : 2;
        int plotW = im->width() - 50;
        int prevPx = -1, prevPy = -1;

        for (int px = 0; px <= plotW; ++px) {
            jc::checkInterrupt();
            double x = xMin + (static_cast<double>(px) / plotW) * (xMax - xMin);
            double y = 0;
            try { y = helpers::safeCallFunction(fn_actual, { Value(x) }).asDouble(); }
            catch (...) { prevPx = -1; prevPy = -1; continue; }
            int screenX = im->mapPlotX(x, xMin, xMax);
            int screenY = im->mapPlotY(y, yMin, yMax);
            if (prevPx >= 0 && std::abs(screenY - prevPy) < im->height())
                im->line(prevPx, prevPy, screenX, screenY, c, thick);
            prevPx = screenX; prevPy = screenY;
        }
        return args[0];
        });

    reg("breakpoint", { 0 }, [](const std::vector<Value>&) -> Value {
        if (VM::activeVM) {
            VM::activeVM->triggerDebugger();
        }
        return Value::none();
        });
    // 做个兼容别名
    builtins["debugger"] = builtins["breakpoint"];
    builtinArity["debugger"] = builtinArity["breakpoint"];
}

// =================================================================
// [Phase 3] 类型与字符串谓词函数
// =================================================================
void BuiltinRegistry::registerTypeChecks() {

    // ═══ 数值类型谓词 ═══

    reg("isint", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<BigInt>(args[0].data)) return Value(1.0);
        if (std::holds_alternative<double>(args[0].data)) {
            double v = std::get<double>(args[0].data);
            return Value(std::isfinite(v) && v == std::floor(v) ? 1.0 : 0.0);
        }
        if (std::holds_alternative<Fraction>(args[0].data))
            return Value(std::get<Fraction>(args[0].data).getDen() == BigInt(1) ? 1.0 : 0.0);
        return Value(0.0);
        });

    reg("isfloat", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(std::holds_alternative<double>(args[0].data) ? 1.0 : 0.0);
        });

    reg("isnumeric", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value((std::holds_alternative<double>(args[0].data) ||
            std::holds_alternative<BigInt>(args[0].data) ||
            std::holds_alternative<Fraction>(args[0].data) ||
            std::holds_alternative<Complex>(args[0].data) ||
            std::holds_alternative<BaseNum>(args[0].data)) ? 1.0 : 0.0);
        });

    reg("iscomplex", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<Complex>(args[0].data)) return Value(1.0);
        // double/BigInt/Fraction 在数学意义上也是复数（虚部为 0）
        return Value(0.0);
        });

    reg("isreal", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<double>(args[0].data) ||
            std::holds_alternative<BigInt>(args[0].data) ||
            std::holds_alternative<Fraction>(args[0].data) ||
            std::holds_alternative<BaseNum>(args[0].data))
            return Value(1.0);
        if (std::holds_alternative<Complex>(args[0].data))
            return Value(Tol::isEq(std::get<Complex>(args[0].data).imag, 0.0) ? 1.0 : 0.0);
        return Value(0.0);
        });

    reg("isfrac", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(std::holds_alternative<Fraction>(args[0].data) ? 1.0 : 0.0);
        });

    reg("isbigint", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(std::holds_alternative<BigInt>(args[0].data) ? 1.0 : 0.0);
        });

    reg("isbase", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(std::holds_alternative<BaseNum>(args[0].data) ? 1.0 : 0.0);
        });

    // ═══ 容器类型谓词 ═══

    reg("ismatrix", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value((std::holds_alternative<RealMatrix>(args[0].data) ||
            std::holds_alternative<ComplexMatrix>(args[0].data) ||
            std::holds_alternative<StringMatrix>(args[0].data)) ? 1.0 : 0.0);
        });

    reg("isrealmat", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(std::holds_alternative<RealMatrix>(args[0].data) ? 1.0 : 0.0);
        });

    reg("iscomplexmat", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(std::holds_alternative<ComplexMatrix>(args[0].data) ? 1.0 : 0.0);
        });

    reg("isstringmat", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(std::holds_alternative<StringMatrix>(args[0].data) ? 1.0 : 0.0);
        });

    reg("isvector", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) {
            const auto& m = std::get<RealMatrix>(args[0].data);
            return Value((m.getRows() == 1 || m.getCols() == 1) ? 1.0 : 0.0);
        }
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) {
            const auto& m = std::get<ComplexMatrix>(args[0].data);
            return Value((m.getRows() == 1 || m.getCols() == 1) ? 1.0 : 0.0);
        }
        return Value(0.0);
        });

    reg("issquare", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data))
            return Value(std::get<RealMatrix>(args[0].data).getRows() == std::get<RealMatrix>(args[0].data).getCols() ? 1.0 : 0.0);
        if (std::holds_alternative<ComplexMatrix>(args[0].data))
            return Value(std::get<ComplexMatrix>(args[0].data).getRows() == std::get<ComplexMatrix>(args[0].data).getCols() ? 1.0 : 0.0);
        return Value(0.0);
        });

    reg("islist", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(std::holds_alternative<List>(args[0].data) ? 1.0 : 0.0);
        });

    reg("isdict", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(std::holds_alternative<Dict>(args[0].data) ? 1.0 : 0.0);
        });

    // ═══ 字符串谓词 ═══

    reg("isstring", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(std::holds_alternative<std::string>(args[0].data) ? 1.0 : 0.0);
        });

    reg("isalpha", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data)) return Value(0.0);
        const auto& s = std::get<std::string>(args[0].data);
        if (s.empty()) return Value(0.0);
        for (char c : s) if (!std::isalpha(static_cast<unsigned char>(c))) return Value(0.0);
        return Value(1.0);
        });

    reg("isdigit", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data)) return Value(0.0);
        const auto& s = std::get<std::string>(args[0].data);
        if (s.empty()) return Value(0.0);
        for (char c : s) if (!std::isdigit(static_cast<unsigned char>(c))) return Value(0.0);
        return Value(1.0);
        });

    reg("isalnum", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data)) return Value(0.0);
        const auto& s = std::get<std::string>(args[0].data);
        if (s.empty()) return Value(0.0);
        for (char c : s) if (!std::isalnum(static_cast<unsigned char>(c))) return Value(0.0);
        return Value(1.0);
        });

    reg("isspace", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data)) return Value(0.0);
        const auto& s = std::get<std::string>(args[0].data);
        if (s.empty()) return Value(0.0);
        for (char c : s) if (!std::isspace(static_cast<unsigned char>(c))) return Value(0.0);
        return Value(1.0);
        });

    reg("isupper", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data)) return Value(0.0);
        const auto& s = std::get<std::string>(args[0].data);
        if (s.empty()) return Value(0.0);
        for (char c : s) if (std::isalpha(static_cast<unsigned char>(c)) && !std::isupper(static_cast<unsigned char>(c))) return Value(0.0);
        return Value(1.0);
        });

    reg("islower", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data)) return Value(0.0);
        const auto& s = std::get<std::string>(args[0].data);
        if (s.empty()) return Value(0.0);
        for (char c : s) if (std::isalpha(static_cast<unsigned char>(c)) && !std::islower(static_cast<unsigned char>(c))) return Value(0.0);
        return Value(1.0);
        });

    reg("isempty", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<std::string>(args[0].data))
            return Value(std::get<std::string>(args[0].data).empty() ? 1.0 : 0.0);
        if (std::holds_alternative<List>(args[0].data))
            return Value(std::get<List>(args[0].data).empty() ? 1.0 : 0.0);
        if (std::holds_alternative<Dict>(args[0].data))
            return Value(std::get<Dict>(args[0].data).empty() ? 1.0 : 0.0);
        if (std::holds_alternative<RealMatrix>(args[0].data)) {
            const auto& m = std::get<RealMatrix>(args[0].data);
            return Value((m.getRows() == 0 || m.getCols() == 0) ? 1.0 : 0.0);
        }
        if (std::holds_alternative<Set>(args[0].data))
            return Value(std::get<Set>(args[0].data).empty() ? 1.0 : 0.0);
        return Value(0.0);
        });

    // ═══ 特殊谓词 ═══

    reg("isnone", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(args[0].isNone() ? 1.0 : 0.0);
        });

    reg("isfunction", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(std::holds_alternative<std::shared_ptr<FunctionClosure>>(args[0].data) ? 1.0 : 0.0);
        });

    reg("isclass", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(std::holds_alternative<std::shared_ptr<ClassDefinition>>(args[0].data) ? 1.0 : 0.0);
        });

    reg("issym", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(args[0].isSymbolic() ? 1.0 : 0.0);
        });

    reg("isnan", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<double>(args[0].data))
            return Value(std::isnan(std::get<double>(args[0].data)) ? 1.0 : 0.0);
        return Value(0.0);
        });

    reg("isinf", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<double>(args[0].data))
            return Value(std::isinf(std::get<double>(args[0].data)) ? 1.0 : 0.0);
        return Value(0.0);
        });

    reg("isfinite", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<double>(args[0].data))
            return Value(std::isfinite(std::get<double>(args[0].data)) ? 1.0 : 0.0);
        // BigInt/Fraction 永远是有限的
        if (std::holds_alternative<BigInt>(args[0].data) ||
            std::holds_alternative<Fraction>(args[0].data))
            return Value(1.0);
        return Value(0.0);
        });

    reg("isprime", { 1 }, [](const std::vector<Value>& args) -> Value {
        // 别名包装（与 isPrime 行为一致，小写风格）
        BigInt n = args[0].isBigInt() ? std::get<BigInt>(args[0].data)
            : BigInt(static_cast<int64_t>(std::round(args[0].asDouble())));
        return Value(n.isPrime() ? 1.0 : 0.0);
        });

    reg("iseven", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<BigInt>(args[0].data))
            return Value((std::get<BigInt>(args[0].data) % BigInt(2)).isZero() ? 1.0 : 0.0);
        if (std::holds_alternative<double>(args[0].data)) {
            double v = std::get<double>(args[0].data);
            return Value((std::isfinite(v) && v == std::floor(v) && std::fmod(v, 2.0) == 0.0) ? 1.0 : 0.0);
        }
        return Value(0.0);
        });

    reg("isodd", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<BigInt>(args[0].data))
            return Value(!(std::get<BigInt>(args[0].data) % BigInt(2)).isZero() ? 1.0 : 0.0);
        if (std::holds_alternative<double>(args[0].data)) {
            double v = std::get<double>(args[0].data);
            return Value((std::isfinite(v) && v == std::floor(v) && std::fmod(v, 2.0) != 0.0) ? 1.0 : 0.0);
        }
        return Value(0.0);
        });

    reg("ispositive", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<double>(args[0].data))
            return Value(std::get<double>(args[0].data) > 0.0 ? 1.0 : 0.0);
        if (std::holds_alternative<BigInt>(args[0].data))
            return Value((!std::get<BigInt>(args[0].data).isZero() && !std::get<BigInt>(args[0].data).isNegative()) ? 1.0 : 0.0);
        if (std::holds_alternative<Fraction>(args[0].data))
            return Value(std::get<Fraction>(args[0].data).toDouble() > 0.0 ? 1.0 : 0.0);
        return Value(0.0);
        });

    reg("isnegative", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<double>(args[0].data))
            return Value(std::get<double>(args[0].data) < 0.0 ? 1.0 : 0.0);
        if (std::holds_alternative<BigInt>(args[0].data))
            return Value(std::get<BigInt>(args[0].data).isNegative() ? 1.0 : 0.0);
        if (std::holds_alternative<Fraction>(args[0].data))
            return Value(std::get<Fraction>(args[0].data).toDouble() < 0.0 ? 1.0 : 0.0);
        return Value(0.0);
        });

    reg("iszero", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<double>(args[0].data))
            return Value(Tol::isEq(std::get<double>(args[0].data), 0.0) ? 1.0 : 0.0);
        if (std::holds_alternative<BigInt>(args[0].data))
            return Value(std::get<BigInt>(args[0].data).isZero() ? 1.0 : 0.0);
        if (std::holds_alternative<Complex>(args[0].data))
            return Value(Tol::isEq(std::get<Complex>(args[0].data).modulus(), 0.0) ? 1.0 : 0.0);
        if (std::holds_alternative<Fraction>(args[0].data))
            return Value(std::get<Fraction>(args[0].data).getNum().isZero() ? 1.0 : 0.0);
        return Value(0.0);
        });

    reg("isset", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(std::holds_alternative<Set>(args[0].data) ? 1.0 : 0.0);
        });
}

// =================================================================
// [Set] 无序去重集合
// =================================================================
void BuiltinRegistry::registerSetFunctions() {

    // ═══ 构造 ═══
    reg("Set", {}, [](const std::vector<Value>& args) -> Value {
        Set s;
        for (const auto& a : args) {
            s.insert(a);
        }
        return Value(s);
        });

    reg("toSet", { 1 }, [](const std::vector<Value>& args) -> Value {
        Set s;
        if (std::holds_alternative<Set>(args[0].data)) return args[0];
        if (std::holds_alternative<List>(args[0].data)) {
            for (const auto& e : std::get<List>(args[0].data).raw()) {
                Value v = anyToVal(e);
                s.insert(v);
            }
        }
        else if (std::holds_alternative<RealMatrix>(args[0].data)) {
            for (double d : std::get<RealMatrix>(args[0].data).rawData()) {
                Value v(d);
                s.insert(v);
            }
        }
        else if (std::holds_alternative<ComplexMatrix>(args[0].data)) {
            for (const auto& c : std::get<ComplexMatrix>(args[0].data).rawData()) {
                Value v(c);
                s.insert(v);
            }
        }
        else if (std::holds_alternative<std::string>(args[0].data)) {
            for (char c : std::get<std::string>(args[0].data)) {
                Value v(std::string(1, c));
                s.insert(v);
            }
        }
        else {
            throw std::runtime_error("Type Error: toSet() expects a list, array, string, or set.");
        }
        return Value(s);
        });

    // ═══ 元素操作（引用语义，原地修改）═══
    reg("setAdd", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<Set>(args[0].data))
            throw std::runtime_error("Type Error: setAdd() expects a Set.");
        Set s = std::get<Set>(args[0].data);  // shared_ptr copy → same underlying data
        s.insert(args[1]);
        return Value(s);
        });

    reg("setRemove", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<Set>(args[0].data))
            throw std::runtime_error("Type Error: setRemove() expects a Set.");
        Set s = std::get<Set>(args[0].data);
        if (!s.erase(args[1]))
            throw std::runtime_error("Runtime Error: Element not found in Set.");
        return Value(s);
        });

    reg("setDiscard", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<Set>(args[0].data))
            throw std::runtime_error("Type Error: setDiscard() expects a Set.");
        Set s = std::get<Set>(args[0].data);
        s.erase(args[1]);  // 静默忽略不存在的元素
        return Value(s);
        });

    reg("setClear", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<Set>(args[0].data))
            throw std::runtime_error("Type Error: setClear() expects a Set.");
        Set s = std::get<Set>(args[0].data);  // shared_ptr 浅拷贝，指向同一块数据
        s.clear();
        return Value(s);
        });

    reg("setPop", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<Set>(args[0].data))
            throw std::runtime_error("Type Error: setPop() expects a Set.");
        Set s = std::get<Set>(args[0].data);
        if (s.empty()) throw std::runtime_error("Runtime Error: setPop() on empty Set.");
        const auto& back = s.raw().back();
        Value result = anyToVal(back.second);
        s.erase(result);
        return result;
        });

    // ═══ 集合运算（返回新 Set）═══
    reg("setUnion", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<Set>(args[0].data) || !std::holds_alternative<Set>(args[1].data))
            throw std::runtime_error("Type Error: setUnion() expects two Sets.");
        const auto& a = std::get<Set>(args[0].data);
        const auto& b = std::get<Set>(args[1].data);
        Set result;
        for (const auto& [key, val] : a.raw()) result.insert(val);
        for (const auto& [key, val] : b.raw()) result.insert(val);
        return Value(result);
        });

    reg("setIntersect", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<Set>(args[0].data) || !std::holds_alternative<Set>(args[1].data))
            throw std::runtime_error("Type Error: setIntersect() expects two Sets.");
        const auto& a = std::get<Set>(args[0].data);
        const auto& b = std::get<Set>(args[1].data);
        Set result;
        for (const auto& [key, val] : a.raw()) {
            if (b.contains(val)) result.insert(val);
        }
        return Value(result);
        });

    reg("setDiff", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<Set>(args[0].data) || !std::holds_alternative<Set>(args[1].data))
            throw std::runtime_error("Type Error: setDiff() expects two Sets.");
        const auto& a = std::get<Set>(args[0].data);
        const auto& b = std::get<Set>(args[1].data);
        Set result;
        for (const auto& [key, val] : a.raw()) {
            if (!b.contains(val)) result.insert(val);
        }
        return Value(result);
        });

    reg("setSymDiff", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<Set>(args[0].data) || !std::holds_alternative<Set>(args[1].data))
            throw std::runtime_error("Type Error: setSymDiff() expects two Sets.");
        const auto& a = std::get<Set>(args[0].data);
        const auto& b = std::get<Set>(args[1].data);
        Set result;
        for (const auto& [key, val] : a.raw()) {
            if (!b.contains(val)) result.insert(val);
        }
        for (const auto& [key, val] : b.raw()) {
            if (!a.contains(val)) result.insert(val);
        }
        return Value(result);
        });

    // ═══ 集合关系谓词 ═══
    reg("isSubset", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<Set>(args[0].data) || !std::holds_alternative<Set>(args[1].data))
            throw std::runtime_error("Type Error: isSubset() expects two Sets.");
        const auto& a = std::get<Set>(args[0].data);
        const auto& b = std::get<Set>(args[1].data);
        for (const auto& [key, val] : a.raw()) {
            if (!b.contains(val)) return Value(0.0);
        }
        return Value(1.0);
        });

    reg("isSuperset", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<Set>(args[0].data) || !std::holds_alternative<Set>(args[1].data))
            throw std::runtime_error("Type Error: isSuperset() expects two Sets.");
        const auto& a = std::get<Set>(args[0].data);
        const auto& b = std::get<Set>(args[1].data);
        for (const auto& [key, val] : b.raw()) {
            if (!a.contains(val)) return Value(0.0);
        }
        return Value(1.0);
        });

    reg("isDisjoint", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<Set>(args[0].data) || !std::holds_alternative<Set>(args[1].data))
            throw std::runtime_error("Type Error: isDisjoint() expects two Sets.");
        const auto& a = std::get<Set>(args[0].data);
        const auto& b = std::get<Set>(args[1].data);
        for (const auto& [key, val] : a.raw()) {
            if (b.contains(val)) return Value(0.0);
        }
        return Value(1.0);
        });

    // ═══ 笛卡尔积 ═══
    reg("setProduct", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<Set>(args[0].data) || !std::holds_alternative<Set>(args[1].data))
            throw std::runtime_error("Type Error: setProduct() expects two Sets.");
        // 直接触发刚写好的重载 *
        return args[0] * args[1];
        });

    // ═══ 集合幂集 ═══
    reg("setPow", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<Set>(args[0].data))
            throw std::runtime_error("Type Error: setPow() expects a Set.");

        const auto& s = std::get<Set>(args[0].data);
        int n = static_cast<int>(s.size());
        if (n > 20)
            throw std::runtime_error("Math Error: Set size too large for powerset (max 20 elements).");

        Set result;
        int limit = 1 << n;  // 2^n
        const auto& raw = s.raw();

        for (int mask = 0; mask < limit; ++mask) {
            jc::checkInterrupt();
            Set sub;
            for (int i = 0; i < n; ++i) {
                if (mask & (1 << i)) {
                    sub.insert(raw[i].second);
                }
            }
            sub.freeze();
            Value subVal(sub);
            result.insert(subVal);
        }
        return Value(result);
        });
}

// =================================================================
// [CAS] 符号计算与计算机代数系统
// =================================================================
void BuiltinRegistry::registerCAS() {
    auto* fnsPtr = &builtins;

    auto toBigInt = [](const Value& v) -> BigInt {
        return v.isBigInt() ? std::get<BigInt>(v.data) : BigInt(static_cast<int64_t>(std::round(v.asDouble())));
    };

    auto evalFunc = [](const std::shared_ptr<FunctionClosure>& cl, double x) -> double {
        return safeCallFunction(cl, { Value(x) }).asDouble();
    };

    auto getVarName = [](const Value& v, const std::string& funcName) -> std::string {
        if (std::holds_alternative<std::string>(v.data)) return std::get<std::string>(v.data);
        if (v.isSymbolic() && v.asSymbolic().ptr->getType() == SymType::VAR) return std::static_pointer_cast<SymVar>(v.asSymbolic().ptr)->name;
        throw std::runtime_error("TypeError: " + funcName + "() expects a variable name (string or symbol).");
    };

    reg("sym", { 1 }, [getVarName](const std::vector<Value>& args) -> Value {
        return Value(SymExpr::makeVar(getVarName(args[0], "sym")));
        });

    reg("RootOf", { 3 }, [getVarName](const std::vector<Value>& args) -> Value {
        SymExpr poly = args[0].asSymbolic();
        std::string var = getVarName(args[1], "RootOf");
        SymExpr k = args[2].asSymbolic();
        return Value(SymExpr(std::make_shared<SymFunc>("RootOf", std::vector<std::shared_ptr<SymNode>>{
            poly.ptr, SymExpr::makeVar(var).ptr, k.ptr
        })));
        });

    reg("RootSum", { 3 }, [getVarName](const std::vector<Value>& args) -> Value {
        SymExpr expr = args[0].asSymbolic();
        std::string var = getVarName(args[1], "RootSum");
        SymExpr poly = args[2].asSymbolic();
        return Value(SymExpr(std::make_shared<SymFunc>("RootSum", std::vector<std::shared_ptr<SymNode>>{
            expr.ptr, SymExpr::makeVar(var).ptr, poly.ptr
        })));
        });

    reg("expand", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(expand(args[0].asSymbolic()));
        });

    reg("simplify", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(full_simplify(args[0].asSymbolic()));
        });

    reg("contract", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(contract(args[0].asSymbolic()));
        });

    reg("trigsimp", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(trigsimp(args[0].asSymbolic()));
        });

    reg("subs", { 3 }, [fnsPtr](const std::vector<Value>& args) -> Value {
        SymExpr result = args[0].asSymbolic();

        std::vector<std::string> vars;
        std::vector<SymExpr> vals;

        if (std::holds_alternative<std::string>(args[1].data)) {
            vars.push_back(std::get<std::string>(args[1].data));
        }
        else if (args[1].isSymbolic() && args[1].asSymbolic().ptr->getType() == SymType::VAR) {
            vars.push_back(std::static_pointer_cast<SymVar>(args[1].asSymbolic().ptr)->name);
        }
        else if (std::holds_alternative<StringMatrix>(args[1].data)) {
            const auto& sm = std::get<StringMatrix>(args[1].data);
            for (int i = 0; i < sm.getRows(); ++i)
                for (int j = 0; j < sm.getCols(); ++j)
                    vars.push_back(sm(i, j));
        }
        else if (std::holds_alternative<List>(args[1].data)) {
            const auto& lst = std::get<List>(args[1].data);
            for (size_t i = 0; i < lst.size(); ++i) {
                Value v = lst.raw()[i];
                if (std::holds_alternative<std::string>(v.data)) {
                    vars.push_back(std::get<std::string>(v.data));
                } else if (v.isSymbolic() && v.asSymbolic().ptr->getType() == SymType::VAR) {
                    vars.push_back(std::static_pointer_cast<SymVar>(v.asSymbolic().ptr)->name);
                } else {
                    throw std::runtime_error("TypeError: subs() variable list must contain strings or symbols.");
                }
            }
        }
        else {
            throw std::runtime_error("TypeError: subs() second argument must be a string, symbol, string matrix, or list.");
        }

        if (vars.size() == 1) {
            vals.push_back(args[2].asSymbolic());
        }
        else if (std::holds_alternative<RealMatrix>(args[2].data)) {
            const auto& rm = std::get<RealMatrix>(args[2].data);
            for (int i = 0; i < rm.getRows(); ++i)
                for (int j = 0; j < rm.getCols(); ++j)
                    vals.push_back(SymExpr(rm(i, j)));
        }
        else if (std::holds_alternative<ComplexMatrix>(args[2].data)) {
            const auto& cm = std::get<ComplexMatrix>(args[2].data);
            for (int i = 0; i < cm.getRows(); ++i)
                for (int j = 0; j < cm.getCols(); ++j)
                    vals.push_back(SymExpr(cm(i, j)));
        }
        else if (std::holds_alternative<List>(args[2].data)) {
            const auto& lst = std::get<List>(args[2].data);
            for (size_t i = 0; i < lst.size(); ++i) {
                Value v = lst.raw()[i];
                vals.push_back(v.asSymbolic());
            }
        }
        else {
            throw std::runtime_error("TypeError: subs() third argument must be a value, matrix, or list.");
        }

        if (vars.size() != vals.size())
            throw std::runtime_error("TypeError: subs() variable count (" + std::to_string(vars.size()) +
                ") and value count (" + std::to_string(vals.size()) + ") must match.");

        for (size_t i = 0; i < vars.size(); ++i)
            result = subs(result, vars[i], vals[i]);

        result = simplify(collapseSymFuncs(result, *fnsPtr));

        if (result.ptr->getType() == SymType::NUM) {
            auto num = std::static_pointer_cast<SymNum>(result.ptr);
            return casValToValue(num->value);
        }
        return Value(result);
        });

    reg("toFunc", { 2 }, [](const std::vector<Value>& args) -> Value {
        jc::SymExpr ast = args[0].asSymbolic();
        std::vector<std::string> varNames;
        if (std::holds_alternative<jc::List>(args[1].data)) {
            for (const auto& anyVar : std::get<jc::List>(args[1].data).raw()) {
                jc::Value v = anyVar;
                if (std::holds_alternative<std::string>(v.data)) {
                    varNames.push_back(std::get<std::string>(v.data));
                } else if (v.isSymbolic() && v.asSymbolic().ptr->getType() == SymType::VAR) {
                    varNames.push_back(std::static_pointer_cast<SymVar>(v.asSymbolic().ptr)->name);
                } else {
                    throw std::runtime_error("toFunc: Variable names must be strings or symbols.");
                }
            }
        }
        else if (std::holds_alternative<jc::StringMatrix>(args[1].data)) {
            for (const auto& s : std::get<jc::StringMatrix>(args[1].data).rawData()) {
                varNames.push_back(s);
            }
        }
        else if (std::holds_alternative<std::string>(args[1].data)) {
            varNames.push_back(std::get<std::string>(args[1].data));
        }
        else if (args[1].isSymbolic() && args[1].asSymbolic().ptr->getType() == SymType::VAR) {
            varNames.push_back(std::static_pointer_cast<SymVar>(args[1].asSymbolic().ptr)->name);
        }
        else {
            throw std::runtime_error("toFunc(): 2nd argument must be a string, symbol, List, or StringMatrix of variable names.");
        }
        int argCount = static_cast<int>(varNames.size());
        std::vector<bool> pRefs(argCount, false);

        auto cls = std::make_shared<jc::FunctionClosure>(
            varNames, pRefs, "<sym_to_func>", nullptr
        );
        cls->defaultValues.resize(argCount, jc::Value::none());
        jc::SymbolicFuncResolver resolver = [](const std::string& name, const std::vector<jc::Value>& fnArgs) -> jc::Value {
            if (!jc::VM::activeVM) throw std::runtime_error("toFunc error: VM context lost.");

            const auto& builtins = jc::VM::activeVM->getNativeBuiltins();
            auto it = builtins.find(name);
            if (it != builtins.end()) {
                return it->second(fnArgs);
            }
            throw std::runtime_error("toFunc error: Math function '" + name + "' not found in BuiltinRegistry.");
            };
        auto jc_caller = [ast, varNames, resolver](const std::vector<jc::Value>& call_args) -> jc::Value {
            if (call_args.size() != varNames.size()) {
                throw std::runtime_error("Compiled function expects " + std::to_string(varNames.size()) + " arguments.");
            }
            bool isPureReal = true;
            for (const auto& arg : call_args) {
                if (arg.isComplex() || std::holds_alternative<jc::RealMatrix>(arg.data) ||
                    std::holds_alternative<jc::ComplexMatrix>(arg.data) ||
                    std::holds_alternative<jc::StringMatrix>(arg.data)) {
                    isPureReal = false;
                    break;
                }
            }
            if (isPureReal) {
                std::map<std::string, double> env;
                for (size_t i = 0; i < varNames.size(); ++i) env[varNames[i]] = call_args[i].asDouble();
                return jc::Value(jc::fastEval(ast.ptr, env, resolver));
            }

            std::map<std::string, jc::Value> valEnv;
            for (size_t i = 0; i < varNames.size(); ++i) valEnv[varNames[i]] = call_args[i];

            return jc::evalUniversal(ast.ptr, valEnv, resolver);
            };
        cls->nativeFn = std::make_any<jc::NativeCallable>(std::move(jc_caller));
        return jc::Value(cls);
        });

    reg("evalf", { 1 }, [fnsPtr](const std::vector<Value>& args) -> Value {
        SymExpr expr = args[0].asSymbolic();
        expr = evalFloat(expr);
        expr = collapseSymFuncs(expr, *fnsPtr);

        if (isConstantExpr(expr)) {
            try {
                std::map<std::string, Value> emptyEnv;
                SymbolicFuncResolver resolver = [fnsPtr](const std::string& name, const std::vector<Value>& fnArgs) -> Value {
                    auto it = fnsPtr->find(name);
                    if (it != fnsPtr->end()) return it->second(fnArgs);
                    throw std::runtime_error("Function not found");
                };
                return evalUniversal(expr.ptr, emptyEnv, resolver);
            } catch (...) {}
        }
        return Value(expr);
        });

    reg("evalv", { 1 }, [fnsPtr](const std::vector<Value>& args) -> Value {
        SymExpr expr = args[0].asSymbolic();
        expr = evalValue(expr);
        expr = simplify(collapseSymFuncs(expr, *fnsPtr));

        if (isConstantExpr(expr)) {
            try {
                std::map<std::string, Value> emptyEnv;
                SymbolicFuncResolver resolver = [fnsPtr](const std::string& name, const std::vector<Value>& fnArgs) -> Value {
                    auto it = fnsPtr->find(name);
                    if (it != fnsPtr->end()) return it->second(fnArgs);
                    throw std::runtime_error("Function not found");
                };
                return evalUniversal(expr.ptr, emptyEnv, resolver);
            } catch (...) {}
        }
        return Value(expr);
        });

    reg("replaceRule", { 3 }, [](const std::vector<Value>& args) -> Value {
        SymExpr expr = args[0].asSymbolic();
        SymExpr pattern = args[1].asSymbolic();
        SymExpr target = args[2].asSymbolic();
        return Value(jc::simplify(jc::applyRule(expr, pattern, target)));
    });

    reg("solveEq", { 2 }, [getVarName](const std::vector<Value>& args) -> Value {
        auto roots = jc::solveEq(args[0].asSymbolic(), getVarName(args[1], "solveEq"));
        List L;
        for (const auto& r : roots) L.push_back(valToAny(Value(r)));
        return Value(L);
    });

    reg("polyDiv", { 3 }, [getVarName](const std::vector<Value>& args) -> Value {
        auto [q, r] = jc::polyDiv(args[0].asSymbolic(), args[1].asSymbolic(), getVarName(args[2], "polyDiv"));
        List L;
        L.push_back(valToAny(Value(q)));
        L.push_back(valToAny(Value(r)));
        L.freeze();
        return Value(L);
    });

    reg("polyGCD", { 3 }, [getVarName](const std::vector<Value>& args) -> Value {
        return Value(jc::polyGCD(args[0].asSymbolic(), args[1].asSymbolic(), getVarName(args[2], "polyGCD")));
    });

    reg("resultant", { 3 }, [getVarName](const std::vector<Value>& args) -> Value {
        return Value(jc::polyResultant(args[0].asSymbolic(), args[1].asSymbolic(), getVarName(args[2], "resultant")));
    });

    reg("factor", { 1 }, [toBigInt](const std::vector<Value>& args) -> Value {
        if (args[0].isSymbolic()) {
            return Value(jc::factor(args[0].asSymbolic()));
        }
        auto factors = toBigInt(args[0]).factorize();
        int r = static_cast<int>(factors.size());
        std::vector<double> flat;
        for (const auto& f : factors) {
            flat.push_back(f.first.toDouble());
            flat.push_back(static_cast<double>(f.second));
        }
        return Value(RealMatrix(r, 2, flat));
        });

    reg("factorReal", { 1 }, [](const std::vector<Value>& args) -> Value {
        return Value(jc::factorReal(args[0].asSymbolic()));
        });

    reg("taylor", { 3, 4 }, [getVarName](const std::vector<Value>& args) -> Value {
        int order = 5;
        if (args.size() == 4) order = static_cast<int>(std::round(args[3].asDouble()));
        return Value(jc::taylor(args[0].asSymbolic(), getVarName(args[1], "taylor"), args[2].asSymbolic(), order));
    });

    reg("limit", { 2, 3 }, [evalFunc, getVarName](const std::vector<Value>& args) -> Value {
        bool isSymLimit = args[0].isSymbolic();
        if (!isSymLimit && args.size() == 3) {
            if (std::holds_alternative<std::string>(args[1].data) || (args[1].isSymbolic() && args[1].asSymbolic().ptr->getType() == SymType::VAR)) {
                isSymLimit = true;
            }
        }
        if (isSymLimit) {
            if (args.size() != 3) throw std::runtime_error("TypeError: Symbolic limit expects 3 arguments: expr, var, val.");
            return Value(jc::limit(args[0].asSymbolic(), getVarName(args[1], "limit"), args[2].asSymbolic()));
        }

        auto cl = args[0].asFunction();
        double x0 = args[1].asDouble();
        double h = 1e-7;
        
        auto safeEval = [&](double x) -> double {
            try { return evalFunc(cl, x); }
            catch (...) { return std::numeric_limits<double>::quiet_NaN(); }
        };

        if (args.size() == 3) {
            double dir = args[2].asDouble();
            if (dir > 0) {
                double v = safeEval(x0 + h);
                if (std::isnan(v)) throw std::runtime_error("Math Error: Right limit does not exist.");
                return Value(v);
            }
            if (dir < 0) {
                double v = safeEval(x0 - h);
                if (std::isnan(v)) throw std::runtime_error("Math Error: Left limit does not exist.");
                return Value(v);
            }
        }
        
        double left = safeEval(x0 - h);
        double right = safeEval(x0 + h);
        
        if (!std::isnan(left) && !std::isnan(right) && std::abs(left - right) < 1e-4) {
            return Value((left + right) / 2.0);
        } else if (std::isnan(left) && !std::isnan(right)) {
            return Value(right);
        } else if (std::isnan(right) && !std::isnan(left)) {
            return Value(left);
        }
        
        throw std::runtime_error("Math Error: Limit does not exist (left and right limits differ significantly or are undefined).");
    });

    reg("diff", { 2 }, [evalFunc, getVarName](const std::vector<Value>& args) -> Value {
        bool isSymDiff = args[0].isSymbolic();
        if (!isSymDiff) {
            if (std::holds_alternative<std::string>(args[1].data) || (args[1].isSymbolic() && args[1].asSymbolic().ptr->getType() == SymType::VAR)) {
                isSymDiff = true;
            }
        }
        if (isSymDiff) {
            SymExpr expr = args[0].asSymbolic();
            std::string var = getVarName(args[1], "diff");
            return Value(simplify(jc::diff(expr, var)));
        }

        auto cl = args[0].asFunction();
        double x = args[1].asDouble();
        double h = 1e-4;

        double d = (-evalFunc(cl, x + 2 * h) + 8 * evalFunc(cl, x + h)
            - 8 * evalFunc(cl, x - h) + evalFunc(cl, x - 2 * h)) / (12 * h);
        return Value(d);
        });

    reg("integ", { 2, 3, 4 }, [evalFunc, getVarName](const std::vector<Value>& args) -> Value {
        bool isSymInteg = args[0].isSymbolic();
        if (!isSymInteg && args.size() >= 2) {
            if (std::holds_alternative<std::string>(args[1].data) || (args[1].isSymbolic() && args[1].asSymbolic().ptr->getType() == SymType::VAR)) {
                isSymInteg = true;
            }
        }
        if (isSymInteg) {
            SymExpr expr = args[0].asSymbolic();
            std::string var = getVarName(args[1], "integ");
            SymExpr integral = jc::integrate(expr, var);
            
            if (args.size() == 4) {
                SymExpr a = args[2].asSymbolic();
                SymExpr b = args[3].asSymbolic();
                return Value(simplify(subs(integral, var, b) - subs(integral, var, a)));
            } else if (args.size() == 3) {
                throw std::runtime_error("TypeError: Symbolic definite integration expects 4 arguments: expr, var, a, b.");
            }
            return Value(simplify(integral));
        }

        if (args.size() < 3) {
            throw std::runtime_error("TypeError: Numeric integration expects at least 3 arguments: func, a, b.");
        }

        auto cl = args[0].asFunction();
        double a = args[1].asDouble(), b = args[2].asDouble();
        int n = (args.size() == 4) ? static_cast<int>(std::round(args[3].asDouble())) : 100000;
        if (n <= 0 || n % 2 != 0) n = 100000;
        double h = (b - a) / n, s = evalFunc(cl, a) + evalFunc(cl, b);
        for (int i = 1; i < n; i += 2) { jc::checkInterrupt(); s += 4 * evalFunc(cl, a + i * h); }
        for (int i = 2; i < n - 1; i += 2) { jc::checkInterrupt(); s += 2 * evalFunc(cl, a + i * h); }
        return Value(s * h / 3.0);
        });
}

} // namespace jc
