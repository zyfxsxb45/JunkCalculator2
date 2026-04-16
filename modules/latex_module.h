#ifndef JC2_MODULE_LATEX_H
#define JC2_MODULE_LATEX_H

#include "../Module.h"
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <stdexcept>

namespace jc_latex {
    using namespace jc;

    // ====================================================================
    // 1. [序列化引擎] Object -> LaTeX
    // ====================================================================
    std::string valueToLatex(const Value& val) {
        if (std::holds_alternative<double>(val.data)) {
            std::ostringstream oss; oss << std::defaultfloat << std::setprecision(6) << std::get<double>(val.data);
            return oss.str();
        }
        else if (std::holds_alternative<BigInt>(val.data)) return std::get<BigInt>(val.data).toString();
        else if (std::holds_alternative<Fraction>(val.data)) {
            auto frac = std::get<Fraction>(val.data);
            return "\\frac{" + frac.getNum().toString() + "}{" + frac.getDen().toString() + "}";
        }
        else if (std::holds_alternative<Complex>(val.data)) {
            auto c = std::get<Complex>(val.data);
            std::ostringstream oss; oss << std::defaultfloat << std::setprecision(4);
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
            std::ostringstream oss; oss << "\\begin{pmatrix}\n";
            for (int i = 0; i < m.getRows(); ++i) {
                for (int j = 0; j < m.getCols(); ++j) {
                    oss << std::defaultfloat << std::setprecision(4) << m(i, j);
                    if (j < m.getCols() - 1) oss << " & ";
                }
                oss << (i < m.getRows() - 1 ? " \\\\\n" : "\n");
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
            return s + " \\right]";
        }
        std::ostringstream oss; oss << val; return oss.str();
    }

    // ====================================================================
    // 2. [底层抽象语法树 AST] 承载编译后的 LaTeX 极速数学逻辑
    // ====================================================================
    class ExprNode {
    public:
        virtual double eval(const std::unordered_map<std::string, double>& env) const = 0;
        virtual ~ExprNode() = default;
    };
    using ExprPtr = std::shared_ptr<ExprNode>;

    class NumNode : public ExprNode {
        double val;
    public:
        NumNode(double v) : val(v) {}
        double eval(const std::unordered_map<std::string, double>&) const override { return val; }
    };

    class VarNode : public ExprNode {
        std::string name;
    public:
        VarNode(std::string n) : name(n) {}
        double eval(const std::unordered_map<std::string, double>& env) const override {
            if (name == "\\pi") return 3.1415926535897932;
            if (name == "e") return 2.718281828459045;
            auto it = env.find(name);
            if (it == env.end()) throw std::runtime_error("LaTeX Math Error: Unknown variable '" + name + "'");
            return it->second;
        }
    };

    class BinOpNode : public ExprNode {
        char op; ExprPtr left, right;
    public:
        BinOpNode(char o, ExprPtr l, ExprPtr r) : op(o), left(l), right(r) {}
        double eval(const std::unordered_map<std::string, double>& env) const override {
            double l = left->eval(env), r = right->eval(env);
            switch (op) {
            case '+': return l + r; case '-': return l - r;
            case '*': return l * r; case '/': return l / r;
            case '^': return std::pow(l, r);
            default: return 0;
            }
        }
    };

    class FuncNode : public ExprNode {
        std::string func; ExprPtr arg;
    public:
        FuncNode(std::string f, ExprPtr a) : func(f), arg(a) {}
        double eval(const std::unordered_map<std::string, double>& env) const override {
            double a = arg->eval(env);
            if (func == "\\sin") return std::sin(a); if (func == "\\cos") return std::cos(a);
            if (func == "\\tan") return std::tan(a); if (func == "\\sqrt") return std::sqrt(a);
            if (func == "\\ln") return std::log(a);  if (func == "\\log") return std::log10(a);
            if (func == "\\exp") return std::exp(a); if (func == "\\abs") return std::abs(a);
            throw std::runtime_error("LaTeX Math Error: Unsupported function '" + func + "'");
        }
    };

    // ====================================================================
    // 3. [递归下降编译器] 将人类文本解析为 AST
    // ====================================================================
    class LatexParser {
        std::string src; size_t pos = 0;

        void skipSpace() { while (pos < src.size() && std::isspace(src[pos])) pos++; }
        bool match(char c) { skipSpace(); if (pos < src.size() && src[pos] == c) { pos++; return true; } return false; }

        std::string peekCmd() {
            skipSpace();
            if (pos < src.size() && src[pos] == '\\') {
                size_t p = pos + 1;
                while (p < src.size() && std::isalpha(src[p])) p++;
                return src.substr(pos, p - pos);
            }
            return "";
        }

        bool matchCmd(const std::string& cmd) {
            if (peekCmd() == cmd) { pos += cmd.size(); return true; }
            return false;
        }

        ExprPtr parseFactor() {
            skipSpace();
            // \frac{A}{B}
            if (matchCmd("\\frac")) {
                if (!match('{')) throw std::runtime_error("Expected '{' after \\frac");
                auto num = parseExpr();
                if (!match('}')) throw std::runtime_error("Expected '}' after numerator");
                if (!match('{')) throw std::runtime_error("Expected '{' for denominator");
                auto den = parseExpr();
                if (!match('}')) throw std::runtime_error("Expected '}' after denominator");
                return std::make_shared<BinOpNode>('/', num, den);
            }

            // Math Functions: \sin, \cos, \sqrt
            std::string cmd = peekCmd();
            if (cmd == "\\sin" || cmd == "\\cos" || cmd == "\\tan" || cmd == "\\sqrt" ||
                cmd == "\\ln" || cmd == "\\log" || cmd == "\\exp" || cmd == "\\abs") {
                pos += cmd.size();
                skipSpace();

                ExprPtr arg;
                if (match('(')) {
                    arg = parseExpr(); // ★ 遇到括号，解析完整表达式！能吃掉所有加减乘除
                    if (!match(')')) throw std::runtime_error("Missing ')' for " + cmd);
                }
                else if (match('{')) {
                    arg = parseExpr(); // ★ LaTeX 标准花括号包围
                    if (!match('}')) throw std::runtime_error("Missing '}' for " + cmd);
                }
                else {
                    arg = parsePower(); // 隐式乘法 (如 \sin x)，只吃一小块
                }
                return std::make_shared<FuncNode>(cmd, arg);
            }
            // Parentheses (单独处理分组)
            if (match('(')) {
                auto expr = parseExpr();
                if (!match(')')) throw std::runtime_error("Missing closing ')'");
                return expr;
            }
            if (match('{')) {
                auto expr = parseExpr();
                if (!match('}')) throw std::runtime_error("Missing closing '}'");
                return expr;
            }
            if (match('[')) {
                auto expr = parseExpr();
                if (!match(']')) throw std::runtime_error("Missing closing ']'");
                return expr;
            }

            // Numbers
            size_t start = pos;
            while (pos < src.size() && (std::isdigit(src[pos]) || src[pos] == '.')) pos++;
            if (pos > start) return std::make_shared<NumNode>(std::stod(src.substr(start, pos - start)));

            // Variables (e, \pi, \theta, x, y...)
            if (cmd != "") {
                pos += cmd.size();
                return std::make_shared<VarNode>(cmd);
            }
            if (pos < src.size() && std::isalpha(src[pos])) {
                std::string varStr(1, src[pos++]);
                return std::make_shared<VarNode>(varStr);
            }

            throw std::runtime_error("LaTeX Parse Error: Unexpected token at '" + src.substr(pos, 5) + "...'");
        }

        ExprPtr parsePower() {
            auto left = parseFactor();
            skipSpace();
            if (match('^')) {
                auto right = parseFactor();
                return std::make_shared<BinOpNode>('^', left, right);
            }
            return left;
        }

        ExprPtr parseTerm() {
            auto left = parsePower();
            while (true) {
                skipSpace();
                if (match('*') || matchCmd("\\cdot") || matchCmd("\\times")) {
                    left = std::make_shared<BinOpNode>('*', left, parsePower());
                }
                else if (match('/')) {
                    left = std::make_shared<BinOpNode>('/', left, parsePower());
                }
                else {
                    // ★ 神级特性：隐式乘法 (e.g. "2x", "xy", "2 \sin(x)")
                    if (pos < src.size() && (src[pos] == '(' || src[pos] == '\\' || std::isalpha(src[pos]))) {
                        left = std::make_shared<BinOpNode>('*', left, parsePower());
                    }
                    else break;
                }
            }
            return left;
        }

    public:
        ExprPtr parseExpr() {
            skipSpace();
            ExprPtr left;
            bool negate = false;
            if (match('-')) negate = true;
            else match('+');

            left = parseTerm();
            if (negate) left = std::make_shared<BinOpNode>('*', std::make_shared<NumNode>(-1), left);

            while (true) {
                skipSpace();
                if (match('+')) left = std::make_shared<BinOpNode>('+', left, parseTerm());
                else if (match('-')) left = std::make_shared<BinOpNode>('-', left, parseTerm());
                else break;
            }
            return left;
        }

        ExprPtr compile(const std::string& latex) {
            src = latex; pos = 0;
            return parseExpr();
        }
    };
} // namespace jc_latex

// ====================================================================
// 4. 将模块注册暴露给 JC2
// ====================================================================
JC2_MODULE(latex) {
    using namespace jc_latex;
    jc::ModuleReg R(env, builtins, arity);
    // ★ 升级了的包装器：动态向 VM 申报这究竟是几个参数的函数！
    auto makeNativeFn = std::make_shared<std::function<Value(const std::string&, int, NativeCallable)>>(
        [](const std::string& name, int argCount, NativeCallable fn) -> Value {
            // 通过填入虚假的参数名，强行告知 VM 正确的 Arity！
            std::vector<std::string> pNames(argCount, "_");
            std::vector<bool> pRefs(argCount, false);
            auto cls = std::make_shared<FunctionClosure>(pNames, pRefs, name, nullptr);
            cls->defaultValues.resize(argCount, Value::none());
            cls->nativeFn = std::make_any<NativeCallable>(std::move(fn));
            return Value(cls);
        }
    );
    // 1. to_latex(obj)
    R.set("to_latex", (*makeNativeFn)("to_latex", 1, [](const std::vector<Value>& args) -> Value {
        if (args.empty()) throw std::runtime_error("to_latex() requires 1 argument.");
        return Value(valueToLatex(args[0]));
        }));
    // 2. eval_latex(string)
    R.set("eval_latex", (*makeNativeFn)("eval_latex", 1, [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("eval_latex() requires a LaTeX string.");
        LatexParser parser;
        auto ast = parser.compile(std::get<std::string>(args[0].data));
        return Value(ast->eval({}));
        }));
    // 3. compile_latex(string, List | StringMatrix)
    R.set("compile_latex", (*makeNativeFn)("compile_latex", 2, [makeNativeFn](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("compile_latex(string, vars): Requires formula and variable names.");

        std::string latex_str = std::get<std::string>(args[0].data);
        std::vector<std::string> varNames;
        // ★ 智能多态识别：容忍用户输入 ["x", "y"] (矩阵) 或 list("x", "y") (列表)！
        if (std::holds_alternative<List>(args[1].data)) {
            for (const auto& anyVar : std::get<List>(args[1].data).raw()) {
                Value v = std::any_cast<Value>(anyVar);
                if (!std::holds_alternative<std::string>(v.data)) throw std::runtime_error("Variable names must be strings.");
                varNames.push_back(std::get<std::string>(v.data));
            }
        }
        else if (std::holds_alternative<StringMatrix>(args[1].data)) {
            for (const auto& s : std::get<StringMatrix>(args[1].data).rawData()) {
                varNames.push_back(s);
            }
        }
        else {
            throw std::runtime_error("compile_latex(): 2nd argument must be a List or Matrix of variable strings.");
        }
        // 编译为 AST
        LatexParser parser;
        ExprPtr ast = parser.compile(latex_str);
        auto jc_caller = [ast, varNames, latex_str](const std::vector<Value>& call_args) -> Value {
            if (call_args.size() != varNames.size())
                throw std::runtime_error("LaTeX Func '" + latex_str + "' expects " + std::to_string(varNames.size()) + " arguments.");

            std::unordered_map<std::string, double> env;
            for (size_t i = 0; i < varNames.size(); ++i) {
                env[varNames[i]] = call_args[i].asDouble();
            }
            return Value(ast->eval(env));
            };
        // ★ 告诉 VM，返回的这个 JIT 函数 exactement 拥有 varNames.size() 个参数
        return (*makeNativeFn)("latex_lambda", static_cast<int>(varNames.size()), jc_caller);
        }));
}
#endif // JC2_MODULE_LATEX_H
