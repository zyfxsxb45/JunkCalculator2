#include "BuiltinRegistry.h"
#include "Highlight.h"          // ★ highlightCode(), colorsEnabled
#include "Module.h"
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
    registerFileIO();          // ★ Phase 2
    registerErrorHandling();   // ★ Phase 2
    registerSystemShell();
    registerTypeChecks();
}

// =================================================================
// [1] 基础数学函数
// =================================================================
void BuiltinRegistry::registerMath() {

    // 我们在此插入您要求的常量工厂和泛类型构造：
    reg("pi", { 0 }, [](const std::vector<Value>&) -> Value { return Value(3.14159265358979323846); });
    reg("e", { 0 }, [](const std::vector<Value>&) -> Value { return Value(2.71828182845904523536); });
    reg("i", { 0 }, [](const std::vector<Value>&) -> Value { return Value(Complex(0.0, 1.0)); });
    reg("complex", { 1, 2 }, [](const std::vector<Value>& args) -> Value {
        if (args.size() == 1) {
            // complex(x) → Complex(x, 0) 或保留已有复数
            if (std::holds_alternative<Complex>(args[0].data))
                return args[0];
            return Value(Complex(args[0].asDouble(), 0.0));
        }
        // complex(a, b) → Complex(a, b)
        return Value(Complex(args[0].asDouble(), args[1].asDouble()));
        });

    reg("double", { 1 }, [](const std::vector<Value>& args) -> Value {
        // 任意数值类型 → double（复数仅虚部为 0 时允许）
        if (std::holds_alternative<Complex>(args[0].data)) {
            const auto& c = std::get<Complex>(args[0].data);
            if (!Tol::isEq(c.imag, 0.0))
                throw std::runtime_error("Type Error: Cannot convert complex with nonzero imaginary part to double.");
            return Value(c.real);
        }
        return Value(args[0].asDouble());
        });

    reg("int", { 1 }, [](const std::vector<Value>& args) -> Value {
        // 截断取整（向零方向）
        if (std::holds_alternative<BigInt>(args[0].data))
            return args[0];
        if (std::holds_alternative<Fraction>(args[0].data)) {
            const auto& f = std::get<Fraction>(args[0].data);
            return Value(f.getNum() / f.getDen());  // BigInt 除法自动截断
        }
        if (std::holds_alternative<Complex>(args[0].data)) {
            const auto& c = std::get<Complex>(args[0].data);
            if (!Tol::isEq(c.imag, 0.0))
                throw std::runtime_error("Type Error: Cannot convert complex with nonzero imaginary part to int.");
            return Value(BigInt(static_cast<int64_t>(std::trunc(c.real))));
        }
        if (std::holds_alternative<double>(args[0].data)) {
            double v = std::get<double>(args[0].data);
            if (!std::isfinite(v))
                throw std::runtime_error("Type Error: Cannot convert non-finite value to int.");
            return Value(BigInt(static_cast<int64_t>(std::trunc(v))));
        }
        if (std::holds_alternative<std::string>(args[0].data)) {
            // 字符串解析为整数
            const auto& s = std::get<std::string>(args[0].data);
            try { return Value(BigInt(s)); }
            catch (...) {
                throw std::runtime_error("Type Error: Cannot parse '" + s + "' as integer.");
            }
        }
        return Value(args[0].asBigInt());
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

    reg("sin", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matSin());
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matSin());
        if (std::holds_alternative<Complex>(args[0].data)) return Value(sin(std::get<Complex>(args[0].data)));
        return Value(std::sin(args[0].asDouble()));
    });
    reg("cos", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matCos());
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matCos());
        if (std::holds_alternative<Complex>(args[0].data)) return Value(cos(std::get<Complex>(args[0].data)));
        return Value(std::cos(args[0].asDouble()));
    });
    reg("tan", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matTan());
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matTan());
        if (std::holds_alternative<Complex>(args[0].data)) return Value(tan(std::get<Complex>(args[0].data)));
        return Value(std::tan(args[0].asDouble()));
    });
    reg("exp", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matExp());
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matExp());
        if (std::holds_alternative<Complex>(args[0].data)) return Value(exp(std::get<Complex>(args[0].data)));
        return Value(std::exp(args[0].asDouble()));
    });
    reg("sinh", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matSinh());
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matSinh());
        if (std::holds_alternative<Complex>(args[0].data)) return Value(sinh(std::get<Complex>(args[0].data)));
        return Value(std::sinh(args[0].asDouble()));
    });
    reg("cosh", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matCosh());
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matCosh());
        if (std::holds_alternative<Complex>(args[0].data)) return Value(cosh(std::get<Complex>(args[0].data)));
        return Value(std::cosh(args[0].asDouble()));
    });
    reg("tanh", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(std::get<RealMatrix>(args[0].data).matTanh());
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(std::get<ComplexMatrix>(args[0].data).matTanh());
        if (std::holds_alternative<Complex>(args[0].data)) return Value(tanh(std::get<Complex>(args[0].data)));
        return Value(std::tanh(args[0].asDouble()));
    });

    reg("log", { 1, 2 }, [](const std::vector<Value>& args) -> Value {
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
    builtins["ln"] = builtins["log"];
    builtinArity["ln"] = builtinArity["log"];

    reg("sqrt", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(matSqrt(std::get<RealMatrix>(args[0].data)));
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) return Value(matSqrt(std::get<ComplexMatrix>(args[0].data)));
        if (std::holds_alternative<Complex>(args[0].data)) return Value(sqrt(std::get<Complex>(args[0].data)));
        double x = args[0].asDouble();
        if (x < 0) return Value(Complex(0, std::sqrt(-x)));
        return Value(std::sqrt(x));
    });

    reg("matpow", { 2 }, [](const std::vector<Value>& args) -> Value {
        return Value(matPow(args[0].asComplexMatrix(), args[1].asComplexMatrix()));
    });

    reg("asin", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<Complex>(args[0].data)) return Value(asin(std::get<Complex>(args[0].data)));
        double x = args[0].asDouble();
        if (x < -1.0 || x > 1.0) return Value(asin(Complex(x, 0.0)));
        return Value(std::asin(x));
    });
    reg("acos", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<Complex>(args[0].data)) return Value(acos(std::get<Complex>(args[0].data)));
        double x = args[0].asDouble();
        if (x < -1.0 || x > 1.0) return Value(acos(Complex(x, 0.0)));
        return Value(std::acos(x));
    });
    reg("atan", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<Complex>(args[0].data)) return Value(atan(std::get<Complex>(args[0].data)));
        return Value(std::atan(args[0].asDouble()));
    });

    reg("abs", { 1 }, [](const std::vector<Value>& args) -> Value {
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
    reg("pow", { 2 }, [](const std::vector<Value>& args) -> Value { return args[0] ^ args[1]; });

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

    reg("round", { 1, 2 }, [roundDispatch](const std::vector<Value>& args) -> Value { return roundDispatch(args, "round", [](double x) { return std::round(x); }); });
    reg("floor", { 1, 2 }, [roundDispatch](const std::vector<Value>& args) -> Value { return roundDispatch(args, "floor", [](double x) { return std::floor(x); }); });
    reg("ceil", { 1, 2 }, [roundDispatch](const std::vector<Value>& args) -> Value { return roundDispatch(args, "ceil", [](double x) { return std::ceil(x); }); });
    reg("trunc", { 1, 2 }, [roundDispatch](const std::vector<Value>& args) -> Value { return roundDispatch(args, "trunc", [](double x) { return std::trunc(x); }); });

    reg("sgn", { 1 }, [](const std::vector<Value>& args) -> Value { double x = args[0].asDouble(); return Value(x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0)); });
    reg("deg", { 1 }, [](const std::vector<Value>& args) -> Value { return Value(args[0].asDouble() / Complex::PI * 180.0); });
    reg("rad", { 1 }, [](const std::vector<Value>& args) -> Value { return Value(args[0].asDouble() / 180.0 * Complex::PI); });
    reg("evalf", { 1 }, [](const std::vector<Value>& args) -> Value { return Value(args[0].asDouble()); });

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
    reg("sum", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { return matrixDispatch1(args[0], [](const auto& m) { return m.sum(); }); });
    reg("prod", { 1 }, [matrixDispatch1](const std::vector<Value>& args) -> Value { return matrixDispatch1(args[0], [](const auto& m) { return m.product(); }); });
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

    reg("dim", { 1 }, [assertVec](const std::vector<Value>& args) -> Value { assertVec(args[0], "dim"); if (std::holds_alternative<RealMatrix>(args[0].data)) return Value(static_cast<double>(std::get<RealMatrix>(args[0].data).getRows())); return Value(static_cast<double>(std::get<ComplexMatrix>(args[0].data).getRows())); });
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
    reg("factor", { 1 }, [toBigInt](const std::vector<Value>& args) -> Value { auto factors = toBigInt(args[0]).factorize(); int r = static_cast<int>(factors.size()); std::vector<double> flat; for (const auto& f : factors) { flat.push_back(f.first.toDouble()); flat.push_back(static_cast<double>(f.second)); } return Value(RealMatrix(r, 2, flat)); });
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
    reg("C", { 2 }, [](const std::vector<Value>& args) -> Value { int64_t n = static_cast<int64_t>(std::round(args[0].asDouble())), k = static_cast<int64_t>(std::round(args[1].asDouble())); if (n<0||k<0) throw std::runtime_error("Math Error: C(n,k) requires non-negative integers."); if (k>n) return Value(BigInt(0)); if (k>n-k) k = n-k; BigInt result(1); for (int64_t i = 0; i < k; ++i) { result = result*BigInt(n-i); result = result/BigInt(i+1); } return Value(result); });
    reg("A", { 2 }, [](const std::vector<Value>& args) -> Value { int64_t n = static_cast<int64_t>(std::round(args[0].asDouble())), k = static_cast<int64_t>(std::round(args[1].asDouble())); if (n<0||k<0) throw std::runtime_error("Math Error: A(n,k) requires non-negative integers."); if (k>n) return Value(BigInt(0)); BigInt result(1); for (int64_t i = 0; i < k; ++i) result = result*BigInt(n-i); return Value(result); });
    reg("catalan", { 1 }, [](const std::vector<Value>& args) -> Value { int64_t n = static_cast<int64_t>(std::round(args[0].asDouble())); if (n<0) throw std::runtime_error("Math Error: catalan(n) requires non-negative integer."); BigInt result(1); for (int64_t i = 0; i < n; ++i) { result = result*BigInt(2*n-i); result = result/BigInt(i+1); } result = result/BigInt(n+1); return Value(result); });
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
    reg("max", { 1 }, [](const std::vector<Value>& args) -> Value { auto d = extractDS(args[0], "max"); double mx = d[0]; for (double v : d) if (v > mx) mx = v; return Value(mx); });
    reg("min", { 1 }, [](const std::vector<Value>& args) -> Value { auto d = extractDS(args[0], "min"); double mn = d[0]; for (double v : d) if (v < mn) mn = v; return Value(mn); });
    reg("span", { 1 }, [](const std::vector<Value>& args) -> Value { auto d = extractDS(args[0], "span"); double mx = d[0], mn = d[0]; for (double v : d) { if (v > mx) mx = v; if (v < mn) mn = v; } return Value(mx - mn); });

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
    reg("sysinfo", { 0 }, [](const std::vector<Value>&) -> Value { std::cout << "--- Junk Calculator System Info ---\n" << "Prime DB: " << BigInt::getPrimeFilePath() << "\n" << "Indexed:  " << BigInt::totalPrimesInFile << " primes\n"; if (BigInt::totalPrimesInFile > 0) std::cout << "Max:      " << BigInt::largestPrimeInFile << "\n"; std::cout << "-----------------------------------" << std::endl; return Value::none(); });
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
        if (step>0) { for (double v=start; v<=end+Tol::EPS*100; v+=step) vals.push_back(v); }
        else { for (double v=start; v>=end-Tol::EPS*100; v+=step) vals.push_back(v); }
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
        }
        if (std::holds_alternative<std::string>(args[0].data)) return Value(static_cast<double>(std::get<std::string>(args[0].data).size()));
        if (std::holds_alternative<RealMatrix>(args[0].data)) { const auto& m = std::get<RealMatrix>(args[0].data); if (m.getCols() == 1) return Value(static_cast<double>(m.getRows())); if (m.getRows() == 1) return Value(static_cast<double>(m.getCols())); return Value(static_cast<double>(m.getRows() * m.getCols())); }
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) { const auto& m = std::get<ComplexMatrix>(args[0].data); if (m.getCols() == 1) return Value(static_cast<double>(m.getRows())); if (m.getRows() == 1) return Value(static_cast<double>(m.getCols())); return Value(static_cast<double>(m.getRows() * m.getCols())); }
        if (std::holds_alternative<StringMatrix>(args[0].data)) return Value(static_cast<double>(std::get<StringMatrix>(args[0].data).getRows() * std::get<StringMatrix>(args[0].data).getCols()));
        if (std::holds_alternative<Dict>(args[0].data)) return Value(static_cast<double>(std::get<Dict>(args[0].data).size()));
        if (std::holds_alternative<List>(args[0].data)) return Value(static_cast<double>(std::get<List>(args[0].data).size()));
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
    reg("first", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<List>(args[0].data)) { const auto& L=std::get<List>(args[0].data); if (L.empty()) throw std::runtime_error("Runtime Error: first() on empty list."); return anyToVal(L.raw()[0]); } if (std::holds_alternative<RealMatrix>(args[0].data)) { const auto& m=std::get<RealMatrix>(args[0].data); if (m.getRows()*m.getCols()==0) throw std::runtime_error("Runtime Error: first() on empty vector."); return Value(m.rawData()[0]); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { const auto& m=std::get<ComplexMatrix>(args[0].data); if (m.getRows()*m.getCols()==0) throw std::runtime_error("Runtime Error: first() on empty vector."); return Value(m.rawData()[0]); } throw std::runtime_error("Type Error: first() expects a vector or list."); });
    reg("last", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<List>(args[0].data)) { const auto& L=std::get<List>(args[0].data); if (L.empty()) throw std::runtime_error("Runtime Error: last() on empty list."); return anyToVal(L.raw().back()); } if (std::holds_alternative<RealMatrix>(args[0].data)) { const auto& m=std::get<RealMatrix>(args[0].data); int n=m.getRows()*m.getCols(); if (n==0) throw std::runtime_error("Runtime Error: last() on empty vector."); return Value(m.rawData()[n-1]); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { const auto& m=std::get<ComplexMatrix>(args[0].data); int n=m.getRows()*m.getCols(); if (n==0) throw std::runtime_error("Runtime Error: last() on empty vector."); return Value(m.rawData()[n-1]); } throw std::runtime_error("Type Error: last() expects a vector or list."); });
    reg("pop", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<List>(args[0].data)) { const auto& L=std::get<List>(args[0].data); if (L.empty()) throw std::runtime_error("Runtime Error: pop() on empty list."); return anyToVal(L.raw().back()); } if (std::holds_alternative<RealMatrix>(args[0].data)) { auto flat=std::get<RealMatrix>(args[0].data).rawData(); if (flat.empty()) throw std::runtime_error("Runtime Error: pop() on empty vector."); return Value(flat.back()); } if (std::holds_alternative<ComplexMatrix>(args[0].data)) { auto flat=std::get<ComplexMatrix>(args[0].data).rawData(); if (flat.empty()) throw std::runtime_error("Runtime Error: pop() on empty vector."); return Value(flat.back()); } throw std::runtime_error("Type Error: pop() expects a vector or list."); });

    reg("push", { 2 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<List>(args[0].data)) { List L=std::get<List>(args[0].data); L.push_back(valToAny(args[1])); return Value(L); } auto v=toVecHelper(args[0], "push"); v.push_back(args[1].asDouble()); return toRowVec(v); });
    reg("prepend", { 2 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<List>(args[0].data)) { List L=std::get<List>(args[0].data); L.insert(0, valToAny(args[1])); return Value(L); } auto v=toVecHelper(args[0], "prepend"); v.insert(v.begin(), args[1].asDouble()); return toRowVec(v); });
    reg("insert", { 3 }, [](const std::vector<Value>& args) -> Value { int idx=static_cast<int>(std::round(args[1].asDouble())); if (std::holds_alternative<List>(args[0].data)) { List L=std::get<List>(args[0].data); L.insert(idx, valToAny(args[2])); return Value(L); } auto v=toVecHelper(args[0], "insert"); if (idx<0) idx=static_cast<int>(v.size())+idx; if (idx<0||idx>static_cast<int>(v.size())) throw std::runtime_error("Runtime Error: insert() index out of range."); v.insert(v.begin()+idx, args[2].asDouble()); return toRowVec(v); });
    reg("removeAt", { 2 }, [](const std::vector<Value>& args) -> Value { int idx=static_cast<int>(std::round(args[1].asDouble())); if (std::holds_alternative<List>(args[0].data)) { List L=std::get<List>(args[0].data); L.removeAt(idx); return Value(L); } auto v=toVecHelper(args[0], "removeAt"); if (v.empty()) throw std::runtime_error("Runtime Error: removeAt() on empty vector."); if (idx<0) idx=static_cast<int>(v.size())+idx; if (idx<0||idx>=static_cast<int>(v.size())) throw std::runtime_error("Runtime Error: removeAt() index out of range."); v.erase(v.begin()+idx); return toRowVec(v); });

    reg("slice", { 2, 3 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<List>(args[0].data)) { const auto& L=std::get<List>(args[0].data); int n=static_cast<int>(L.size()); int start=static_cast<int>(std::round(args[1].asDouble())); if (start<0) start=n+start; start=std::max(0,std::min(start,n)); int end=n; if (args.size()==3) { end=static_cast<int>(std::round(args[2].asDouble())); if (end<0) end=n+end; end=std::max(0,std::min(end,n)); } List result; for (int i=start;i<end;++i) result.push_back(L.raw()[i]); return Value(result); }
        auto v=toVecHelper(args[0], "slice"); int n=static_cast<int>(v.size()); int start=static_cast<int>(std::round(args[1].asDouble())); if (start<0) start=n+start; start=std::max(0,std::min(start,n)); int end=n; if (args.size()==3) { end=static_cast<int>(std::round(args[2].asDouble())); if (end<0) end=n+end; end=std::max(0,std::min(end,n)); } if (start>=end) return toRowVec({}); return toRowVec(std::vector<double>(v.begin()+start, v.begin()+end));
    });

    reg("reverse", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<std::string>(args[0].data)) { std::string s=std::get<std::string>(args[0].data); std::reverse(s.begin(),s.end()); return Value(s); } if (std::holds_alternative<List>(args[0].data)) { List L=std::get<List>(args[0].data); std::reverse(L.raw().begin(),L.raw().end()); return Value(L); } auto v=toVecHelper(args[0], "reverse"); std::reverse(v.begin(),v.end()); return toRowVec(v); });

    reg("flatten", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<List>(args[0].data)) { List result; std::function<void(const List&)> flattenList = [&](const List& L) { for (const auto& e : L.raw()) { Value elem = anyToVal(e); if (std::holds_alternative<List>(elem.data)) flattenList(std::get<List>(elem.data)); else result.push_back(e); } }; flattenList(std::get<List>(args[0].data)); return Value(result); }
        if (std::holds_alternative<RealMatrix>(args[0].data)) { const auto& m=std::get<RealMatrix>(args[0].data); int n=m.getRows()*m.getCols(); return Value(RealMatrix(n,1,m.rawData())); }
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) { const auto& m=std::get<ComplexMatrix>(args[0].data); int n=m.getRows()*m.getCols(); return Value(ComplexMatrix(n,1,m.rawData())); }
        if (std::holds_alternative<StringMatrix>(args[0].data)) { const auto& m=std::get<StringMatrix>(args[0].data); int n=m.getRows()*m.getCols(); return Value(StringMatrix(n,1,m.rawData())); }
        throw std::runtime_error("Type Error: flatten() expects a matrix or list.");
    });

    reg("unique", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<List>(args[0].data)) { const auto& L=std::get<List>(args[0].data); List result; for (const auto& e : L.raw()) { Value elem = anyToVal(e); bool found=false; for (const auto& r : result.raw()) { Value rv=anyToVal(r); if (elem.data.index()==rv.data.index()) { std::ostringstream a,b; a<<elem; b<<rv; if (a.str()==b.str()) { found=true; break; } } } if (!found) result.push_back(e); } return Value(result); }
        auto v=toVecHelper(args[0], "unique"); std::vector<double> result; for (double x:v) { bool found=false; for (double y:result) if (Tol::isEq(x,y,1e4)){found=true;break;} if (!found) result.push_back(x); } return toRowVec(result);
    });

    reg("range", { 1, 2, 3 }, [](const std::vector<Value>& args) -> Value {
        if (args.size()==1) { int end=static_cast<int>(std::round(args[0].asDouble())); if (end<=0) return Value(RealMatrix(1,0)); std::vector<double> v(end); for (int i=0;i<end;++i) v[i]=static_cast<double>(i); return Value(RealMatrix(1,end,v)); }
        if (args.size()==2) { int start=static_cast<int>(std::round(args[0].asDouble())); int end=static_cast<int>(std::round(args[1].asDouble())); if (start>=end) return Value(RealMatrix(1,0)); int n=end-start; std::vector<double> v(n); for (int i=0;i<n;++i) v[i]=static_cast<double>(start+i); return Value(RealMatrix(1,n,v)); }
        double start=args[0].asDouble(),end=args[1].asDouble(),step=args[2].asDouble(); if (Tol::isEq(step,0.0)) throw std::runtime_error("Math Error: range() step cannot be zero."); std::vector<double> v; if (step>0) { for (double x=start;x<end-Tol::EPS*100;x+=step) v.push_back(x); } else { for (double x=start;x>end+Tol::EPS*100;x+=step) v.push_back(x); } int n=static_cast<int>(v.size()); if (n==0) return Value(RealMatrix(1,0)); return Value(RealMatrix(1,n,v));
    });

    reg("fill", { 2 }, [](const std::vector<Value>& args) -> Value { int n=static_cast<int>(std::round(args[1].asDouble())); if (n<0) throw std::runtime_error("Runtime Error: fill() count must be non-negative."); double val=args[0].asDouble(); return Value(RealMatrix(1,n,std::vector<double>(n,val))); });
    reg("linspace", { 3 }, [](const std::vector<Value>& args) -> Value { double a=args[0].asDouble(),b=args[1].asDouble(); int n=static_cast<int>(std::round(args[2].asDouble())); if (n<1) throw std::runtime_error("Runtime Error: linspace() requires n >= 1."); std::vector<double> v(n); if (n==1) v[0]=a; else { for (int i=0;i<n;++i) v[i]=a+(b-a)*i/(n-1); } return Value(RealMatrix(1,n,v)); });
    reg("cumsum", { 1 }, [](const std::vector<Value>& args) -> Value { auto v=toVecHelper(args[0],"cumsum"); for (size_t i=1;i<v.size();++i) v[i]+=v[i-1]; return toRowVec(v); });
    reg("cumprod", { 1 }, [](const std::vector<Value>& args) -> Value { auto v=toVecHelper(args[0],"cumprod"); for (size_t i=1;i<v.size();++i) v[i]*=v[i-1]; return toRowVec(v); });
    reg("diffs", { 1 }, [](const std::vector<Value>& args) -> Value { auto v=toVecHelper(args[0],"diffs"); if (v.size()<2) throw std::runtime_error("Runtime Error: diffs() requires at least 2 elements."); std::vector<double> d(v.size()-1); for (size_t i=0;i<d.size();++i) d[i]=v[i+1]-v[i]; return toRowVec(d); });

    reg("indexOf", { 2 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<List>(args[0].data)) { const auto& L=std::get<List>(args[0].data); for (size_t i=0;i<L.size();++i) { std::ostringstream a,b; a<<anyToVal(L.raw()[i]); b<<args[1]; if (a.str()==b.str()) return Value(static_cast<double>(i)); } return Value(-1.0); } auto v=toVecHelper(args[0],"indexOf"); double target=args[1].asDouble(); for (size_t i=0;i<v.size();++i) if (Tol::isEq(v[i],target,1e4)) return Value(static_cast<double>(i)); return Value(-1.0); });
    reg("count", { 2 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<List>(args[0].data)) { const auto& L=std::get<List>(args[0].data); int c=0; std::ostringstream bs; bs<<args[1]; std::string bstr=bs.str(); for (const auto& e:L.raw()) { std::ostringstream as; as<<anyToVal(e); if (as.str()==bstr) c++; } return Value(static_cast<double>(c)); } auto v=toVecHelper(args[0],"count"); double target=args[1].asDouble(); int c=0; for (double x:v) if (Tol::isEq(x,target,1e4)) c++; return Value(static_cast<double>(c)); });

    reg("join", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("Type Error: join() delimiter must be a string.");
        const std::string& delim=std::get<std::string>(args[1].data);
        if (std::holds_alternative<List>(args[0].data)) { const auto& L=std::get<List>(args[0].data); std::ostringstream oss; for (size_t i=0;i<L.size();++i) { if (i>0) oss<<delim; oss<<anyToVal(L.raw()[i]); } return Value(oss.str()); }
        auto v=toVecHelper(args[0],"join"); std::ostringstream oss; for (size_t i=0;i<v.size();++i) { if (i>0) oss<<delim; double val=v[i]; double rounded=std::round(val); if (Tol::isEq(val,rounded,1e5)&&std::abs(rounded)<1e15&&rounded==std::trunc(rounded)) oss<<static_cast<int64_t>(rounded); else oss<<val; } return Value(oss.str());
    });

    reg("zip", { 2 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<List>(args[0].data)||std::holds_alternative<List>(args[1].data)) {
            auto ensureList = [](const Value& v) -> List { if (std::holds_alternative<List>(v.data)) return std::get<List>(v.data); List L; if (std::holds_alternative<RealMatrix>(v.data)) { for (double d:std::get<RealMatrix>(v.data).rawData()) L.push_back(valToAny(Value(d))); } else if (std::holds_alternative<ComplexMatrix>(v.data)) { for (const auto& c:std::get<ComplexMatrix>(v.data).rawData()) L.push_back(valToAny(Value(c))); } else L.push_back(valToAny(v)); return L; };
            List a=ensureList(args[0]),b=ensureList(args[1]); if (a.size()!=b.size()) throw std::runtime_error("Math Error: zip() requires same length."); List result; for (size_t i=0;i<a.size();++i) { List pair; pair.push_back(a.raw()[i]); pair.push_back(b.raw()[i]); result.push_back(valToAny(Value(pair))); } return Value(result);
        }
        if (std::holds_alternative<RealMatrix>(args[0].data)&&std::holds_alternative<RealMatrix>(args[1].data)) { const auto& a=std::get<RealMatrix>(args[0].data); const auto& b=std::get<RealMatrix>(args[1].data); auto fa=a.rawData(),fb=b.rawData(); if (fa.size()!=fb.size()) throw std::runtime_error("Math Error: zip() vectors must have same length."); int n=static_cast<int>(fa.size()); std::vector<double> flat(n*2); for (int i=0;i<n;++i){flat[i*2]=fa[i];flat[i*2+1]=fb[i];} return Value(RealMatrix(n,2,flat)); }
        ComplexMatrix a=args[0].asComplexMatrix(),b=args[1].asComplexMatrix(); auto fa=a.rawData(),fb=b.rawData(); if (fa.size()!=fb.size()) throw std::runtime_error("Math Error: zip() vectors must have same length."); int n=static_cast<int>(fa.size()); std::vector<Complex> flat(n*2); for (int i=0;i<n;++i){flat[i*2]=fa[i];flat[i*2+1]=fb[i];} return Value(ComplexMatrix(n,2,flat));
    });

    reg("cat", {}, [](const std::vector<Value>& args) -> Value {
        if (args.size()<1) throw std::runtime_error("Runtime Error: cat() expects at least 1 argument.");
        bool hasList=false; for (const auto& a:args) if (std::holds_alternative<List>(a.data)){hasList=true;break;}
        if (hasList) { List result; for (const auto& a:args) { if (std::holds_alternative<List>(a.data)) { for (const auto& e:std::get<List>(a.data).raw()) result.push_back(e); } else result.push_back(valToAny(a)); } return Value(result); }
        std::vector<double> flat; for (const auto& a:args) { if (std::holds_alternative<RealMatrix>(a.data)) { const auto& d=std::get<RealMatrix>(a.data).rawData(); flat.insert(flat.end(),d.begin(),d.end()); } else if (std::holds_alternative<double>(a.data)) flat.push_back(std::get<double>(a.data)); else if (std::holds_alternative<BigInt>(a.data)) flat.push_back(std::get<BigInt>(a.data).toDouble()); else if (std::holds_alternative<Fraction>(a.data)) flat.push_back(std::get<Fraction>(a.data).toDouble()); else throw std::runtime_error("Type Error: cat() only accepts real scalars, real vectors, or Lists."); }
        int n=static_cast<int>(flat.size()); if (n==0) return Value(RealMatrix(1,0)); return Value(RealMatrix(1,n,flat));
    });
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
// [19] Dict
// =================================================================
void BuiltinRegistry::registerDictFunctions() {
    reg("dict", {}, [](const std::vector<Value>& args) -> Value { Dict d; if (args.size()%2!=0) throw std::runtime_error("Runtime Error: dict() expects even number of arguments."); for (size_t i=0;i<args.size();i+=2) { std::string key; if (std::holds_alternative<std::string>(args[i].data)) key=std::get<std::string>(args[i].data); else { std::ostringstream oss; oss<<args[i]; key=oss.str(); } d.set(key, valToAny(args[i+1])); } return Value(d); });
    reg("keys", { 1 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<Dict>(args[0].data)) throw std::runtime_error("Type Error: keys() expects a Dict."); auto ks=std::get<Dict>(args[0].data).getKeys(); std::vector<std::string> flat(ks.begin(),ks.end()); return Value(StringMatrix(1,static_cast<int>(flat.size()),flat)); });
    reg("values", { 1 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<Dict>(args[0].data)) throw std::runtime_error("Type Error: values() expects a Dict."); const auto& entries=std::get<Dict>(args[0].data).getEntries(); std::vector<double> nums; bool allDouble=true; for (const auto& [k,v]:entries) { try { const auto& val=std::any_cast<const Value&>(v); if (allDouble) { try{nums.push_back(val.asDouble());}catch(...){allDouble=false;} } } catch(...){allDouble=false;} } if (allDouble&&!nums.empty()) return Value(RealMatrix(1,static_cast<int>(nums.size()),nums)); std::vector<std::string> strs; for (const auto& [k,v]:entries) { try { const auto& val=std::any_cast<const Value&>(v); std::ostringstream oss; oss<<val; strs.push_back(oss.str()); } catch(...){strs.push_back("?");} } return Value(StringMatrix(1,static_cast<int>(strs.size()),strs)); });
    reg("hasKey", { 2 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<Dict>(args[0].data)) throw std::runtime_error("Type Error: hasKey() expects a Dict."); std::string key; if (std::holds_alternative<std::string>(args[1].data)) key=std::get<std::string>(args[1].data); else { std::ostringstream oss; oss<<args[1]; key=oss.str(); } return Value(std::get<Dict>(args[0].data).has(key)?1.0:0.0); });
    reg("removeKey", { 2 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<Dict>(args[0].data)) throw std::runtime_error("Type Error: removeKey() expects a Dict."); Dict d=std::get<Dict>(args[0].data); std::string key; if (std::holds_alternative<std::string>(args[1].data)) key=std::get<std::string>(args[1].data); else { std::ostringstream oss; oss<<args[1]; key=oss.str(); } if (!d.remove(key)) throw std::runtime_error("Runtime Error: Key '"+key+"' not found in Dict."); return Value(d); });
    reg("dictSize", { 1 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<Dict>(args[0].data)) throw std::runtime_error("Type Error: dictSize() expects a Dict."); return Value(static_cast<double>(std::get<Dict>(args[0].data).size())); });
    reg("dictMerge", { 2 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<Dict>(args[0].data)||!std::holds_alternative<Dict>(args[1].data)) throw std::runtime_error("Type Error: dictMerge() expects two Dicts."); Dict result=std::get<Dict>(args[0].data); for (const auto& [k,v]:std::get<Dict>(args[1].data).getEntries()) result.set(k,v); return Value(result); });
    reg("dictPairs", { 1 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<Dict>(args[0].data)) throw std::runtime_error("Type Error: dictPairs() expects a Dict."); const auto& entries=std::get<Dict>(args[0].data).getEntries(); std::vector<std::string> flat; for (const auto& [k,v]:entries) { flat.push_back(k); try { const auto& val=std::any_cast<const Value&>(v); std::ostringstream oss; oss<<val; flat.push_back(oss.str()); } catch(...){flat.push_back("?");} } return Value(StringMatrix(static_cast<int>(entries.size()),2,flat)); });
}

