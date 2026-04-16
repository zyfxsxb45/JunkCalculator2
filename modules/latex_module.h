#ifndef JC2_MODULE_LATEX_H
#define JC2_MODULE_LATEX_H

#include "../Module.h"
#include <sstream>
#include <iomanip>
#include <cctype>

namespace jc_latex {
    using namespace jc;

    // ====================================================================
    // 1. [序列化引擎] Object -> LaTeX
    // ====================================================================
    std::string valueToLatex(const Value& val) {
        if (std::holds_alternative<double>(val.data)) {
            std::ostringstream oss;
            oss << std::defaultfloat << std::setprecision(6) << std::get<double>(val.data);
            return oss.str();
        }
        else if (std::holds_alternative<BigInt>(val.data)) {
            return std::get<BigInt>(val.data).toString();
        }
        else if (std::holds_alternative<Fraction>(val.data)) {
            auto frac = std::get<Fraction>(val.data);
            return "\\frac{" + frac.getNum().toString() + "}{" + frac.getDen().toString() + "}";
        }
        else if (std::holds_alternative<Complex>(val.data)) {
            auto c = std::get<Complex>(val.data);
            std::ostringstream oss;
            oss << std::defaultfloat << std::setprecision(4);
            if (Tol::isEq(c.real, 0.0) && Tol::isEq(c.imag, 0.0)) return "0";
            if (!Tol::isEq(c.real, 0.0)) oss << c.real;
            if (!Tol::isEq(c.imag, 0.0)) {
                if (c.imag > 0 && !Tol::isEq(c.real, 0.0)) oss << "+";
                if (Tol::isEq(c.imag, 1.0)) oss << "i";
                else if (Tol::isEq(c.imag, -1.0)) oss << "-i";
                else oss << c.imag << "i";
            }
            return oss.str();
        }
        else if (std::holds_alternative<RealMatrix>(val.data)) {
            const auto& m = std::get<RealMatrix>(val.data);
            std::ostringstream oss;
            oss << "\\begin{pmatrix}\n";
            for (int i = 0; i < m.getRows(); ++i) {
                for (int j = 0; j < m.getCols(); ++j) {
                    oss << std::defaultfloat << std::setprecision(4) << m(i, j);
                    if (j < m.getCols() - 1) oss << " & ";
                }
                if (i < m.getRows() - 1) oss << " \\\\\n";
                else oss << "\n";
            }
            oss << "\\end{pmatrix}";
            return oss.str();
        }
        else if (std::holds_alternative<List>(val.data)) {
            const auto& l = std::get<List>(val.data);
            std::string s = "\\left[ ";
            for (size_t i = 0; i < l.size(); ++i) {
                s += valueToLatex(std::any_cast<Value>(l.raw()[i]));
                if (i < l.size() - 1) s += ", ";
            }
            s += " \\right]";
            return s;
        }
        else if (std::holds_alternative<std::string>(val.data)) {
            return "\\text{" + std::get<std::string>(val.data) + "}";
        }
        // 兜底：直接调用的 toString 逻辑
        std::ostringstream oss; oss << val; return oss.str();
    }


    // ====================================================================
    // 2. [解析引擎] LaTeX -> Expression (AST 实时求值)
    // ====================================================================
    class LatexParser {
        std::string src;
        size_t pos = 0;

        void skipSpace() {
            while (pos < src.size() && std::isspace(src[pos])) pos++;
        }

        bool match(char c) {
            skipSpace();
            if (pos < src.size() && src[pos] == c) { pos++; return true; }
            return false;
        }

        bool matchCmd(const std::string& cmd) {
            skipSpace();
            if (pos + cmd.size() <= src.size() && src.substr(pos, cmd.size()) == cmd) {
                pos += cmd.size();
                return true;
            }
            return false;
        }

