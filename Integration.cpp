#include "Integration.h"
#include "SymRules.h"
#include <stdexcept>
#include <algorithm>
#include <numeric>
#include <vector>
#include <map>
#include <functional>

namespace jc {

    // =================================================================
    // 内部辅助函数 (Internal Helpers)
    // =================================================================
    static bool isCasNegative(const CASVal& v) {
        return std::visit([](auto&& arg) -> bool {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, BigInt>) return arg.isNegative();
            else if constexpr (std::is_same_v<T, Fraction>) return arg.getNum().isNegative() != arg.getDen().isNegative();
            else if constexpr (std::is_same_v<T, double>) return arg < 0.0;
            else return false;
        }, v);
    }

    // =================================================================
    // 实数域有理分式积分模版 (Rational Function Integration Templates)
    // =================================================================
    static SymExpr integratePartialFraction(SymExpr N, SymExpr D, int k, const std::string& var) {
        int degD = getDegree(D, var);
        auto coeffsD = extractCoeffs(D, var);
        auto coeffsN = extractCoeffs(N, var);
        
        SymExpr X = SymExpr::makeVar(var);

        if (degD == 1) {
            SymExpr a = coeffsD[1];
            SymExpr b = coeffsD[0];
            SymExpr B = simplifyCore(b / a);
            
            SymExpr A_num = coeffsN.empty() ? SymExpr(BigInt(0)) : coeffsN[0];
            SymExpr A = simplifyCore(A_num / (a ^ SymExpr(BigInt(k))));

            if (A.isZero()) return SymExpr(BigInt(0));

            if (k == 1) {
                return A * SymExpr(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{simplifyCore(X + B).ptr}));
            } else {
                return simplifyCore((A / SymExpr(BigInt(1 - k))) * ((X + B) ^ SymExpr(BigInt(1 - k))));
            }
        } else if (degD == 2) {
            SymExpr a = coeffsD[2];
            SymExpr b = coeffsD[1];
            SymExpr c = coeffsD[0];
            SymExpr p = simplifyCore(b / a);
            SymExpr q = simplifyCore(c / a);

            SymExpr A_num = coeffsN.size() > 1 ? coeffsN[1] : SymExpr(BigInt(0));
            SymExpr B_num = coeffsN.size() > 0 ? coeffsN[0] : SymExpr(BigInt(0));
            
            SymExpr ak = a ^ SymExpr(BigInt(k));
            SymExpr A = simplifyCore(A_num / ak);
            SymExpr B = simplifyCore(B_num / ak);

            SymExpr C = simplifyCore(B - (A * p) / SymExpr(BigInt(2)));
            SymExpr Delta = simplifyCore(q - (p * p) / SymExpr(BigInt(4)));
            
            SymExpr res(BigInt(0));
            SymExpr quadratic = simplifyCore((X ^ SymExpr(BigInt(2))) + p * X + q);

            if (!A.isZero()) {
                if (k == 1) {
                    res = res + (A / SymExpr(BigInt(2))) * SymExpr(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{quadratic.ptr}));
                } else {
                    res = res + (A / (SymExpr(BigInt(2)) * SymExpr(BigInt(1 - k)))) * (quadratic ^ SymExpr(BigInt(1 - k)));
                }
            }

            if (!C.isZero()) {
                std::function<SymExpr(int)> integrateArctan = [&](int power) -> SymExpr {
                    if (power == 1) {
                        SymExpr sqrtDelta = simplifyCore(Delta ^ SymExpr(Fraction(1, 2)));
                        SymExpr atanArg = simplifyCore((X + p / SymExpr(BigInt(2))) / sqrtDelta);
                        return (SymExpr(BigInt(1)) / sqrtDelta) * SymExpr(std::make_shared<SymFunc>("atan", std::vector<std::shared_ptr<SymNode>>{atanArg.ptr}));
                    } else {
                        SymExpr u = simplifyCore(X + p / SymExpr(BigInt(2)));
                        SymExpr term1 = u / (SymExpr(BigInt(2)) * Delta * SymExpr(BigInt(power - 1)) * ((u * u + Delta) ^ SymExpr(BigInt(power - 1))));
                        SymExpr term2 = (SymExpr(BigInt(2 * power - 3)) / (SymExpr(BigInt(2)) * Delta * SymExpr(BigInt(power - 1)))) * integrateArctan(power - 1);
                        return simplifyCore(term1 + term2);
                    }
                };
                res = res + C * integrateArctan(k);
            }
            return res;
        }
        throw std::runtime_error("Calculus Error: Denominator degree > 2 not supported in partial fraction integration.");
    }

    // =================================================================
    // 提取有理分式的分子和分母 (Get Numerator and Denominator)
    // =================================================================
    static std::pair<SymExpr, SymExpr> getFraction(const SymExpr& expr) {
        if (!expr.ptr) return {expr, SymExpr(BigInt(1))};
        switch (expr.ptr->getType()) {
            case SymType::NUM:
            case SymType::VAR:
                return {expr, SymExpr(BigInt(1))};
            case SymType::ADD: {
                auto add = std::static_pointer_cast<SymAdd>(expr.ptr);
                std::vector<std::pair<SymExpr, SymExpr>> nds;
                for (auto& arg : add->args) nds.push_back(getFraction(SymExpr(arg)));
                SymExpr den(BigInt(1));
                for (auto& nd : nds) den = simplifyCore(den * nd.second);
                SymExpr num(BigInt(0));
                for (size_t i = 0; i < nds.size(); ++i) {
                    SymExpr termNum = nds[i].first;
                    for (size_t j = 0; j < nds.size(); ++j) {
                        if (i != j) termNum = simplifyCore(expand(termNum * nds[j].second, 500));
                    }
                    num = simplifyCore(expand(num + termNum, 500));
                }
                return {num, den};
            }
            case SymType::MUL: {
                auto mul = std::static_pointer_cast<SymMul>(expr.ptr);
                SymExpr num(BigInt(1));
                SymExpr den(BigInt(1));
                for (auto& arg : mul->args) {
                    auto nd = getFraction(SymExpr(arg));
                    num = simplifyCore(expand(num * nd.first, 500));
                    den = simplifyCore(den * nd.second);
                }
                return {num, den};
            }
            case SymType::POW: {
                auto powNode = std::static_pointer_cast<SymPow>(expr.ptr);
                if (powNode->exp->getType() == SymType::NUM) {
                    auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                    if (isInt) {
                        auto nd = getFraction(SymExpr(powNode->base));
                        if (n >= 0) {
                            return {simplifyCore(expand(nd.first ^ SymExpr(BigInt(n)), 500)), simplifyCore(nd.second ^ SymExpr(BigInt(n)))};
                        } else {
                            return {simplifyCore(expand(nd.second ^ SymExpr(BigInt(-n)), 500)), simplifyCore(nd.first ^ SymExpr(BigInt(-n)))};
                        }
                    }
                }
                return {expr, SymExpr(BigInt(1))};
            }
            case SymType::FUNC:
                return {expr, SymExpr(BigInt(1))};
        }
        return {expr, SymExpr(BigInt(1))};
    }

    // =================================================================
    // 🚀 Risch 算法核心组件 (Risch Algorithm Core Components)
    // =================================================================
    enum class RischExtType { LOG, EXP };

    struct RischExtension {
        RischExtType type = RischExtType::LOG;
        SymExpr arg;       // 扩张的参数 u (如 log(u) 或 exp(u) 中的 u)
        SymExpr deriv;     // 导数 t'
        std::string name;  // 变量名 (如 _t1)
        SymExpr t_var;     // 变量节点
    };

    struct RischDiffField {
        std::string x_var;
        std::vector<RischExtension> tower;

        // 将表达式重写为微分域变量的代数式
        SymExpr rewrite(const SymExpr& expr) const {
            SymExpr res = expr;
            // 从塔顶向下替换，确保嵌套扩张被正确处理 (如 exp(exp(x)))
            for (auto it = tower.rbegin(); it != tower.rend(); ++it) {
                if (it->type == RischExtType::LOG) {
                    SymExpr targetLog(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{it->arg.ptr}));
                    res = applyRule(res, targetLog, it->t_var);
                    SymExpr targetLn(std::make_shared<SymFunc>("ln", std::vector<std::shared_ptr<SymNode>>{it->arg.ptr}));
                    res = applyRule(res, targetLn, it->t_var);
                } else if (it->type == RischExtType::EXP) {
                    SymExpr targetExp(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{it->arg.ptr}));
                    res = applyRule(res, targetExp, it->t_var);
                }
            }
            return res;
        }
    };

    static void buildRischFieldRec(const SymExpr& expr, const std::string& x, RischDiffField& field, int& counter) {
        if (!expr.ptr) return;
        
        // 后序遍历：确保内层扩张先被加入塔中
        switch (expr.ptr->getType()) {
            case SymType::ADD:
                for (auto& arg : std::static_pointer_cast<SymAdd>(expr.ptr)->args) buildRischFieldRec(SymExpr(arg), x, field, counter);
                break;
            case SymType::MUL:
                for (auto& arg : std::static_pointer_cast<SymMul>(expr.ptr)->args) buildRischFieldRec(SymExpr(arg), x, field, counter);
                break;
            case SymType::POW: {
                auto p = std::static_pointer_cast<SymPow>(expr.ptr);
                buildRischFieldRec(SymExpr(p->base), x, field, counter);
                buildRischFieldRec(SymExpr(p->exp), x, field, counter);
                break;
            }
            case SymType::FUNC: {
                auto f = std::static_pointer_cast<SymFunc>(expr.ptr);
                for (auto& arg : f->args) buildRischFieldRec(SymExpr(arg), x, field, counter);
                
                if (f->name == "log" || f->name == "ln" || f->name == "exp") {
                    SymExpr arg(f->args[0]);
                    if (!containsVar(arg.ptr, x)) break; // 常数不构成扩张
                    
                    // 检查是否已在塔中
                    bool exists = false;
                    for (const auto& ext : field.tower) {
                        if ((ext.type == RischExtType::LOG && (f->name == "log" || f->name == "ln")) ||
                            (ext.type == RischExtType::EXP && f->name == "exp")) {
                            if (ext.arg.toString() == arg.toString()) {
                                exists = true;
                                break;
                            }
                        }
                    }
                    
                    if (!exists) {
                        RischExtension ext;
                        ext.type = (f->name == "exp") ? RischExtType::EXP : RischExtType::LOG;
                        ext.arg = arg;
                        ext.name = "_t" + std::to_string(++counter);
                        ext.t_var = SymExpr::makeVar(ext.name);
                        
                        // 计算导数 t'
                        SymExpr du = diff(arg, x);
                        if (ext.type == RischExtType::LOG) {
                            ext.deriv = simplifyCore(du / arg);
                        } else {
                            ext.deriv = simplifyCore(du * ext.t_var);
                        }
                        
                        field.tower.push_back(ext);
                    }
                }
                break;
            }
            default: break;
        }
    }

    static RischDiffField buildRischDifferentialField(const SymExpr& expr, const std::string& x) {
        RischDiffField field;
        field.x_var = x;
        int counter = 0;
        buildRischFieldRec(expr, x, field, counter);
        
        // 将导数重写为域变量的代数式
        for (auto& ext : field.tower) {
            ext.deriv = field.rewrite(ext.deriv);
        }
        return field;
    }

    // =================================================================
    // 🚀 Risch 算法 Step 2: Hermite 约化 (Hermite Reduction)
    // =================================================================
    static std::pair<SymExpr, SymExpr> hermiteReduce(SymExpr A, SymExpr D, const std::string& var) {
        A = simplifyCore(expand(A, 500));
        D = simplifyCore(expand(D, 500));
        
        auto [Q, R] = polyDiv(A, D, var);
        SymExpr rationalPart(0);
        SymExpr integralPart = Q; 
        
        A = R;
        if (A.isZero()) return {rationalPart, integralPart};

        auto sqFree = polySquareFree(D, var);
        int m = 0;
        for (const auto& p : sqFree) m = std::max(m, p.second);

        SymExpr currentA = A;
        SymExpr currentD = D;

        for (int i = m; i >= 2; --i) {
            SymExpr V(1);
            for (auto& p : sqFree) {
                if (p.second == i) V = p.first;
            }
            if (getDegree(V, var) <= 0) continue;

            SymExpr Vi = simplifyCore(expand(V ^ SymExpr(i), 500));
            SymExpr U = polyDiv(currentD, Vi, var).first;

            SymExpr dV = diff(V, var);
            SymExpr U_dV = simplifyCore(expand(U * dV, 500));

            auto [gcd, S, T] = polyEGCD(U_dV, V, var);
            SymExpr factor = simplifyCore(expand(currentA / gcd, 500));
            SymExpr B = simplifyCore(expand(S * factor, 500));
            SymExpr C = simplifyCore(expand(T * factor, 500));

            auto [q, r] = polyDiv(B, V, var);
            B = r;
            C = simplifyCore(expand(C + q * U_dV, 500));

            SymExpr V_im1 = simplifyCore(expand(V ^ SymExpr(i - 1), 500));
            SymExpr denom = simplifyCore(expand(SymExpr(i - 1) * V_im1, 500));
            rationalPart = rationalPart + simplifyCore(expand(-B / denom, 500));

            SymExpr dB = diff(B, var);
            SymExpr term2 = simplifyCore(expand((dB * U) / SymExpr(i - 1), 500));
            currentA = simplifyCore(expand(C + term2, 500));
            
            currentD = simplifyCore(expand(U * V_im1, 500));
            
            for (auto& p : sqFree) {
                if (p.second == i) p.second = i - 1;
            }
        }
        
        integralPart = integralPart + simplifyCore(expand(currentA / currentD, 500));
        return {rationalPart, integralPart};
    }

    // =================================================================
    // 🚀 Risch 算法入口 (Risch Algorithm Entry Point)
    // =================================================================
    SymExpr rischIntegrate(const SymExpr& expr, const std::string& var) {
        // Step 1 - 构建微分域与刘维尔扩张
        RischDiffField field = buildRischDifferentialField(expr, var);
        SymExpr rewritten = field.rewrite(expr);
        
        // Step 2 - Hermite 约化 (目前仅针对有理函数域 Q(x))
        if (field.tower.empty()) {
            auto [num, den] = getFraction(rewritten);
            if (getDegree(den, var) > 0) {
                auto [ratPart, intPart] = hermiteReduce(num, den, var);
                
                if (intPart.isZero()) return ratPart;
                
                try {
                    if (intPart.toString() == expr.toString()) throw std::runtime_error("Loop");
                    SymExpr intRes = integrate(intPart, var);
                    return ratPart + intRes;
                } catch (...) {
                    std::string debugInfo = "Risch Algorithm Step 2 (Hermite Reduction) completed.\n";
                    debugInfo += "Original: " + expr.toString() + "\n";
                    debugInfo += "Rational Part: " + ratPart.toString() + "\n";
                    debugInfo += "Remaining Integral: \\int (" + intPart.toString() + ") d" + var + "\n";
                    throw std::runtime_error("Calculus Error: " + debugInfo);
                }
            }
        }
        
        // Step 4 & 5 - 扩张多项式积分 (Extension Polynomial Integration)
        if (!field.tower.empty()) {
            auto topExt = field.tower.back();
            if (topExt.type == RischExtType::LOG) {
                auto coeffs = extractCoeffs(rewritten, topExt.name);
                if (!coeffs.empty()) {
                    int n = static_cast<int>(coeffs.size()) - 1;
                    std::vector<SymExpr> B(n + 2, SymExpr(BigInt(0)));
                    SymExpr t_deriv = topExt.deriv;
                    SymExpr log_u(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{topExt.arg.ptr}));
                    SymExpr t_sym = SymExpr::makeVar(topExt.name);

                    try {
                        SymExpr intAn = integrate(coeffs[n], var);
                        SymExpr intAn_t = applyRule(intAn, log_u, t_sym);
                        auto coeffs_intAn = extractCoeffs(intAn_t, topExt.name);
                        
                        SymExpr c(BigInt(0));
                        SymExpr B_n = intAn_t;
                        if (coeffs_intAn.size() > 1) {
                            c = simplifyCore(coeffs_intAn[1] / SymExpr(BigInt(n + 1)));
                            B_n = coeffs_intAn[0];
                            if (coeffs_intAn.size() > 2) {
                                throw std::runtime_error("Non-elementary integral.");
                            }
                        } else if (coeffs_intAn.size() == 1) {
                            B_n = coeffs_intAn[0];
                        }
                        
                        B[n + 1] = c;
                        B[n] = B_n;
                        
                        for (int i = n - 1; i >= 0; --i) {
                            SymExpr integrand = simplifyCore(coeffs[i] - SymExpr(BigInt(i + 1)) * B[i + 1] * t_deriv);
                            SymExpr int_i = integrate(integrand, var);
                            SymExpr int_i_t = applyRule(int_i, log_u, t_sym);
                            if (containsVar(int_i_t.ptr, topExt.name)) {
                                throw std::runtime_error("Non-elementary integral.");
                            }
                            B[i] = int_i_t;
                        }
                        
                        SymExpr result(BigInt(0));
                        for (int i = 0; i <= n + 1; ++i) {
                            if (!B[i].isZero()) {
                                result = result + B[i] * (log_u ^ SymExpr(BigInt(i)));
                            }
                        }
                        return simplifyCore(result);
                    } catch (...) {
                        // 失败则继续往下走，抛出调试信息
                    }
                }
            } else if (topExt.type == RischExtType::EXP) {
                auto coeffs = extractCoeffs(rewritten, topExt.name);
                if (!coeffs.empty()) {
                    int n = static_cast<int>(coeffs.size()) - 1;
                    SymExpr u_deriv = simplifyCore(topExt.deriv / SymExpr::makeVar(topExt.name));
                    SymExpr exp_u(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{topExt.arg.ptr}));
                    
                    bool success = true;
                    SymExpr result(BigInt(0));
                    
                    for (int i = 0; i <= n; ++i) {
                        if (coeffs[i].isZero()) continue;
                        if (i == 0) {
                            try {
                                result = result + integrate(coeffs[0], var);
                            } catch (...) {
                                success = false;
                                break;
                            }
                        } else {
                            // 启发式求解 Risch 微分方程 B_i' + i u' B_i = A_i
                            // 猜测 B_i = A_i / (i u')
                            SymExpr B_guess = simplifyCore(coeffs[i] / (SymExpr(BigInt(i)) * u_deriv));
                            SymExpr B_guess_deriv = simplifyCore(diff(B_guess, var));
                            if (B_guess_deriv.isZero()) {
                                result = result + B_guess * (exp_u ^ SymExpr(BigInt(i)));
                            } else {
                                // 猜测失败，尝试分部积分形式的启发式
                                success = false;
                                break;
                            }
                        }
                    }
                    
                    if (success) return simplifyCore(result);
                }
            }
        }

        // 调试输出：展示构建的微分域
        std::string debugInfo = "Risch Field over Q(" + var + "):\n";
        for (const auto& ext : field.tower) {
            debugInfo += "  " + ext.name + " = ";
            if (ext.type == RischExtType::LOG) debugInfo += "log(" + ext.arg.toString() + ")";
            else debugInfo += "exp(" + ext.arg.toString() + ")";
            debugInfo += ",  " + ext.name + "' = " + ext.deriv.toString() + "\n";
        }
        debugInfo += "Rewritten integrand: " + rewritten.toString();
        
        throw std::runtime_error("Calculus Error: Risch Algorithm Step 1 completed.\n" + debugInfo);
    }

    // =================================================================
    // 🚀 符号积分 (Symbolic Integration) - 基础多项式与初等函数
    // =================================================================
    SymExpr integrate(const SymExpr& expr, const std::string& var) {
        if (!expr.ptr) return expr;

        SymExpr x = SymExpr::makeVar(var);

        // 从规则库获取当前变量的积分规则
        std::vector<std::pair<SymExpr, SymExpr>> rules = getIntegRules(var);

        std::function<SymExpr(const SymExpr&, int)> doInteg = [&](const SymExpr& e, int depth) -> SymExpr {
            if (depth > 3) throw std::runtime_error("Calculus Error: Integration depth limit exceeded.");

            if (!containsVar(e.ptr, var)) {
                return e * x; // ∫ c dx = c * x
            }

            if (e.ptr->getType() == SymType::ADD) {
                auto add = std::static_pointer_cast<SymAdd>(e.ptr);
                SymExpr res(BigInt(0));
                for (auto& arg : add->args) res = res + doInteg(SymExpr(arg), depth);
                return res;
            }

            // --- 提取系数和变量部分 ---
            SymExpr coeff(BigInt(1));
            SymExpr varPart(BigInt(1));

            if (e.ptr->getType() == SymType::MUL) {
                auto mul = std::static_pointer_cast<SymMul>(e.ptr);
                for (auto& arg : mul->args) {
                    if (!containsVar(arg, var)) coeff = coeff * SymExpr(arg);
                    else varPart = varPart * SymExpr(arg);
                }
            } else {
                varPart = e;
            }

            // --- 0. 查表匹配 (Pattern Matching) ---
            SymExpr integratedPart = varPart;
            bool matched = false;
            for (const auto& rule : rules) {
                std::map<std::string, SymExpr> captures;
                if (matchAST(varPart.ptr, rule.first.ptr, captures)) {
                    integratedPart = substituteCaptures(rule.second, captures);
                    matched = true;
                    break;
                }
            }

            if (matched) {
                return coeff * integratedPart;
            }

            // --- 1. 线性换元法 (Linear Substitution) ---
            auto findLinearArg = [&](const std::shared_ptr<SymNode>& node, SymExpr& out_u, SymExpr& out_a) -> bool {
                if (!node) return false;
                if (node->getType() == SymType::FUNC) {
                    auto func = std::static_pointer_cast<SymFunc>(node);
                    for (auto& arg : func->args) {
                        SymExpr u(arg);
                        SymExpr du = simplifyCore(diff(u, var));
                        if (!du.isZero() && !containsVar(du.ptr, var)) {
                            if (u.ptr->getType() != SymType::VAR) {
                                out_u = u;
                                out_a = du;
                                return true;
                            }
                        }
                    }
                } else if (node->getType() == SymType::POW) {
                    auto powNode = std::static_pointer_cast<SymPow>(node);
                    SymExpr u(powNode->base);
                    SymExpr du = simplifyCore(diff(u, var));
                    if (!du.isZero() && !containsVar(du.ptr, var)) {
                        if (u.ptr->getType() != SymType::VAR) {
                            out_u = u;
                            out_a = du;
                            return true;
                        }
                    }
                }
                return false;
            };

            SymExpr sub_u, sub_a;
            if (findLinearArg(varPart.ptr, sub_u, sub_a)) {
                std::string u_var = "_u";
                SymExpr u_sym = SymExpr::makeVar(u_var);
                std::function<SymExpr(const SymExpr&)> replaceNode = [&](const SymExpr& expr_node) -> SymExpr {
                    if (!expr_node.ptr) return expr_node;
                    if (expr_node.toString() == sub_u.toString()) return u_sym;
                    switch (expr_node.ptr->getType()) {
                        case SymType::ADD: {
                            SymExpr res(BigInt(0));
                            for (auto& arg : std::static_pointer_cast<SymAdd>(expr_node.ptr)->args) res = res + replaceNode(SymExpr(arg));
                            return res;
                        }
                        case SymType::MUL: {
                            SymExpr res(BigInt(1));
                            for (auto& arg : std::static_pointer_cast<SymMul>(expr_node.ptr)->args) res = res * replaceNode(SymExpr(arg));
                            return res;
                        }
                        case SymType::POW: {
                            auto p = std::static_pointer_cast<SymPow>(expr_node.ptr);
                            return replaceNode(SymExpr(p->base)) ^ replaceNode(SymExpr(p->exp));
                        }
                        case SymType::FUNC: {
                            auto f = std::static_pointer_cast<SymFunc>(expr_node.ptr);
                            std::vector<std::shared_ptr<SymNode>> nArgs;
                            for (auto& arg : f->args) nArgs.push_back(replaceNode(SymExpr(arg)).ptr);
                            return SymExpr(std::make_shared<SymFunc>(f->name, std::move(nArgs)));
                        }
                        default: return expr_node;
                    }
                };
                
                SymExpr f_u = replaceNode(varPart);
                if (!containsVar(f_u.ptr, var)) {
                    try {
                        SymExpr f_var = subs(f_u, u_var, SymExpr::makeVar(var));
                        SymExpr int_var = doInteg(simplify(expand(f_var / sub_a, 500)), depth + 1);
                        SymExpr res = subs(int_var, var, sub_u);
                        return coeff * res;
                    } catch (...) {}
                }
            }

            // --- 1.5 广义换元法 (凑微分法) ---
            if (varPart.ptr->getType() == SymType::MUL) {
                auto mul = std::static_pointer_cast<SymMul>(varPart.ptr);
                std::vector<SymExpr> candidate_us;
                for (auto& arg : mul->args) {
                    candidate_us.push_back(SymExpr(arg));
                    if (arg->getType() == SymType::POW) {
                        candidate_us.push_back(SymExpr(std::static_pointer_cast<SymPow>(arg)->base));
                    } else if (arg->getType() == SymType::FUNC) {
                        auto func = std::static_pointer_cast<SymFunc>(arg);
                        if (func->args.size() == 1) {
                            candidate_us.push_back(SymExpr(func->args[0]));
                        }
                    }
                }
                
                for (const auto& u : candidate_us) {
                    if (u.ptr->getType() == SymType::VAR || u.ptr->getType() == SymType::NUM) continue;
                    SymExpr du = simplifyCore(diff(u, var));
                    if (du.isZero()) continue;
                    
                    SymExpr rem = simplify(varPart / du);
                    
                    std::string u_var = "_u";
                    SymExpr u_sym = SymExpr::makeVar(u_var);
                    std::function<SymExpr(const SymExpr&)> replaceU = [&](const SymExpr& expr_node) -> SymExpr {
                        if (!expr_node.ptr) return expr_node;
                        if (expr_node.toString() == u.toString()) return u_sym;
                        switch (expr_node.ptr->getType()) {
                            case SymType::ADD: {
                                SymExpr res(BigInt(0));
                                for (auto& arg : std::static_pointer_cast<SymAdd>(expr_node.ptr)->args) res = res + replaceU(SymExpr(arg));
                                return res;
                            }
                            case SymType::MUL: {
                                SymExpr res(BigInt(1));
                                for (auto& arg : std::static_pointer_cast<SymMul>(expr_node.ptr)->args) res = res * replaceU(SymExpr(arg));
                                return res;
                            }
                            case SymType::POW: {
                                auto p = std::static_pointer_cast<SymPow>(expr_node.ptr);
                                return replaceU(SymExpr(p->base)) ^ replaceU(SymExpr(p->exp));
                            }
                            case SymType::FUNC: {
                                auto f = std::static_pointer_cast<SymFunc>(expr_node.ptr);
                                std::vector<std::shared_ptr<SymNode>> nArgs;
                                for (auto& arg : f->args) nArgs.push_back(replaceU(SymExpr(arg)).ptr);
                                return SymExpr(std::make_shared<SymFunc>(f->name, std::move(nArgs)));
                            }
                            default: return expr_node;
                        }
                    };
                    
                    SymExpr rem_u = replaceU(rem);
                    
                    if (containsVar(rem_u.ptr, var)) {
                        auto coeffs = extractCoeffs(u, var);
                        if (coeffs.size() > 1) {
                            bool isBinomial = true;
                            int n = static_cast<int>(coeffs.size()) - 1;
                            for (int i = 1; i < n; ++i) {
                                if (!coeffs[i].isZero()) { isBinomial = false; break; }
                            }
                            if (isBinomial && !coeffs[n].isZero()) {
                                SymExpr A = coeffs[n];
                                SymExpr B = coeffs[0];
                                SymExpr x_n = (u_sym - B) / A;
                                
                                std::function<SymExpr(const SymExpr&)> replaceXn = [&](const SymExpr& node) -> SymExpr {
                                    if (!node.ptr) return node;
                                    if (node.ptr->getType() == SymType::POW) {
                                        auto p = std::static_pointer_cast<SymPow>(node.ptr);
                                        if (p->base->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(p->base)->name == var) {
                                            if (p->exp->getType() == SymType::NUM) {
                                                auto [isInt, exp_val] = extractExactInt(std::static_pointer_cast<SymNum>(p->exp)->value);
                                                if (isInt && exp_val % n == 0) {
                                                    return x_n ^ SymExpr(BigInt(exp_val / n));
                                                }
                                            }
                                        }
                                    }
                                    if (n == 1 && node.ptr->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(node.ptr)->name == var) {
                                        return x_n;
                                    }
                                    switch (node.ptr->getType()) {
                                        case SymType::ADD: {
                                            SymExpr res(BigInt(0));
                                            for (auto& arg : std::static_pointer_cast<SymAdd>(node.ptr)->args) res = res + replaceXn(SymExpr(arg));
                                            return res;
                                        }
                                        case SymType::MUL: {
                                            SymExpr res(BigInt(1));
                                            for (auto& arg : std::static_pointer_cast<SymMul>(node.ptr)->args) res = res * replaceXn(SymExpr(arg));
                                            return res;
                                        }
                                        case SymType::POW: {
                                            auto p = std::static_pointer_cast<SymPow>(node.ptr);
                                            return replaceXn(SymExpr(p->base)) ^ replaceXn(SymExpr(p->exp));
                                        }
                                        case SymType::FUNC: {
                                            auto f = std::static_pointer_cast<SymFunc>(node.ptr);
                                            std::vector<std::shared_ptr<SymNode>> nArgs;
                                            for (auto& arg : f->args) nArgs.push_back(replaceXn(SymExpr(arg)).ptr);
                                            return SymExpr(std::make_shared<SymFunc>(f->name, std::move(nArgs)));
                                        }
                                        default: return node;
                                    }
                                };
                                rem_u = simplify(expand(replaceXn(rem_u), 500));
                            }
                        }
                    }

                    if (!containsVar(rem_u.ptr, var)) {
                        try {
                            SymExpr f_var = subs(rem_u, u_var, SymExpr::makeVar(var));
                            SymExpr int_var = doInteg(simplify(expand(f_var, 500)), depth + 1);
                            SymExpr res = subs(int_var, var, u);
                            return coeff * res;
                        } catch (...) {}
                    }
                }
            }

            // --- 1.8 三角函数高次幂降幂与拆分 ---
            if (varPart.ptr->getType() == SymType::POW) {
                auto powNode = std::static_pointer_cast<SymPow>(varPart.ptr);
                if (powNode->base->getType() == SymType::FUNC && powNode->exp->getType() == SymType::NUM) {
                    auto func = std::static_pointer_cast<SymFunc>(powNode->base);
                    auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                    if (isInt && n >= 2 && func->args.size() == 1) {
                        SymExpr arg(func->args[0]);
                        if (func->name == "sin") {
                            if (n % 2 == 0) {
                                SymExpr _C = SymExpr::makeVar("_C");
                                SymExpr halfAngle = (SymExpr(BigInt(1)) - _C) / SymExpr(BigInt(2));
                                SymExpr expanded = expand(halfAngle ^ SymExpr(BigInt(n / 2)), 500);
                                SymExpr cos2x = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{(SymExpr(BigInt(2)) * arg).ptr}));
                                expanded = subs(expanded, "_C", cos2x);
                                try { return coeff * doInteg(expanded, depth + 1); } catch (...) {}
                            } else {
                                SymExpr _C = SymExpr::makeVar("_C");
                                SymExpr cosSq = SymExpr(BigInt(1)) - (_C ^ SymExpr(BigInt(2)));
                                SymExpr rem = SymExpr(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{arg.ptr})) * (cosSq ^ SymExpr(BigInt((n - 1) / 2)));
                                SymExpr expanded = expand(rem, 500);
                                SymExpr cosx = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                                expanded = subs(expanded, "_C", cosx);
                                try { return coeff * doInteg(expanded, depth + 1); } catch (...) {}
                            }
                        } else if (func->name == "cos") {
                            if (n % 2 == 0) {
                                SymExpr _C = SymExpr::makeVar("_C");
                                SymExpr halfAngle = (SymExpr(BigInt(1)) + _C) / SymExpr(BigInt(2));
                                SymExpr expanded = expand(halfAngle ^ SymExpr(BigInt(n / 2)), 500);
                                SymExpr cos2x = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{(SymExpr(BigInt(2)) * arg).ptr}));
                                expanded = subs(expanded, "_C", cos2x);
                                try { return coeff * doInteg(expanded, depth + 1); } catch (...) {}
                            } else {
                                SymExpr _S = SymExpr::makeVar("_S");
                                SymExpr sinSq = SymExpr(BigInt(1)) - (_S ^ SymExpr(BigInt(2)));
                                SymExpr rem = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{arg.ptr})) * (sinSq ^ SymExpr(BigInt((n - 1) / 2)));
                                SymExpr expanded = expand(rem, 500);
                                SymExpr sinx = SymExpr(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                                expanded = subs(expanded, "_S", sinx);
                                try { return coeff * doInteg(expanded, depth + 1); } catch (...) {}
                            }
                        }
                    }
                }
            }

            // --- 1.9 二次根式三角换元 (Trigonometric Substitution) ---
            if (var != "_t" && var != "_weierstrass_t") {
                SymExpr quadBase;
                bool foundQuad = false;
                
                std::function<void(const std::shared_ptr<SymNode>&)> findQuad = [&](const std::shared_ptr<SymNode>& node) {
                    if (!node || foundQuad) return;
                    if (node->getType() == SymType::POW) {
                        auto powNode = std::static_pointer_cast<SymPow>(node);
                        if (powNode->exp->getType() == SymType::NUM) {
                            auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                            if (!isInt) {
                                auto coeffs = extractCoeffs(SymExpr(powNode->base), var);
                                if (coeffs.size() == 3 && coeffs[1].isZero() && !coeffs[2].isZero() && !coeffs[0].isZero()) {
                                    quadBase = SymExpr(powNode->base);
                                    foundQuad = true;
                                    return;
                                }
                            }
                        }
                    }
                    if (node->getType() == SymType::ADD) {
                        for (auto& arg : std::static_pointer_cast<SymAdd>(node)->args) findQuad(arg);
                    } else if (node->getType() == SymType::MUL) {
                        for (auto& arg : std::static_pointer_cast<SymMul>(node)->args) findQuad(arg);
                    } else if (node->getType() == SymType::FUNC) {
                        for (auto& arg : std::static_pointer_cast<SymFunc>(node)->args) findQuad(arg);
                    }
                };
                
                findQuad(varPart.ptr);
                
                if (foundQuad) {
                    auto coeffs = extractCoeffs(quadBase, var);
                    SymExpr A = coeffs[2];
                    SymExpr C = coeffs[0];
                    
                    auto isPos = [](const SymExpr& e) {
                        if (e.ptr->getType() == SymType::NUM) return !isCasNegative(std::static_pointer_cast<SymNum>(e.ptr)->value) && !e.isZero();
                        return false;
                    };
                    auto isNeg = [](const SymExpr& e) {
                        if (e.ptr->getType() == SymType::NUM) return isCasNegative(std::static_pointer_cast<SymNum>(e.ptr)->value);
                        return false;
                    };
                    
                    bool A_pos = isPos(A), A_neg = isNeg(A);
                    bool C_pos = isPos(C), C_neg = isNeg(C);
                    
                    SymExpr t = SymExpr::makeVar("_t");
                    SymExpr x_sub, dx_sub, t_back;
                    bool valid = false;
                    
                    if (A_pos && C_pos) {
                        SymExpr sqrtCA = simplifyCore((C / A) ^ SymExpr(Fraction(1, 2)));
                        SymExpr tan_t(std::make_shared<SymFunc>("tan", std::vector<std::shared_ptr<SymNode>>{t.ptr}));
                        SymExpr cos_t(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{t.ptr}));
                        x_sub = sqrtCA * tan_t;
                        dx_sub = sqrtCA / (cos_t * cos_t);
                        SymExpr atan_arg = simplifyCore(SymExpr::makeVar(var) / sqrtCA);
                        t_back = SymExpr(std::make_shared<SymFunc>("atan", std::vector<std::shared_ptr<SymNode>>{atan_arg.ptr}));
                        valid = true;
                    } else if (A_neg && C_pos) {
                        SymExpr sqrtCA = simplifyCore((-C / A) ^ SymExpr(Fraction(1, 2)));
                        SymExpr sin_t(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{t.ptr}));
                        SymExpr cos_t(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{t.ptr}));
                        x_sub = sqrtCA * sin_t;
                        dx_sub = sqrtCA * cos_t;
                        SymExpr asin_arg = simplifyCore(SymExpr::makeVar(var) / sqrtCA);
                        t_back = SymExpr(std::make_shared<SymFunc>("asin", std::vector<std::shared_ptr<SymNode>>{asin_arg.ptr}));
                        valid = true;
                    } else if (A_pos && C_neg) {
                        SymExpr sqrtCA = simplifyCore((-C / A) ^ SymExpr(Fraction(1, 2)));
                        SymExpr cos_t(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{t.ptr}));
                        SymExpr sin_t(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{t.ptr}));
                        x_sub = sqrtCA / cos_t;
                        dx_sub = sqrtCA * sin_t / (cos_t * cos_t);
                        SymExpr acos_arg = simplifyCore(sqrtCA / SymExpr::makeVar(var));
                        t_back = SymExpr(std::make_shared<SymFunc>("acos", std::vector<std::shared_ptr<SymNode>>{acos_arg.ptr}));
                        valid = true;
                    }
                    
                    if (valid) {
                        try {
                            SymExpr subbed = simplifyCore(subs(varPart, var, x_sub) * dx_sub);
                            SymExpr subbed_var = subs(subbed, "_t", SymExpr::makeVar(var));
                            SymExpr int_var = doInteg(trigsimp(subbed_var), depth + 1);
                            SymExpr res = subs(int_var, var, t_back);
                            return coeff * res;
                        } catch (...) {}
                    }
                }
            }

            // --- 2. 有理分式积分引擎 (Rational Function Integration) ---
            SymExpr num(BigInt(1)), den(BigInt(1));
            bool isFraction = false;
            if (varPart.ptr->getType() == SymType::MUL) {
                auto mul = std::static_pointer_cast<SymMul>(varPart.ptr);
                for (auto& arg : mul->args) {
                    if (arg->getType() == SymType::POW) {
                        auto powNode = std::static_pointer_cast<SymPow>(arg);
                        if (powNode->exp->getType() == SymType::NUM) {
                            auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                            if (isInt && n < 0) {
                                den = den * (SymExpr(powNode->base) ^ SymExpr(BigInt(-n)));
                                isFraction = true;
                                continue;
                            }
                        }
                    }
                    num = num * SymExpr(arg);
                }
            } else if (varPart.ptr->getType() == SymType::POW) {
                auto powNode = std::static_pointer_cast<SymPow>(varPart.ptr);
                if (powNode->exp->getType() == SymType::NUM) {
                    auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                    if (isInt && n < 0) {
                        den = SymExpr(powNode->base) ^ SymExpr(BigInt(-n));
                        num = SymExpr(BigInt(1));
                        isFraction = true;
                    } else {
                        num = varPart;
                    }
                } else {
                    num = varPart;
                }
            } else {
                num = varPart;
            }

            if (isFraction && containsVar(den.ptr, var)) {
                SymExpr num_expanded = simplifyCore(expand(num, 500));
                SymExpr den_expanded = simplifyCore(expand(den, 500));
                
                int degN = getDegree(num_expanded, var);
                int degD = getDegree(den_expanded, var);
                
                // ★ 只有当分子和分母都是多项式时，才使用有理分式积分引擎
                if (degD >= 0 && (degN >= 0 || !containsVar(num_expanded.ptr, var))) {
                    try {
                        SymExpr polyE(BigInt(0));
                        SymExpr R = num_expanded;
                        
                        if (degN >= degD && degD > 0) {
                            auto [q, r] = polyDiv(num_expanded, den_expanded, var);
                            polyE = q;
                            R = r;
                        }
                        
                        SymExpr res(BigInt(0));
                        if (!polyE.isZero()) {
                            res = doInteg(polyE, depth);
                        }
                        
                        if (!R.isZero()) {
                            SymExpr factD = factorReal(den);
                            std::vector<std::pair<SymExpr, int>> denFactors;
                            auto process = [&](const SymExpr& f) {
                                if (!containsVar(f.ptr, var)) return;
                                if (f.ptr->getType() == SymType::POW) {
                                    auto powNode = std::static_pointer_cast<SymPow>(f.ptr);
                                    if (powNode->exp->getType() == SymType::NUM) {
                                        auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                                        if (isInt && n > 0) {
                                            denFactors.push_back({SymExpr(powNode->base), static_cast<int>(n)});
                                            return;
                                        }
                                    }
                                }
                                denFactors.push_back({f, 1});
                            };
                            if (factD.ptr->getType() == SymType::MUL) {
                                for (auto& arg : std::static_pointer_cast<SymMul>(factD.ptr)->args) process(SymExpr(arg));
                            } else {
                                process(factD);
                            }
                            
                            if (denFactors.empty()) {
                                throw std::runtime_error("Calculus Error: Failed to factorize denominator for partial fraction decomposition.");
                            } else {
                                SymExpr remaining_N = R;
                                SymExpr remaining_D(BigInt(1));
                                for (auto& f : denFactors) remaining_D = simplifyCore(expand(remaining_D * (f.first ^ SymExpr(BigInt(f.second))), 500));
                                
                                SymExpr D_expanded = den_expanded;
                                auto coeffs_D = extractCoeffs(D_expanded, var);
                                auto coeffs_rem = extractCoeffs(remaining_D, var);
                                if (coeffs_D.empty() || coeffs_rem.empty()) {
                                    throw std::runtime_error("Calculus Error: Denominator is not a polynomial.");
                                }
                                auto lead_D = coeffs_D.back();
                                auto lead_rem = coeffs_rem.back();
                                SymExpr const_factor = simplifyCore(lead_D / lead_rem);
                                remaining_N = simplifyCore(expand(remaining_N / const_factor, 500));
                                
                                std::vector<std::pair<SymExpr, SymExpr>> partialFractions;
                                for (size_t i = 0; i < denFactors.size(); ++i) {
                                    if (i == denFactors.size() - 1) {
                                        partialFractions.push_back({remaining_N, remaining_D});
                                        break;
                                    }
                                    SymExpr D1 = simplifyCore(expand(denFactors[i].first ^ SymExpr(BigInt(denFactors[i].second)), 500));
                                    SymExpr D2(BigInt(1));
                                    for (size_t j = i + 1; j < denFactors.size(); ++j) {
                                        D2 = simplifyCore(expand(D2 * (denFactors[j].first ^ SymExpr(BigInt(denFactors[j].second))), 500));
                                    }
                                    
                                    try {
                                        auto [gcd, S, T] = polyEGCD(D1, D2, var);
                                        SymExpr N_T = simplifyCore(expand((remaining_N * T) / gcd, 500));
                                        SymExpr N_S = simplifyCore(expand((remaining_N * S) / gcd, 500));
                                        
                                        auto [q1, r1] = polyDiv(N_T, D1, var);
                                        partialFractions.push_back({r1, D1});
                                        
                                        remaining_N = simplifyCore(expand(N_S + q1 * D2, 500));
                                        remaining_D = D2;
                                    } catch (const std::runtime_error& e) {
                                        throw std::runtime_error(std::string("Calculus Error: Partial fraction decomposition failed. ") + e.what());
                                    }
                                }
                                
                                SymExpr polyPart(BigInt(0));
                                for (size_t i = 0; i < partialFractions.size(); ++i) {
                                    SymExpr curr_N = partialFractions[i].first;
                                    SymExpr base_D = denFactors[i].first;
                                    int k = denFactors[i].second;
                                    
                                    for (int j = k; j >= 1; --j) {
                                        try {
                                            auto [q, r] = polyDiv(curr_N, base_D, var);
                                            res = res + integratePartialFraction(r, base_D, j, var);
                                            curr_N = q;
                                        } catch (const std::runtime_error& e) {
                                            throw std::runtime_error(std::string("Calculus Error: Integration of partial fraction failed. ") + e.what());
                                        }
                                    }
                                    polyPart = polyPart + curr_N;
                                }
                                if (!polyPart.isZero()) {
                                    res = res + doInteg(polyPart, depth);
                                }
                            }
                        }
                        return coeff * res;
                    } catch (...) {
                        // 有理分式积分失败，静默吞没，继续尝试后续方法（如 Risch 算法）
                    }
                }
            }

            // --- 3. 启发式分部积分 (Integration by Parts) ---
            bool tryParts = false;
            std::vector<SymExpr> factors;
            if (varPart.ptr->getType() == SymType::MUL) {
                auto mul = std::static_pointer_cast<SymMul>(varPart.ptr);
                for (auto& arg : mul->args) factors.push_back(SymExpr(arg));
                tryParts = true;
            } else if (varPart.ptr->getType() == SymType::FUNC || varPart.ptr->getType() == SymType::POW) {
                factors.push_back(varPart);
                factors.push_back(SymExpr(BigInt(1)));
                tryParts = true;
            }

            if (tryParts) {
                auto getPriority = [&](const SymExpr& f) -> int {
                    if (f.ptr->getType() == SymType::FUNC) {
                        auto fn = std::static_pointer_cast<SymFunc>(f.ptr);
                        if (fn->name == "log" || fn->name == "ln" || fn->name == "asin" || fn->name == "acos" || fn->name == "atan") return 1;
                    }
                    if (f.ptr->getType() == SymType::POW) {
                        auto p = std::static_pointer_cast<SymPow>(f.ptr);
                        if (p->base->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(p->base)->name == var) {
                            if (p->exp->getType() == SymType::NUM) {
                                auto [isInt, n] = extractExactInt(std::static_pointer_cast<SymNum>(p->exp)->value);
                                if (isInt && n > 0) return 2;
                            }
                        }
                    }
                    if (f.ptr->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(f.ptr)->name == var) return 2;
                    return 3;
                };

                std::vector<size_t> indices(factors.size());
                std::iota(indices.begin(), indices.end(), 0);
                std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
                    return getPriority(factors[a]) < getPriority(factors[b]);
                });

                for (size_t i : indices) {
                    SymExpr u = factors[i];
                    SymExpr dv(BigInt(1));
                    for (size_t j = 0; j < factors.size(); ++j) {
                        if (j != i) dv = dv * factors[j];
                    }

                    SymExpr du = simplifyCore(diff(u, var));
                    try {
                        SymExpr v = doInteg(dv, depth + 1);
                        SymExpr v_du = simplifyCore(v * du);
                        
                        // 循环分部积分检测 (1-step cycle)
                        SymExpr ratio1 = simplifyCore(v_du / varPart);
                        if (!containsVar(ratio1.ptr, var) && !ratio1.isZero()) {
                            SymExpr k = ratio1;
                            SymExpr one_plus_k = simplifyCore(SymExpr(BigInt(1)) + k);
                            if (!one_plus_k.isZero()) {
                                // 修复：分部积分循环时，必须确保 u*v 不会被错误化简
                                return coeff * simplify(expand((u * v) / one_plus_k, 500));
                            }
                        }
                        
                        // 循环分部积分检测 (2-step cycle)
                        if (v_du.ptr->getType() == SymType::MUL) {
                            auto mul2 = std::static_pointer_cast<SymMul>(v_du.ptr);
                            std::vector<SymExpr> factors2;
                            for (auto& arg : mul2->args) factors2.push_back(SymExpr(arg));
                            
                            std::vector<size_t> indices2(factors2.size());
                            std::iota(indices2.begin(), indices2.end(), 0);
                            std::sort(indices2.begin(), indices2.end(), [&](size_t a, size_t b) {
                                return getPriority(factors2[a]) < getPriority(factors2[b]);
                            });
                            
                            bool found_2step = false;
                            SymExpr result_2step;
                            for (size_t i2 : indices2) {
                                SymExpr u2 = factors2[i2];
                                SymExpr dv2(BigInt(1));
                                for (size_t j2 = 0; j2 < factors2.size(); ++j2) {
                                    if (j2 != i2) dv2 = dv2 * factors2[j2];
                                }
                                SymExpr du2 = simplifyCore(diff(u2, var));
                                try {
                                    SymExpr v2 = doInteg(dv2, depth + 2);
                                    SymExpr v2_du2 = simplifyCore(v2 * du2);
                                    
                                    SymExpr ratio2 = simplifyCore(v2_du2 / varPart);
                                    if (!containsVar(ratio2.ptr, var) && !ratio2.isZero()) {
                                        SymExpr k = ratio2;
                                        SymExpr one_minus_k = simplifyCore(SymExpr(BigInt(1)) - k);
                                        if (!one_minus_k.isZero()) {
                                            result_2step = coeff * simplifyCore((u * v - u2 * v2) / one_minus_k);
                                            found_2step = true;
                                            break;
                                        }
                                    }
                                } catch (...) {}
                            }
                            if (found_2step) return result_2step;
                        }

                        SymExpr int_v_du = doInteg(v_du, depth + 1);
                        return coeff * simplifyCore(u * v - int_v_du);
                    } catch (...) {}
                }
            }

            // --- 4. 万能公式换元 (Weierstrass Substitution) ---
            bool hasTrig = false;
            std::function<bool(const std::shared_ptr<SymNode>&)> isRationalTrig = [&](const std::shared_ptr<SymNode>& node) -> bool {
                if (!node) return true;
                switch (node->getType()) {
                    case SymType::NUM: return true;
                    case SymType::VAR: return std::static_pointer_cast<SymVar>(node)->name != var;
                    case SymType::ADD:
                        for (auto& arg : std::static_pointer_cast<SymAdd>(node)->args)
                            if (!isRationalTrig(arg)) return false;
                        return true;
                    case SymType::MUL:
                        for (auto& arg : std::static_pointer_cast<SymMul>(node)->args)
                            if (!isRationalTrig(arg)) return false;
                        return true;
                    case SymType::POW: {
                        auto powNode = std::static_pointer_cast<SymPow>(node);
                        if (!isRationalTrig(powNode->base)) return false;
                        if (containsVar(powNode->exp, var)) return false;
                        return true;
                    }
                    case SymType::FUNC: {
                        auto func = std::static_pointer_cast<SymFunc>(node);
                        if (func->args.size() == 1 && func->args[0]->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(func->args[0])->name == var) {
                            if (func->name == "sin" || func->name == "cos" || func->name == "tan") {
                                hasTrig = true;
                                return true;
                            }
                            return false;
                        }
                        for (auto& arg : func->args)
                            if (containsVar(arg, var)) return false;
                        return true;
                    }
                }
                return false;
            };

            if (isRationalTrig(varPart.ptr) && hasTrig) {
                std::string t_var = "_weierstrass_t";
                SymExpr t = SymExpr::makeVar(t_var);
                SymExpr sin_sub = (SymExpr(BigInt(2)) * t) / (SymExpr(BigInt(1)) + t * t);
                SymExpr cos_sub = (SymExpr(BigInt(1)) - t * t) / (SymExpr(BigInt(1)) + t * t);
                SymExpr tan_sub = (SymExpr(BigInt(2)) * t) / (SymExpr(BigInt(1)) - t * t);
                SymExpr dt_sub = SymExpr(BigInt(2)) / (SymExpr(BigInt(1)) + t * t);

                std::function<SymExpr(const std::shared_ptr<SymNode>&)> applyWeierstrass = [&](const std::shared_ptr<SymNode>& node) -> SymExpr {
                    if (!node) return SymExpr(BigInt(0));
                    switch (node->getType()) {
                        case SymType::NUM: return SymExpr(node);
                        case SymType::VAR: return SymExpr(node);
                        case SymType::ADD: {
                            SymExpr res(BigInt(0));
                            for (auto& arg : std::static_pointer_cast<SymAdd>(node)->args) res = res + applyWeierstrass(arg);
                            return res;
                        }
                        case SymType::MUL: {
                            SymExpr res(BigInt(1));
                            for (auto& arg : std::static_pointer_cast<SymMul>(node)->args) res = res * applyWeierstrass(arg);
                            return res;
                        }
                        case SymType::POW: {
                            auto powNode = std::static_pointer_cast<SymPow>(node);
                            return applyWeierstrass(powNode->base) ^ applyWeierstrass(powNode->exp);
                        }
                        case SymType::FUNC: {
                            auto func = std::static_pointer_cast<SymFunc>(node);
                            if (func->args.size() == 1 && func->args[0]->getType() == SymType::VAR && std::static_pointer_cast<SymVar>(func->args[0])->name == var) {
                                if (func->name == "sin") return sin_sub;
                                if (func->name == "cos") return cos_sub;
                                if (func->name == "tan") return tan_sub;
                            }
                            std::vector<std::shared_ptr<SymNode>> newArgs;
                            for (auto& arg : func->args) newArgs.push_back(applyWeierstrass(arg).ptr);
                            return SymExpr(std::make_shared<SymFunc>(func->name, std::move(newArgs)));
                        }
                    }
                    return SymExpr(node);
                };

                try {
                    SymExpr subbed = applyWeierstrass(varPart.ptr) * dt_sub;
                    auto [num_t, den_t] = getFraction(subbed);
                    
                    SymExpr gcd_t = polyGCD(num_t, den_t, t_var);
                    if (!gcd_t.isOne() && !gcd_t.isZero()) {
                        num_t = polyDiv(num_t, gcd_t, t_var).first;
                        den_t = polyDiv(den_t, gcd_t, t_var).first;
                    }
                    
                    SymExpr rational_t = simplifyCore(expand(num_t / den_t, 500));
                    SymExpr rational_var = subs(rational_t, t_var, SymExpr::makeVar(var));
                    SymExpr integrated_var = doInteg(rational_var, depth + 1);
                    SymExpr back_sub = SymExpr(std::make_shared<SymFunc>("tan", std::vector<std::shared_ptr<SymNode>>{(SymExpr::makeVar(var) / SymExpr(BigInt(2))).ptr}));
                    return coeff * simplifyCore(subs(integrated_var, var, back_sub));
                } catch (...) {}
            }

            // 启发式方法全部失效，移交 Risch 算法处理
            return coeff * rischIntegrate(varPart, var);
        };

        auto tryInteg = [&]() -> SymExpr {
            try {
                return simplify(doInteg(expr, 0));
            } catch (const std::runtime_error& e) {
                std::string msg = e.what();
                SymExpr expanded;
                try { expanded = expand(expr, 1000); } catch (...) { expanded = expr; }
                
                // 严格防死锁：只有当展开后的表达式确实发生了变化，才允许重置 depth=0 再次尝试
                if (expanded.toString() != expr.toString()) {
                    try {
                        return simplify(doInteg(expanded, 0));
                    } catch (...) {}
                }
                
                // 如果是 Risch 算法抛出的进度信息，直接向外传递
                if (msg.find("Risch Algorithm") != std::string::npos) throw;
                
                throw std::runtime_error("Calculus Error: Function integration not supported or complex power. (" + msg + ")");
            } catch (...) {
                throw std::runtime_error("Calculus Error: Function integration not supported or complex power.");
            }
        };

        return tryInteg();
    }

} // namespace jc