// =================================================================
// [20] List & Conversion
// =================================================================
void BuiltinRegistry::registerListConversion() {
    reg("list", {}, [](const std::vector<Value>& args) -> Value { List L; for (const auto& a:args) L.push_back(valToAny(a)); return Value(L); });

    reg("toList", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<List>(args[0].data)) return args[0];
        if (std::holds_alternative<RealMatrix>(args[0].data)) { const auto& m=std::get<RealMatrix>(args[0].data); if (m.getRows()==1||m.getCols()==1) { List L; for (const auto& d:m.rawData()) L.push_back(valToAny(Value(d))); return Value(L); } List rows; for (int i=0;i<m.getRows();++i) { List row; for (int j=0;j<m.getCols();++j) row.push_back(valToAny(Value(m(i,j)))); rows.push_back(valToAny(Value(row))); } return Value(rows); }
        if (std::holds_alternative<ComplexMatrix>(args[0].data)) { const auto& m=std::get<ComplexMatrix>(args[0].data); if (m.getRows()==1||m.getCols()==1) { List L; for (const auto& c:m.rawData()) L.push_back(valToAny(Value(c))); return Value(L); } List rows; for (int i=0;i<m.getRows();++i) { List row; for (int j=0;j<m.getCols();++j) row.push_back(valToAny(Value(m(i,j)))); rows.push_back(valToAny(Value(row))); } return Value(rows); }
        if (std::holds_alternative<StringMatrix>(args[0].data)) { const auto& m=std::get<StringMatrix>(args[0].data); if (m.getRows()==1||m.getCols()==1) { List L; for (const auto& s:m.rawData()) L.push_back(valToAny(Value(s))); return Value(L); } List rows; for (int i=0;i<m.getRows();++i) { List row; for (int j=0;j<m.getCols();++j) row.push_back(valToAny(Value(m(i,j)))); rows.push_back(valToAny(Value(row))); } return Value(rows); }
        if (std::holds_alternative<std::string>(args[0].data)) { const auto& s=std::get<std::string>(args[0].data); List L; for (char c:s) L.push_back(valToAny(Value(std::string(1,c)))); return Value(L); }
        List L; L.push_back(valToAny(args[0])); return Value(L);
    });

    reg("toStrVec", { 1 }, [](const std::vector<Value>& args) -> Value { if (std::holds_alternative<List>(args[0].data)) { const auto& L=std::get<List>(args[0].data); std::vector<std::string> flat; for (const auto& e:L.raw()) { Value v=anyToVal(e); if (std::holds_alternative<std::string>(v.data)) flat.push_back(std::get<std::string>(v.data)); else { std::ostringstream oss; oss<<v; flat.push_back(oss.str()); } } return Value(StringMatrix(static_cast<int>(flat.size()),1,flat)); } if (std::holds_alternative<StringMatrix>(args[0].data)) return args[0]; throw std::runtime_error("Type Error: toStrVec() expects a List or StringMatrix."); });
    reg("toArray", { 1 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<List>(args[0].data)) throw std::runtime_error("Type Error: toArray() expects a List."); const auto& L=std::get<List>(args[0].data); std::vector<double> flat; for (const auto& e:L.raw()) flat.push_back(anyToVal(e).asDouble()); return Value(RealMatrix(1,static_cast<int>(flat.size()),flat)); });

    reg("toMatrix", { 1 }, [](const std::vector<Value>& args) -> Value {
        if (std::holds_alternative<RealMatrix>(args[0].data)||std::holds_alternative<ComplexMatrix>(args[0].data)||std::holds_alternative<StringMatrix>(args[0].data)) return args[0];
        if (!std::holds_alternative<List>(args[0].data)) throw std::runtime_error("Type Error: toMatrix() expects a List or matrix.");
        const auto& L=std::get<List>(args[0].data);
        if (L.empty()) return Value(RealMatrix(0,0));
        Value first=anyToVal(L.raw()[0]);
        bool isNested=std::holds_alternative<List>(first.data);
        auto isReal=[](const Value& v){return std::holds_alternative<double>(v.data)||std::holds_alternative<BigInt>(v.data)||std::holds_alternative<Fraction>(v.data);};
        auto isNumeric=[](const Value& v){return std::holds_alternative<double>(v.data)||std::holds_alternative<BigInt>(v.data)||std::holds_alternative<Fraction>(v.data)||std::holds_alternative<Complex>(v.data);};
        auto isStr=[](const Value& v){return std::holds_alternative<std::string>(v.data);};
        auto valToStr=[](const Value& v)->std::string{if(std::holds_alternative<std::string>(v.data))return std::get<std::string>(v.data);std::ostringstream oss;oss<<v;return oss.str();};
        if (!isNested) { int n=static_cast<int>(L.size()); bool allReal=true,allNum=true,allStr=true; for (const auto& e:L.raw()){Value v=anyToVal(e);if(!isReal(v))allReal=false;if(!isNumeric(v))allNum=false;if(!isStr(v))allStr=false;} if (allStr){std::vector<std::string> flat;for(const auto& e:L.raw())flat.push_back(std::get<std::string>(anyToVal(e).data));return Value(StringMatrix(1,n,flat));} if (allReal){std::vector<double> flat;for(const auto& e:L.raw())flat.push_back(anyToVal(e).asDouble());return Value(RealMatrix(1,n,flat));} if (allNum){std::vector<Complex> flat;for(const auto& e:L.raw())flat.push_back(anyToVal(e).asComplex());return Value(ComplexMatrix(1,n,flat));} std::vector<std::string> flat;for(const auto& e:L.raw())flat.push_back(valToStr(anyToVal(e)));return Value(StringMatrix(1,n,flat)); }
        int rows=static_cast<int>(L.size()),cols=-1; bool allReal=true,allNum=true,allStr=true;
        std::vector<std::vector<Value>> grid;
        for(const auto& rowAny:L.raw()){Value rowVal=anyToVal(rowAny);if(!std::holds_alternative<List>(rowVal.data))throw std::runtime_error("Type Error: toMatrix() expects uniform List of Lists.");const auto& rowList=std::get<List>(rowVal.data);if(cols==-1)cols=static_cast<int>(rowList.size());else if(static_cast<int>(rowList.size())!=cols)throw std::runtime_error("Type Error: toMatrix() rows must have equal length.");std::vector<Value> rowVec;for(const auto& e:rowList.raw()){Value v=anyToVal(e);if(!isReal(v))allReal=false;if(!isNumeric(v))allNum=false;if(!isStr(v))allStr=false;rowVec.push_back(v);}grid.push_back(std::move(rowVec));}
        if(cols<=0)return Value(RealMatrix(0,0));
        if(allStr){std::vector<std::string> flat;for(const auto& row:grid)for(const auto& v:row)flat.push_back(std::get<std::string>(v.data));return Value(StringMatrix(rows,cols,flat));}
        if(allReal){std::vector<double> flat;for(const auto& row:grid)for(const auto& v:row)flat.push_back(v.asDouble());return Value(RealMatrix(rows,cols,flat));}
        if(allNum){std::vector<Complex> flat;for(const auto& row:grid)for(const auto& v:row)flat.push_back(v.asComplex());return Value(ComplexMatrix(rows,cols,flat));}
        std::vector<std::string> flat;for(const auto& row:grid)for(const auto& v:row)flat.push_back(valToStr(v));return Value(StringMatrix(rows,cols,flat));
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
    reg("hasField", { 2 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) return Value(0.0); if (!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("Type Error: hasField() field name must be a string."); return Value(std::get<std::shared_ptr<Instance>>(args[0].data)->fields.has(std::get<std::string>(args[1].data))?1.0:0.0); });
    reg("getFields", { 1 }, [](const std::vector<Value>& args) -> Value { if (!std::holds_alternative<std::shared_ptr<Instance>>(args[0].data)) throw std::runtime_error("Type Error: getFields() expects an instance."); auto ks=std::get<std::shared_ptr<Instance>>(args[0].data)->fields.getKeys(); if(ks.empty()) return Value(StringMatrix(1,0)); std::vector<std::string> flat(ks.begin(),ks.end()); return Value(StringMatrix(1,static_cast<int>(flat.size()),flat)); });
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
        return std::visit([](auto&& a) -> Value {
            using T = std::decay_t<decltype(a)>;
            if constexpr (std::is_same_v<T, std::monostate>) return Value(std::string("none"));
            else if constexpr (std::is_same_v<T, double>) return Value(std::string("double"));
            else if constexpr (std::is_same_v<T, BigInt>) return Value(std::string("BigInt"));
            else if constexpr (std::is_same_v<T, Fraction>) return Value(std::string("Fraction"));
            else if constexpr (std::is_same_v<T, Complex>) return Value(std::string("Complex"));
            else if constexpr (std::is_same_v<T, std::string>) return Value(std::string("String"));
            else if constexpr (std::is_same_v<T, RealMatrix>) return Value(std::string("RealMatrix"));
            else if constexpr (std::is_same_v<T, ComplexMatrix>) return Value(std::string("ComplexMatrix"));
            else if constexpr (std::is_same_v<T, BaseNum>) return Value(std::string("BaseNum"));
            else if constexpr (std::is_same_v<T, StringMatrix>) return Value(std::string("StringMatrix"));
            else if constexpr (std::is_same_v<T, Dict>) return Value(std::string("Dict"));
            else if constexpr (std::is_same_v<T, List>) return Value(std::string("List"));
            else if constexpr (std::is_same_v<T, std::shared_ptr<FunctionClosure>>) return Value(std::string("Function"));
            else if constexpr (std::is_same_v<T, std::shared_ptr<ClassDefinition>>) return Value(std::string("Class"));
            else if constexpr (std::is_same_v<T, std::shared_ptr<Instance>>) return Value(a->classDef->name);
            else if constexpr (std::is_same_v<T, SuperProxyPtr>) return Value(std::string("super"));
            else return Value(std::string("unknown"));
            }, args[0].data);
        });
}