        double parseFactor() {
            skipSpace();
            if (matchCmd("\\pi")) return 3.1415926535897932;
            if (matchCmd("e")) return 2.718281828459045;

            // 解析分数: \frac{A}{B}
            if (matchCmd("\\frac")) {
                if (!match('{')) throw std::runtime_error("LaTeX Parse Error: \\frac missing '{'");
                double num = parseExpr();
                if (!match('}')) throw std::runtime_error("LaTeX Parse Error: \\frac missing '}'");
                if (!match('{')) throw std::runtime_error("LaTeX Parse Error: \\frac missing second '{'");
                double den = parseExpr();
                if (!match('}')) throw std::runtime_error("LaTeX Parse Error: \\frac missing second '}'");
                return num / den;
            }
            // 解析根号: \sqrt{A}
            if (matchCmd("\\sqrt")) {
                if (!match('{')) throw std::runtime_error("LaTeX Parse Error: \\sqrt missing '{'");
                double v = parseExpr();
                if (!match('}')) throw std::runtime_error("LaTeX Parse Error: \\sqrt missing '}'");
                return std::sqrt(v);
            }
            // 解析括号或 LaTeX 分组
            if (match('(')) {
                double v = parseExpr();
                if (!match(')')) throw std::runtime_error("LaTeX Parse Error: Missing ')'");
                return v;
            }
            if (match('{')) {
                double v = parseExpr();
                if (!match('}')) throw std::runtime_error("LaTeX Parse Error: Missing '}'");
                return v;
            }

            // 解析数字
            size_t start = pos;
            while (pos < src.size() && (std::isdigit(src[pos]) || src[pos] == '.')) pos++;
            if (pos > start) {
                return std::stod(src.substr(start, pos - start));
            }

            throw std::runtime_error("LaTeX Parse Error: Unexpected token at " + src.substr(pos, 5) + "...");
        }

        double parsePower() {
            double v = parseFactor();
            skipSpace();
            if (match('^')) {
                double power = parseFactor(); // 解析指数 (支持 2^3 或 2^{x+1})
                v = std::pow(v, power);
            }
            return v;
        }

        double parseTerm() {
            double v = parsePower();
            while (true) {
                skipSpace();
                // 支持显式乘法、除法，以及隐式乘法 (如 2 \pi, 2(3), 2 \sqrt{2})
                if (match('*') || matchCmd("\\cdot") || matchCmd("\\times")) {
                    v *= parsePower();
                }
                else if (match('/')) {
                    v /= parsePower();
                }
                else {
                    // 检查隐式乘法预兆
                    if (pos < src.size() && (src[pos] == '(' || src[pos] == '\\' || std::isdigit(src[pos]) || src[pos] == 'e')) {
                        v *= parsePower();
                    }
                    else {
                        break;
                    }
                }
            }
            return v;
        }

    public:
        double parseExpr() {
            skipSpace();
            double v = 0;
            bool negate = false;
            if (match('-')) negate = true;
            else if (match('+')) negate = false;

            v = parseTerm();
            if (negate) v = -v;

            while (true) {
                skipSpace();
                if (match('+')) v += parseTerm();
                else if (match('-')) v -= parseTerm();
                else break;
            }
            return v;
        }

        double eval(const std::string& latex) {
            src = latex; pos = 0;
            return parseExpr();
        }
    };
} // namespace jc_latex

// ====================================================================
// 3. 将模块暴漏给 JC2 虚拟机
// ====================================================================
JC2_MODULE(latex) {
    using namespace jc_latex;
    jc::ModuleReg R(env, builtins, arity);

    // ★ 辅助包装器：将 C++ Lambda 包装为 JC2 的原生闭包对象
    auto makeNativeFn = [](const std::string& name, NativeCallable fn) {
        auto closure = std::make_shared<FunctionClosure>(
            std::vector<std::string>{}, std::vector<bool>{}, name, nullptr);
        closure->nativeFn = std::make_any<NativeCallable>(std::move(fn));
        return Value(closure);
        };

    // 暴露 to_latex(obj)
    R.set("to_latex", makeNativeFn("to_latex", [](const std::vector<Value>& args) -> Value {
        if (args.empty()) throw std::runtime_error("to_latex() requires 1 argument.");
        return Value(valueToLatex(args[0]));
        }));

    // 暴露 from_latex(string)
    R.set("from_latex", makeNativeFn("from_latex", [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("from_latex() requires a LaTeX string.");

        std::string latex_str = std::get<std::string>(args[0].data);

        LatexParser parser;
        try {
            double result = parser.eval(latex_str);
            return Value(result);
        }
        catch (const std::exception& e) {
            throw std::runtime_error(e.what());
        }
        }));
}

#endif // JC2_MODULE_LATEX_H
