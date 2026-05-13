#ifndef JC2_MODULE_LATEX_H
#define JC2_MODULE_LATEX_H

#include "Module.h"
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cmath>
#include <complex>    // ★ 引入 C++ 标准复数库
#include <memory>
#include <unordered_map>
#include <stdexcept>

namespace jc_latex {
    using namespace jc;
    // ★ 定义底层的运算基核为复数
    using cplx = std::complex<double>;

    // ====================================================================
    // 1. [序列化引擎] Object -> LaTeX
    // ====================================================================
    std::string valueToLatex(const Value& val) {
        if (val.isDouble()) {
            std::ostringstream oss; oss << std::defaultfloat << std::setprecision(6) << val.asDoubleRaw();
            return oss.str();
        }
        else if (val.isInt32()) return std::to_string(val.asInt32());
        else if (val.isObjType(ObjType::BIGINT)) return static_cast<ObjBigInt*>(val.asObj())->num.toString();
        else if (val.isObjType(ObjType::FRACTION)) {
            auto frac = static_cast<ObjFraction*>(val.asObj())->frac;
            return "\\frac{" + frac.getNum().toString() + "}{" + frac.getDen().toString() + "}";
        }
        else if (val.isObjType(ObjType::COMPLEX)) {
            auto c = static_cast<ObjComplex*>(val.asObj())->comp;
            std::ostringstream oss; oss << std::defaultfloat << std::setprecision(4);
            if (c.real == 0.0 && c.imag == 0.0) return "0";
            if (c.real != 0.0) oss << c.real;
            if (c.imag != 0.0) {
                if (c.imag > 0 && c.real != 0.0) oss << "+";
                if (Tol::isEq(c.imag, 1.0)) oss << "i";
                else if (Tol::isEq(c.imag, -1.0)) oss << "-i";
                else oss << c.imag << "i";
            }
            return oss.str();
        }
        else if (val.isObjType(ObjType::REAL_MATRIX)) {
            const auto& m = static_cast<ObjRealMatrix*>(val.asObj())->mat;
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
        else if (val.isObjType(ObjType::LIST)) {
            const auto& l = static_cast<ObjList*>(val.asObj())->vec;
            std::string s = "\\left[ ";
            for (size_t i = 0; i < l.size(); ++i) {
                s += valueToLatex(l[i]);
                if (i < l.size() - 1) s += ", ";
            }
            return s + " \\right]";
        }
        std::ostringstream oss; oss << val; return oss.str();
    }

    // ====================================================================
    // 2. [底层抽象语法树 AST] 承载编译后的 LaTeX 极速数学逻辑 (全复平面支持)
    // ====================================================================
    class ExprNode {
    public:
        // ★ 将原本的 double 返回值升级为 std::complex<double>
        virtual cplx eval(const std::unordered_map<std::string, cplx>& env) const = 0;
        virtual ~ExprNode() = default;
    };
    using ExprPtr = std::shared_ptr<ExprNode>;

    class NumNode : public ExprNode {
        double val;
    public:
        NumNode(double v) : val(v) {}
        cplx eval(const std::unordered_map<std::string, cplx>&) const override { return cplx(val, 0.0); }
    };

    class VarNode : public ExprNode {
        std::string name;
    public:
        VarNode(std::string n) : name(n) {}
        cplx eval(const std::unordered_map<std::string, cplx>& env) const override {
            if (name == "\\pi" || name == "pi") return cplx(3.1415926535897932, 0.0);
            if (name == "e") return cplx(2.718281828459045, 0.0);
            // ★ 神来之笔：原生支持复数单位 i 和 j
            if (name == "i" || name == "j") return cplx(0.0, 1.0);

            auto it = env.find(name);
            if (it == env.end()) throw std::runtime_error("LaTeX Math Error: Unknown variable '" + name + "'");
            return it->second;
        }
    };

    class BinOpNode : public ExprNode {
        char op; ExprPtr left, right;
    public:
        BinOpNode(char o, ExprPtr l, ExprPtr r) : op(o), left(l), right(r) {}
        cplx eval(const std::unordered_map<std::string, cplx>& env) const override {
            cplx l = left->eval(env), r = right->eval(env);
            // ★ \complex 完全支持基础算术与复数次幂！
            switch (op) {
            case '+': return l + r; case '-': return l - r;
            case '*': return l * r; case '/': return l / r;
            case '^': return std::pow(l, r);
            default: return cplx(0, 0);
            }
        }
    };

    class FuncNode : public ExprNode {
        std::string func; ExprPtr arg;
    public:
        FuncNode(std::string f, ExprPtr a) : func(f), arg(a) {}
        cplx eval(const std::unordered_map<std::string, cplx>& env) const override {
            cplx a = arg->eval(env);
            // ★ cmath 全部拥有复数同名方法重载，天然平替！
            if (func == "\\sin") return std::sin(a); if (func == "\\cos") return std::cos(a);
            if (func == "\\tan") return std::tan(a); if (func == "\\sqrt") return std::sqrt(a);
            if (func == "\\ln") return std::log(a);  if (func == "\\log") return std::log10(a); // log10 兼容降级
            if (func == "\\exp") return std::exp(a);
            if (func == "\\abs") return cplx(std::abs(a), 0); // abs 返回模长
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
            if (matchCmd("\\frac")) {
                if (!match('{')) throw std::runtime_error("Expected '{' after \\frac");
                auto num = parseExpr();
                if (!match('}')) throw std::runtime_error("Expected '}' after numerator");
                if (!match('{')) throw std::runtime_error("Expected '{' for denominator");
                auto den = parseExpr();
                if (!match('}')) throw std::runtime_error("Expected '}' after denominator");
                return std::make_shared<BinOpNode>('/', num, den);
            }

            std::string cmd = peekCmd();
            if (cmd == "\\sin" || cmd == "\\cos" || cmd == "\\tan" || cmd == "\\sqrt" ||
                cmd == "\\ln" || cmd == "\\log" || cmd == "\\exp" || cmd == "\\abs") {
                pos += cmd.size();
                skipSpace();

                ExprPtr arg;
                if (match('(')) {
                    arg = parseExpr();
                    if (!match(')')) throw std::runtime_error("Missing ')' for " + cmd);
                }
                else if (match('{')) {
                    arg = parseExpr();
                    if (!match('}')) throw std::runtime_error("Missing '}' for " + cmd);
                }
                else {
                    arg = parsePower();
                }
                return std::make_shared<FuncNode>(cmd, arg);
            }

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

            size_t start = pos;
            while (pos < src.size() && (std::isdigit(src[pos]) || src[pos] == '.')) pos++;
            if (pos > start) return std::make_shared<NumNode>(std::stod(src.substr(start, pos - start)));

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
// 4. 将模块注册暴露给 JC2 桥接
// ====================================================================
JC2_MODULE(latex) {
    using namespace jc_latex;
    jc::ModuleReg R(env, builtins, arity);

    auto makeNativeFn = std::make_shared<std::function<Value(const std::string&, int, NativeCallable)>>(
        [](const std::string& name, int argCount, NativeCallable fn) -> Value {
            std::vector<std::string> pNames(argCount, "_");
            std::vector<bool> pRefs(argCount, false);
            auto cls = GcHeap::get().allocate<ObjClosure>(pNames, pRefs, name, nullptr);
            cls->defaultValues.resize(argCount, Value::none());
            cls->nativeFn = std::make_any<NativeCallable>(std::move(fn));
            return Value(cls);
        }
    );

    R.set("to_latex", (*makeNativeFn)("to_latex", 1, [](const std::vector<Value>& args) -> Value {
        if (args.empty()) throw std::runtime_error("to_latex() requires 1 argument.");
        return Value(valueToLatex(args[0]));
        }));

    R.set("eval_latex", (*makeNativeFn)("eval_latex", 1, [](const std::vector<Value>& args) -> Value {
        if (args.empty() || !args[0].isString())
            throw std::runtime_error("eval_latex() requires a LaTeX string.");
        LatexParser parser;
        auto ast = parser.compile(args[0].asString());

        // ★ 核心收尾：C++ 的 complex 落回到 JC2 的类型系统中
        cplx res = ast->eval({});
        if (res.imag() == 0.0) return Value(res.real()); // 纯虚部为 0 降级为 double
        return Value(Complex(res.real(), res.imag()));           // 否则以复数形式返还！
        }));

    R.set("compile_latex", (*makeNativeFn)("compile_latex", 2, [makeNativeFn](const std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !args[0].isString())
            throw std::runtime_error("compile_latex(string, vars): Requires formula and variable names.");

        std::string latex_str = args[0].asString();
        std::vector<std::string> varNames;

        if (args[1].isObjType(ObjType::LIST)) {
            for (const auto& v : static_cast<ObjList*>(args[1].asObj())->vec) {
                if (!v.isString()) throw std::runtime_error("Variable names must be strings.");
                varNames.push_back(v.asString());
            }
        }
        else if (args[1].isObjType(ObjType::STRING_MATRIX)) {
            for (const auto& s : static_cast<ObjStringMatrix*>(args[1].asObj())->mat.rawData()) {
                varNames.push_back(s);
            }
        }
        else throw std::runtime_error("compile_latex(): 2nd argument must be a List or Matrix of variable strings.");

        LatexParser parser;
        ExprPtr ast = parser.compile(latex_str);

        auto jc_caller = [ast, varNames, latex_str](const std::vector<Value>& call_args) -> Value {
            if (call_args.size() != varNames.size())
                throw std::runtime_error("LaTeX Func '" + latex_str + "' expects " + std::to_string(varNames.size()) + " arguments.");

            // ★ 新的参数拆包逻辑：兼容用户传入 JC2_Complex 或者 JC2_Double
            std::unordered_map<std::string, cplx> env;
            for (size_t i = 0; i < varNames.size(); ++i) {
                if (call_args[i].isObjType(ObjType::COMPLEX)) {
                    auto c = static_cast<ObjComplex*>(call_args[i].asObj())->comp;
                    env[varNames[i]] = cplx(c.real, c.imag);
                }
                else {
                    env[varNames[i]] = cplx(call_args[i].asDouble(), 0.0);
                }
            }

            cplx res = ast->eval(env);
            if (res.imag() == 0.0) return Value(res.real());
            return Value(Complex(res.real(), res.imag()));
            };
        return (*makeNativeFn)("latex_lambda", static_cast<int>(varNames.size()), jc_caller);
        }));
}
#endif // JC2_MODULE_LATEX_H