// =================================================================
// [HOF] 高阶函数（使用通用 callClosure，VM/Evaluator 均可用）
// Evaluator 会用自己的版本覆盖这些（支持 AST 闭包 + dunder）
// =================================================================
// =================================================================
// [HOF] 高阶函数（使用 safeCallFunction 自动在 Evaluator 走 AST，VM 走 Native）
// =================================================================
void BuiltinRegistry::registerHigherOrder() {

    reg("map", { 2 }, [](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        if (!cl->acceptsArgCount(1))
            throw std::runtime_error("Runtime Error: map() requires a single-parameter function.");
        if (std::holds_alternative<List>(args[1].data)) {
            const auto& L = std::get<List>(args[1].data);
            List result;
            for (const auto& e : L.raw()) {
                Value y = safeCallFunction(cl, { anyToVal(e) });   // ★ 关键替换
                result.push_back(valToAny(y));
            }
            return Value(result);
        }
        if (std::holds_alternative<RealMatrix>(args[1].data)) {
            auto flat = std::get<RealMatrix>(args[1].data).rawData();
            std::vector<double> rd; std::vector<Complex> rc; bool hasComplex = false;
            for (double x : flat) {
                Value y = safeCallFunction(cl, { Value(x) });      // ★ 关键替换
                if (!hasComplex) {
                    try { rd.push_back(y.asDouble()); }
                    catch (...) { hasComplex = true; for (double d : rd) rc.push_back(Complex(d)); rc.push_back(y.asComplex()); }
                }
                else { rc.push_back(y.asComplex()); }
            }
            int n = static_cast<int>(flat.size());
            if (hasComplex) return Value(ComplexMatrix(1, n, rc));
            return Value(RealMatrix(1, n, rd));
        }
        if (std::holds_alternative<ComplexMatrix>(args[1].data)) {
            auto flat = std::get<ComplexMatrix>(args[1].data).rawData();
            std::vector<Complex> rc;
            for (const auto& x : flat) rc.push_back(safeCallFunction(cl, { Value(x) }).asComplex());  // ★ 关键替换
            return Value(ComplexMatrix(1, static_cast<int>(flat.size()), rc));
        }
        throw std::runtime_error("Type Error: map() expects a vector/matrix/list.");
        });

    reg("filter", { 2 }, [](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        if (!cl->acceptsArgCount(1))
            throw std::runtime_error("Runtime Error: filter() requires a single-parameter function.");
        if (std::holds_alternative<List>(args[1].data)) {
            const auto& L = std::get<List>(args[1].data);
            List result;
            for (const auto& e : L.raw()) {
                if (isTruthy(safeCallFunction(cl, { anyToVal(e) }))) result.push_back(e); // ★
            }
            return Value(result);
        }
        if (std::holds_alternative<RealMatrix>(args[1].data)) {
            auto flat = std::get<RealMatrix>(args[1].data).rawData();
            std::vector<double> result;
            for (double x : flat) {
                if (isTruthy(safeCallFunction(cl, { Value(x) }))) result.push_back(x); // ★
            }
            int n = static_cast<int>(result.size());
            if (n == 0) return Value(RealMatrix(1, 0));
            return Value(RealMatrix(1, n, result));
        }
        if (std::holds_alternative<ComplexMatrix>(args[1].data)) {
            auto flat = std::get<ComplexMatrix>(args[1].data).rawData();
            std::vector<Complex> result;
            for (const auto& x : flat) {
                if (isTruthy(safeCallFunction(cl, { Value(x) }))) result.push_back(x); // ★
            }
            int n = static_cast<int>(result.size());
            if (n == 0) return Value(ComplexMatrix(1, 0));
            return Value(ComplexMatrix(1, n, result));
        }
        throw std::runtime_error("Type Error: filter() expects a vector/matrix/list.");
        });

    reg("reduce", { 2, 3 }, [](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        if (!cl->acceptsArgCount(2))
            throw std::runtime_error("Runtime Error: reduce() requires a two-parameter function.");
        if (std::holds_alternative<List>(args[1].data)) {
            const auto& L = std::get<List>(args[1].data);
            Value acc; size_t startIdx = 0;
            if (args.size() == 3) { acc = args[2]; }
            else { if (L.empty()) throw std::runtime_error("Runtime Error: reduce() on empty list without initial value."); acc = anyToVal(L.raw()[0]); startIdx = 1; }
            for (size_t i = startIdx; i < L.size(); ++i)
                acc = safeCallFunction(cl, { acc, anyToVal(L.raw()[i]) }); // ★
            return acc;
        }
        std::vector<double> flat;
        if (std::holds_alternative<RealMatrix>(args[1].data))
            flat = std::get<RealMatrix>(args[1].data).rawData();
        else if (std::holds_alternative<ComplexMatrix>(args[1].data)) {
            auto cd = std::get<ComplexMatrix>(args[1].data).rawData();
            for (const auto& c : cd) { if (!Tol::isEq(c.imag, 0.0)) throw std::runtime_error("reduce() requires real data."); flat.push_back(c.real); }
        }
        else throw std::runtime_error("Type Error: reduce() expects a vector/list.");
        Value acc; size_t startIdx = 0;
        if (args.size() == 3) { acc = args[2]; }
        else { if (flat.empty()) throw std::runtime_error("Runtime Error: reduce() on empty vector without initial value."); acc = Value(flat[0]); startIdx = 1; }
        for (size_t i = startIdx; i < flat.size(); ++i)
            acc = safeCallFunction(cl, { acc, Value(flat[i]) }); // ★
        return acc;
        });

    reg("any", { 2 }, [](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        if (!cl->acceptsArgCount(1)) throw std::runtime_error("Runtime Error: any() requires a single-parameter function.");
        if (std::holds_alternative<List>(args[1].data)) { for (const auto& e : std::get<List>(args[1].data).raw()) { if (isTruthy(safeCallFunction(cl, { anyToVal(e) }))) return Value(1.0); } return Value(0.0); } // ★
        auto flat = toVecHelper(args[1], "any");
        for (double x : flat) { if (isTruthy(safeCallFunction(cl, { Value(x) }))) return Value(1.0); } // ★
        return Value(0.0);
        });

    reg("all", { 2 }, [](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        if (!cl->acceptsArgCount(1)) throw std::runtime_error("Runtime Error: all() requires a single-parameter function.");
        if (std::holds_alternative<List>(args[1].data)) { for (const auto& e : std::get<List>(args[1].data).raw()) { if (!isTruthy(safeCallFunction(cl, { anyToVal(e) }))) return Value(0.0); } return Value(1.0); } // ★
        auto flat = toVecHelper(args[1], "all");
        for (double x : flat) { if (!isTruthy(safeCallFunction(cl, { Value(x) }))) return Value(0.0); } // ★
        return Value(1.0);
        });

    reg("countIf", { 2 }, [](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        if (!cl->acceptsArgCount(1)) throw std::runtime_error("Runtime Error: countIf() requires a single-parameter function.");
        int c = 0;
        if (std::holds_alternative<List>(args[1].data)) { for (const auto& e : std::get<List>(args[1].data).raw()) { if (isTruthy(safeCallFunction(cl, { anyToVal(e) }))) c++; } return Value(static_cast<double>(c)); } // ★
        auto flat = toVecHelper(args[1], "countIf");
        for (double x : flat) { if (isTruthy(safeCallFunction(cl, { Value(x) }))) c++; } // ★
        return Value(static_cast<double>(c));
        });

    reg("sort", { 1, 2 }, [](const std::vector<Value>& args) -> Value {
        if (args.size() == 1 && std::holds_alternative<RealMatrix>(args[0].data)) {
            auto f = std::get<RealMatrix>(args[0].data).rawData();
            std::sort(f.begin(), f.end());
            return Value(RealMatrix(1, static_cast<int>(f.size()), f));
        }
        if (args.size() == 1 && std::holds_alternative<List>(args[0].data)) {
            List L = std::get<List>(args[0].data);
            std::sort(L.raw().begin(), L.raw().end(), [](const std::any& a, const std::any& b) {
                std::ostringstream oa, ob; oa << anyToVal(a); ob << anyToVal(b);
                return oa.str() < ob.str();
                });
            return Value(L);
        }
        if (args.size() == 2) {
            auto cmp = args[1].asFunction();
            if (!cmp->acceptsArgCount(2))
                throw std::runtime_error("Runtime Error: sort() comparator must be a 2-parameter function.");
            if (std::holds_alternative<List>(args[0].data)) {
                List L = std::get<List>(args[0].data);
                std::sort(L.raw().begin(), L.raw().end(), [&](const std::any& a, const std::any& b) {
                    return isTruthy(safeCallFunction(cmp, { anyToVal(a), anyToVal(b) })); // ★
                    });
                return Value(L);
            }
            if (std::holds_alternative<RealMatrix>(args[0].data)) {
                auto f = std::get<RealMatrix>(args[0].data).rawData();
                std::sort(f.begin(), f.end(), [&](double a, double b) {
                    return isTruthy(safeCallFunction(cmp, { Value(a), Value(b) })); // ★
                    });
                return Value(RealMatrix(1, static_cast<int>(f.size()), f));
            }
            throw std::runtime_error("Type Error: sort() expects an array or list.");
        }
        throw std::runtime_error("Runtime Error: sort() expects 1 or 2 arguments.");
        });

    reg("strmap", { 2 }, [](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        if (!cl->acceptsArgCount(1)) throw std::runtime_error("Runtime Error: strmap() requires a single-parameter function.");
        if (!std::holds_alternative<StringMatrix>(args[1].data)) throw std::runtime_error("Type Error: strmap() expects a StringMatrix.");
        const auto& m = std::get<StringMatrix>(args[1].data);
        std::vector<std::string> result; result.reserve(m.getRows() * m.getCols());
        for (int i = 0; i < m.getRows(); ++i)
            for (int j = 0; j < m.getCols(); ++j) {
                Value y = safeCallFunction(cl, { Value(m(i, j)) }); // ★
                if (std::holds_alternative<std::string>(y.data)) result.push_back(std::get<std::string>(y.data));
                else { std::ostringstream oss; oss << y; result.push_back(oss.str()); }
            }
        return Value(StringMatrix(m.getRows(), m.getCols(), result));
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

    reg("diff", { 2 }, [evalFunc](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction(); double x = args[1].asDouble(); double h = 1e-4;
        double d = (-evalFunc(cl, x + 2 * h) + 8 * evalFunc(cl, x + h)
            - 8 * evalFunc(cl, x - h) + evalFunc(cl, x - 2 * h)) / (12 * h);
        return Value(d);
        });

    reg("integ", { 3, 4 }, [evalFunc](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction();
        double a = args[1].asDouble(), b = args[2].asDouble();
        int n = (args.size() == 4) ? static_cast<int>(std::round(args[3].asDouble())) : 100000;
        if (n <= 0 || n % 2 != 0) n = 100000;
        double h = (b - a) / n, s = evalFunc(cl, a) + evalFunc(cl, b);
        for (int i = 1; i < n; i += 2) s += 4 * evalFunc(cl, a + i * h);
        for (int i = 2; i < n - 1; i += 2) s += 2 * evalFunc(cl, a + i * h);
        return Value(s * h / 3.0);
        });

    reg("solveE", { 2 }, [evalFunc](const std::vector<Value>& args) -> Value {
        auto cl = args[0].asFunction(); double x = args[1].asDouble(); double h = 1e-5;
        for (int i = 0; i < 1000; ++i) {
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
        while (std::getline(file, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); List row; size_t pos = 0, found; while ((found = line.find(delim, pos)) != std::string::npos) { row.push_back(valToAny(Value(line.substr(pos, found - pos)))); pos = found + delim.size(); } row.push_back(valToAny(Value(line.substr(pos)))); rows.push_back(valToAny(Value(row))); }
        file.close(); return Value(rows);
        });

    reg("readCSVMat", { 1, 2 }, [](const std::vector<Value>& args) -> Value {
        if (!std::holds_alternative<std::string>(args[0].data)) throw std::runtime_error("Type Error: readCSVMat() expects a string path.");
        std::string path = safeResolvePath(std::get<std::string>(args[0].data));
        std::string delim = ",";
        if (args.size() == 2) { if (!std::holds_alternative<std::string>(args[1].data)) throw std::runtime_error("Type Error: readCSVMat() delimiter must be a string."); delim = std::get<std::string>(args[1].data); }
        std::ifstream file(path); if (!file.is_open()) throw std::runtime_error("IO Error: Cannot open file '" + path + "'.");
        std::vector<std::vector<std::string>> rowsData; std::string line; size_t maxCols = 0;
        while (std::getline(file, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); std::vector<std::string> row; size_t pos = 0, found; while ((found = line.find(delim, pos)) != std::string::npos) { row.push_back(line.substr(pos, found - pos)); pos = found + delim.size(); } row.push_back(line.substr(pos)); if (row.size() > maxCols) maxCols = row.size(); rowsData.push_back(row); }
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
        while (std::getline(file, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); if (line.empty()) continue; std::vector<double> row; size_t pos = 0, found; while ((found = line.find(delim, pos)) != std::string::npos) { try { row.push_back(std::stod(line.substr(pos, found - pos))); } catch (...) { row.push_back(0.0); } pos = found + delim.size(); } try { row.push_back(std::stod(line.substr(pos))); } catch (...) { row.push_back(0.0); } if (row.size() > maxCols) maxCols = row.size(); rowsData.push_back(row); }
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
            return Value(L);
        }
        catch (ErrorSignal& sig) {
            List L; L.push_back(valToAny(Value(0.0))); L.push_back(valToAny(Value(sig.message)));
            return Value(L);
        }
        catch (const std::exception& ex) {
            List L; L.push_back(valToAny(Value(0.0))); L.push_back(valToAny(Value(std::string(ex.what()))));
            return Value(L);
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
}

} // namespace jc
