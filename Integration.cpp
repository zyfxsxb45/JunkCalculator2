#include "Integration.h"
#include "SymRules.h"
#include <stdexcept>
#include <algorithm>
#include <numeric>
#include <vector>
#include <map>
#include <unordered_map>
#include <functional>
#include <optional>

namespace jc {

    // =================================================================
    // Rothstein-Trager 算法 (Rothstein-Trager Algorithm)
    // 用于积分 A/D，其中 D 无平方，deg(A) < deg(D)
    // =================================================================
    static SymExpr rothsteinTrager(SymExpr A, SymExpr D, const std::string& var) {
        SymExpr z = SymExpr::makeVar("_z");
        SymExpr dD = diff(D, var);
        SymExpr P = simplifyCore(expand(A - z * dD, SymConfig::maxExpandTerms));
        
        SymExpr R_z = polyResultant(P, D, var);
        R_z = getFraction(R_z).first; // 提取分子，消除伪除法引入的 z 分母
        
        std::vector<SymExpr> roots = solveEq(R_z, "_z");
        if (roots.empty()) {
            if (R_z.isZero() || getDegree(R_z, "_z") > 0) {
                throw std::runtime_error("Calculus Error: Rothstein-Trager failed to find roots of the resultant.");
            }
            return SymExpr(BigInt(0));
        }
        
        SymExpr result(BigInt(0));
        for (const auto& root : roots) {
            SymExpr P_i = simplifyCore(expand(subs(P, "_z", root), SymConfig::maxExpandTerms));
            SymExpr v_i = polyGCD(P_i, D, var);
            
            // 首一化 v_i，保持对数内部整洁
            auto coeffs = extractCoeffs(v_i, var);
            if (!coeffs.empty()) {
                SymExpr lead = coeffs.back();
                if (!lead.isZero() && !lead.isOne()) {
                    v_i = simplifyCore(expand(v_i / lead, SymConfig::maxExpandTerms));
                }
            }
            
            SymExpr log_vi(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{v_i.ptr}));
            result = result + root * log_vi;
        }
        return simplifyCore(result);
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
            std::function<SymExpr(const SymExpr&)> preprocessPow = [&](const SymExpr& e) -> SymExpr {
                if (!e.ptr) return e;
                switch (e.ptr->getType()) {
                    case SymType::ADD: {
                        SymExpr r(BigInt(0));
                        for (auto& arg : std::static_pointer_cast<SymAdd>(e.ptr)->args) r = r + preprocessPow(SymExpr(arg));
                        return r;
                    }
                    case SymType::MUL: {
                        SymExpr r(BigInt(1));
                        for (auto& arg : std::static_pointer_cast<SymMul>(e.ptr)->args) r = r * preprocessPow(SymExpr(arg));
                        return r;
                    }
                    case SymType::POW: {
                        auto p = std::static_pointer_cast<SymPow>(e.ptr);
                        SymExpr base = preprocessPow(SymExpr(p->base));
                        SymExpr exp = preprocessPow(SymExpr(p->exp));
                        if (containsVar(exp.ptr, x_var)) {
                            SymExpr log_base(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{base.ptr}));
                            return SymExpr(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(exp * log_base).ptr}));
                        }
                        return base ^ exp;
                    }
                    case SymType::FUNC: {
                        auto f = std::static_pointer_cast<SymFunc>(e.ptr);
                        std::vector<std::shared_ptr<SymNode>> nArgs;
                        for (auto& arg : f->args) nArgs.push_back(preprocessPow(SymExpr(arg)).ptr);
                        return SymExpr(std::make_shared<SymFunc>(f->name, std::move(nArgs)));
                    }
                    default: return e;
                }
            };
            SymExpr res = preprocessPow(expr);
            
            // 从塔顶向下替换，确保嵌套扩张被正确处理 (如 exp(exp(x)))
            for (auto it = tower.rbegin(); it != tower.rend(); ++it) {
                if (it->type == RischExtType::LOG) {
                    SymExpr targetLog(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{it->arg.ptr}));
                    res = applyRule(res, targetLog, it->t_var);
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
                if (containsVar(p->exp, x)) {
                    SymExpr log_base(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{p->base}));
                    SymExpr exp_node(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{(SymExpr(p->exp) * log_base).ptr}));
                    buildRischFieldRec(exp_node, x, field, counter);
                } else {
                    buildRischFieldRec(SymExpr(p->base), x, field, counter);
                    buildRischFieldRec(SymExpr(p->exp), x, field, counter);
                }
                break;
            }
            case SymType::FUNC: {
                auto f = std::static_pointer_cast<SymFunc>(expr.ptr);
                for (auto& arg : f->args) buildRischFieldRec(SymExpr(arg), x, field, counter);
                
                if (f->name == "log" || f->name == "exp") {
                    SymExpr arg(f->args[0]);
                    if (!containsVar(arg.ptr, x)) break; // 常数不构成扩张
                    
                    // 检查是否已在塔中
                    bool exists = false;
                    for (const auto& ext : field.tower) {
                        if ((ext.type == RischExtType::LOG && f->name == "log") ||
                            (ext.type == RischExtType::EXP && f->name == "exp")) {
                            if (ext.arg == arg) {
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
    static std::tuple<SymExpr, SymExpr, SymExpr, SymExpr> hermiteReduce(SymExpr A, SymExpr D, const std::string& var) {
        A = simplifyCore(expand(A, SymConfig::maxExpandTerms));
        D = simplifyCore(expand(D, SymConfig::maxExpandTerms));
        
        auto [Q, R] = polyDiv(A, D, var);
        SymExpr rationalPart(0);
        SymExpr polyPart = Q; 
        
        A = R;
        if (A.isZero()) return {rationalPart, polyPart, SymExpr(BigInt(0)), SymExpr(BigInt(1))};

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

            SymExpr Vi = simplifyCore(expand(V ^ SymExpr(i), SymConfig::maxExpandTerms));
            SymExpr U = polyDiv(currentD, Vi, var).first;

            SymExpr dV = diff(V, var);
            SymExpr U_dV = simplifyCore(expand(U * dV, SymConfig::maxExpandTerms));

            auto [gcd, S, T] = polyEGCD(U_dV, V, var);
            SymExpr factor = simplifyCore(expand(currentA / gcd, SymConfig::maxExpandTerms));
            SymExpr B = simplifyCore(expand(S * factor, SymConfig::maxExpandTerms));
            SymExpr C = simplifyCore(expand(T * factor, SymConfig::maxExpandTerms));

            auto [q, r] = polyDiv(B, V, var);
            B = r;
            C = simplifyCore(expand(C + q * U_dV, SymConfig::maxExpandTerms));

            SymExpr V_im1 = simplifyCore(expand(V ^ SymExpr(i - 1), SymConfig::maxExpandTerms));
            SymExpr denom = simplifyCore(expand(SymExpr(i - 1) * V_im1, SymConfig::maxExpandTerms));
            rationalPart = rationalPart + simplifyCore(expand(-B / denom, SymConfig::maxExpandTerms));

            SymExpr dB = diff(B, var);
            SymExpr term2 = simplifyCore(expand((dB * U) / SymExpr(i - 1), SymConfig::maxExpandTerms));
            currentA = simplifyCore(expand(C + term2, SymConfig::maxExpandTerms));
            
            currentD = simplifyCore(expand(U * V_im1, SymConfig::maxExpandTerms));
            
            for (auto& p : sqFree) {
                if (p.second == i) p.second = i - 1;
            }
        }
        
        return {rationalPart, polyPart, currentA, currentD};
    }

    // =================================================================
    // 🚀 Risch 算法入口 (Risch Algorithm Entry Point)
    // =================================================================
    SymExpr rischIntegrate(const SymExpr& expr, const std::string& var) {
        // Step 0 - 刘维尔域规范化 (Liouvillian Field Normalization)
        SymExpr normalizedExpr = rischNormalize(expr);

        // Step 1 - 构建微分域与刘维尔扩张
        RischDiffField field = buildRischDifferentialField(normalizedExpr, var);
        SymExpr rewritten = field.rewrite(normalizedExpr);
        
        // Step 1.5 - 单一扩张的变量代换降维打击 (Change of Variables for Single Extension)
        if (field.tower.size() == 1) {
            auto ext = field.tower[0];
            
            // 形式 1: 积分恰好是 F(t) * t' dx = F(t) dt
            SymExpr F_t = simplifyCore(rewritten / ext.deriv);
            if (!containsVar(F_t.ptr, var)) {
                try {
                    SymExpr int_t = integrate(F_t, ext.name);
                    SymExpr orig_ext;
                    if (ext.type == RischExtType::LOG) {
                        orig_ext = SymExpr(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{ext.arg.ptr}));
                    } else {
                        orig_ext = SymExpr(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{ext.arg.ptr}));
                    }
                    return simplifyCore(subs(int_t, ext.name, orig_ext));
                } catch (...) {}
            }
            
            // 形式 2: 积分是 F(e^(ax+b)) dx，且 F 中不含 x
            if (ext.type == RischExtType::EXP && !containsVar(rewritten.ptr, var)) {
                SymExpr u_deriv = simplifyCore(diff(ext.arg, var));
                if (!containsVar(u_deriv.ptr, var) && !u_deriv.isZero()) {
                    try {
                        SymExpr integrand_t = simplifyCore(rewritten / (u_deriv * ext.t_var));
                        SymExpr int_t = integrate(integrand_t, ext.name);
                        SymExpr orig_ext(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{ext.arg.ptr}));
                        return simplifyCore(subs(int_t, ext.name, orig_ext));
                    } catch (...) {}
                }
            }
        }

        // Step 2 - Hermite 约化 (目前仅针对有理函数域 Q(x))
        if (field.tower.empty()) {
            auto [num, den] = getFraction(rewritten);
            if (getDegree(den, var) > 0) {
                auto [ratPart, polyPart, cA, cD] = hermiteReduce(num, den, var);
            
                SymExpr intPart = polyPart;
                if (!cA.isZero()) intPart = intPart + simplifyCore(expand(cA / cD, SymConfig::maxExpandTerms));
                
                if (intPart.isZero()) return ratPart;
                
                try {
                    if (intPart == normalizedExpr) throw std::runtime_error("Loop");
                    if (getAstNodeCount(intPart) >= getAstNodeCount(normalizedExpr)) throw std::runtime_error("Complexity increased");
                    SymExpr intRes = integrate(intPart, var);
                    return ratPart + intRes;
                } catch (const std::exception& e) {
                    std::string debugInfo = "Hermite Reduction completed, but remaining integral is non-elementary.\n";
                    debugInfo += "Original: " + normalizedExpr.toString() + "\n";
                    debugInfo += "Rational Part: " + ratPart.toString() + "\n";
                    debugInfo += "Remaining Integral: \\int (" + intPart.toString() + ") d" + var + "\n";
                    debugInfo += "Reason: " + std::string(e.what()) + "\n";
                    throw std::runtime_error("Calculus Error: " + debugInfo);
                } catch (...) {
                    std::string debugInfo = "Hermite Reduction completed, but remaining integral is non-elementary.\n";
                    debugInfo += "Original: " + normalizedExpr.toString() + "\n";
                    debugInfo += "Rational Part: " + ratPart.toString() + "\n";
                    debugInfo += "Remaining Integral: \\int (" + intPart.toString() + ") d" + var + "\n";
                    throw std::runtime_error("Calculus Error: " + debugInfo);
                }
            }
        }
        
        // Step 4 & 5 - 扩张多项式积分 (Extension Polynomial Integration)
        if (!field.tower.empty()) {
            auto topExt = field.tower.back();
            
            auto backSubstitute = [&](SymExpr res) {
                for (auto it = field.tower.rbegin(); it != field.tower.rend(); ++it) {
                    SymExpr orig;
                    if (it->type == RischExtType::LOG) {
                        orig = SymExpr(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{it->arg.ptr}));
                    } else {
                        orig = SymExpr(std::make_shared<SymFunc>("exp", std::vector<std::shared_ptr<SymNode>>{it->arg.ptr}));
                    }
                    res = subs(res, it->name, orig);
                }
                return res;
            };

            if (topExt.type == RischExtType::LOG) {
                auto coeffs = extractCoeffs(rewritten, topExt.name);
                if (!coeffs.empty()) {
                    int n = static_cast<int>(coeffs.size()) - 1;
                    std::vector<SymExpr> B(n + 2, SymExpr(BigInt(0)));
                    SymExpr t_deriv = topExt.deriv;
                    SymExpr t_sym = SymExpr::makeVar(topExt.name);

                    try {
                        SymExpr intAn_x = backSubstitute(coeffs[n]);
                        SymExpr intAn = integrate(intAn_x, var);
                        SymExpr intAn_t = field.rewrite(intAn);
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
                            SymExpr integrand_x = backSubstitute(integrand);
                            SymExpr int_i = integrate(integrand_x, var);
                            SymExpr int_i_t = field.rewrite(int_i);
                            if (containsVar(int_i_t.ptr, topExt.name)) {
                                throw std::runtime_error("Non-elementary integral.");
                            }
                            B[i] = int_i_t;
                        }
                        
                        SymExpr result(BigInt(0));
                        for (int i = 0; i <= n + 1; ++i) {
                            if (!B[i].isZero()) {
                                result = result + B[i] * (t_sym ^ SymExpr(BigInt(i)));
                            }
                        }
                        return simplifyCore(backSubstitute(result));
                    } catch (...) {
                        // 失败则继续往下走，抛出调试信息
                    }
                }
            } else if (topExt.type == RischExtType::EXP) {
                auto coeffs = extractCoeffs(rewritten, topExt.name);
                if (!coeffs.empty()) {
                    int n = static_cast<int>(coeffs.size()) - 1;
                    SymExpr u_deriv = simplifyCore(topExt.deriv / SymExpr::makeVar(topExt.name));
                    SymExpr t_sym = SymExpr::makeVar(topExt.name);
                    
                    bool success = true;
                    SymExpr result(BigInt(0));
                    
                    for (int i = 0; i <= n; ++i) {
                        if (coeffs[i].isZero()) continue;
                        if (i == 0) {
                            try {
                                SymExpr integrand_x = backSubstitute(coeffs[0]);
                                SymExpr int_0 = integrate(integrand_x, var);
                                result = result + field.rewrite(int_0);
                            } catch (...) {
                                success = false;
                                break;
                            }
                        } else {
                            // 求解 Risch 微分方程 B_i' + i u' B_i = A_i
                            SymExpr A_i = backSubstitute(coeffs[i]);
                            SymExpr V_i = backSubstitute(simplifyCore(SymExpr(BigInt(i)) * u_deriv));
                            
                            SymExpr B_i(BigInt(0));
                            SymExpr currentA = A_i;
                            bool rde_success = false;
                            
                            int iter = 0;
                            while (true) {
                                if (currentA.isZero()) { rde_success = true; break; }
                                if (++iter > SymConfig::maxIterations) break;
                                
                                auto cA = extractCoeffs(currentA, var);
                                auto cV = extractCoeffs(V_i, var);
                                
                                if (cA.empty() || cV.empty()) {
                                    SymExpr B_term = simplifyCore(currentA / V_i);
                                    // 严格检查 B_term 是否为常数（不包含积分变量）
                                    if (!containsVar(B_term.ptr, var)) {
                                        B_i = B_i + B_term;
                                        rde_success = true;
                                    }
                                    break;
                                }
                                
                                int degA = static_cast<int>(cA.size()) - 1;
                                int degV = static_cast<int>(cV.size()) - 1;
                                if (degA < degV) break;
                                
                                int degB = degA - degV;
                                SymExpr leadB = simplifyCore(cA.back() / cV.back());
                                SymExpr B_term = simplifyCore(leadB * (SymExpr::makeVar(var) ^ SymExpr(BigInt(degB))));
                                
                                B_i = B_i + B_term;
                                SymExpr B_term_deriv = simplifyCore(diff(B_term, var));
                                currentA = simplifyCore(expand(currentA - B_term_deriv - V_i * B_term, SymConfig::maxExpandTerms));
                            }
                            
                            if (rde_success) {
                                SymExpr B_i_t = field.rewrite(B_i);
                                result = result + B_i_t * (t_sym ^ SymExpr(BigInt(i)));
                            } else {
                                success = false;
                                break;
                            }
                        }
                    }
                    
                    if (success) return simplifyCore(backSubstitute(result));
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
        
        throw std::runtime_error("Calculus Error: Integral is non-elementary or requires advanced Risch steps.\n" + debugInfo);
    }

    // =================================================================
    // 🚀 符号积分 (Symbolic Integration) - 基础多项式与初等函数
    // =================================================================
    SymExpr integrate(const SymExpr& expr, const std::string& var) {
        if (!expr.ptr) return expr;

        SymExpr x = SymExpr::makeVar(var);

        // 从规则库获取静态积分规则
        const auto& rules = getStaticIntegRules();

        std::unordered_map<std::string, std::optional<SymExpr>> integCache;
        std::function<std::optional<SymExpr>(const SymExpr&, int)> doInteg;

        auto doIntegImpl = [&](const SymExpr& e, int depth) -> std::optional<SymExpr> {
            if (depth > SymConfig::maxDepth / 3) return std::nullopt;

            if (!containsVar(e.ptr, var)) {
                return e * x; // ∫ c dx = c * x
            }

            if (e.ptr->getType() == SymType::ADD) {
                auto add = std::static_pointer_cast<SymAdd>(e.ptr);
                SymExpr res(BigInt(0));
                for (auto& arg : add->args) {
                    auto partRes = doInteg(SymExpr(arg), depth);
                    if (!partRes) return std::nullopt;
                    res = res + *partRes;
                }
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
                    // 积分规则中的 _x 必须精确匹配当前的积分变量 var
                    auto it_x = captures.find("_x");
                    if (it_x == captures.end() || it_x->second.ptr->getType() != SymType::VAR || 
                        std::static_pointer_cast<SymVar>(it_x->second.ptr)->name != var) {
                        continue;
                    }
                    
                    // 积分规则中的 _n, _a 必须是相对于积分变量的常数
                    bool validMatch = true;
                    for (const auto& cap : captures) {
                        if ((cap.first == "_n" || cap.first == "_a") && containsVar(cap.second.ptr, var)) {
                            validMatch = false;
                            break;
                        }
                    }
                    if (validMatch) {
                        integratedPart = substituteCaptures(rule.second, captures);
                        matched = true;
                        break;
                    }
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
                    SymExpr u_base(powNode->base);
                    SymExpr du_base = simplifyCore(diff(u_base, var));
                    if (!du_base.isZero() && !containsVar(du_base.ptr, var)) {
                        if (u_base.ptr->getType() != SymType::VAR) {
                            out_u = u_base;
                            out_a = du_base;
                            return true;
                        }
                    }
                    SymExpr u_exp(powNode->exp);
                    SymExpr du_exp = simplifyCore(diff(u_exp, var));
                    if (!du_exp.isZero() && !containsVar(du_exp.ptr, var)) {
                        if (u_exp.ptr->getType() != SymType::VAR) {
                            out_u = u_exp;
                            out_a = du_exp;
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
                    if (expr_node == sub_u) return u_sym;
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
                    SymExpr f_var = subs(f_u, u_var, SymExpr::makeVar(var));
                    if (auto int_var = doInteg(simplify(expand(f_var / sub_a, SymConfig::maxExpandTerms)), depth + 1)) {
                        SymExpr res = subs(*int_var, var, sub_u);
                        return coeff * res;
                    }
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
                        if (expr_node == u) return u_sym;
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
                                rem_u = simplify(expand(replaceXn(rem_u), SymConfig::maxExpandTerms));
                            }
                        }
                    }

                    if (!containsVar(rem_u.ptr, var)) {
                        SymExpr f_var = subs(rem_u, u_var, SymExpr::makeVar(var));
                        if (auto int_var = doInteg(simplify(expand(f_var, SymConfig::maxExpandTerms)), depth + 1)) {
                            SymExpr res = subs(*int_var, var, u);
                            return coeff * res;
                        }
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
                                SymExpr expanded = expand(halfAngle ^ SymExpr(BigInt(n / 2)), SymConfig::maxExpandTerms);
                                SymExpr cos2x = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{(SymExpr(BigInt(2)) * arg).ptr}));
                                expanded = subs(expanded, "_C", cos2x);
                                if (auto res = doInteg(expanded, depth + 1)) return coeff * (*res);
                            } else {
                                SymExpr _C = SymExpr::makeVar("_C");
                                SymExpr cosSq = SymExpr(BigInt(1)) - (_C ^ SymExpr(BigInt(2)));
                                SymExpr rem = SymExpr(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{arg.ptr})) * (cosSq ^ SymExpr(BigInt((n - 1) / 2)));
                                SymExpr expanded = expand(rem, SymConfig::maxExpandTerms);
                                SymExpr cosx = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                                expanded = subs(expanded, "_C", cosx);
                                if (auto res = doInteg(expanded, depth + 1)) return coeff * (*res);
                            }
                        } else if (func->name == "cos") {
                            if (n % 2 == 0) {
                                SymExpr _C = SymExpr::makeVar("_C");
                                SymExpr halfAngle = (SymExpr(BigInt(1)) + _C) / SymExpr(BigInt(2));
                                SymExpr expanded = expand(halfAngle ^ SymExpr(BigInt(n / 2)), SymConfig::maxExpandTerms);
                                SymExpr cos2x = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{(SymExpr(BigInt(2)) * arg).ptr}));
                                expanded = subs(expanded, "_C", cos2x);
                                if (auto res = doInteg(expanded, depth + 1)) return coeff * (*res);
                            } else {
                                SymExpr _S = SymExpr::makeVar("_S");
                                SymExpr sinSq = SymExpr(BigInt(1)) - (_S ^ SymExpr(BigInt(2)));
                                SymExpr rem = SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{arg.ptr})) * (sinSq ^ SymExpr(BigInt((n - 1) / 2)));
                                SymExpr expanded = expand(rem, SymConfig::maxExpandTerms);
                                SymExpr sinx = SymExpr(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                                expanded = subs(expanded, "_S", sinx);
                                if (auto res = doInteg(expanded, depth + 1)) return coeff * (*res);
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
                        SymExpr subbed = simplifyCore(subs(varPart, var, x_sub) * dx_sub);
                        SymExpr subbed_var = subs(subbed, "_t", SymExpr::makeVar(var));
                        if (auto int_var = doInteg(trigsimp(subbed_var), depth + 1)) {
                            SymExpr res = subs(*int_var, var, t_back);
                            return coeff * res;
                        }
                    }
                }
            }

            // --- 1.95 高次二项式分式积分 (Binomial Fraction Integration) ---
            if (varPart.ptr->getType() == SymType::POW) {
                auto powNode = std::static_pointer_cast<SymPow>(varPart.ptr);
                if (powNode->exp->getType() == SymType::NUM) {
                    auto [isIntExp, expVal] = extractExactInt(std::static_pointer_cast<SymNum>(powNode->exp)->value);
                    if (isIntExp && expVal == -1) {
                        auto coeffs = extractCoeffs(SymExpr(powNode->base), var);
                        if (coeffs.size() > 2) {
                            bool isBinomial = true;
                            int n = static_cast<int>(coeffs.size()) - 1;
                            for (int i = 1; i < n; ++i) {
                                if (!coeffs[i].isZero()) { isBinomial = false; break; }
                            }
                            if (isBinomial && !coeffs[n].isZero() && !coeffs[0].isZero()) {
                                SymExpr A = coeffs[n];
                                SymExpr B = coeffs[0];
                                
                                SymExpr C = simplifyCore(A / B);
                                bool isPos = false;
                                bool isNeg = false;
                                if (C.ptr->getType() == SymType::NUM) {
                                    isNeg = isCasNegative(std::static_pointer_cast<SymNum>(C.ptr)->value);
                                    isPos = !isNeg && !C.isZero();
                                } else if (C.ptr->getType() == SymType::MUL) {
                                    auto mul = std::static_pointer_cast<SymMul>(C.ptr);
                                    if (!mul->args.empty() && mul->args[0]->getType() == SymType::NUM) {
                                        isNeg = isCasNegative(std::static_pointer_cast<SymNum>(mul->args[0])->value);
                                        isPos = !isNeg;
                                    } else {
                                        isPos = true;
                                    }
                                } else {
                                    isPos = true;
                                }
                                
                                SymExpr absC = isNeg ? simplifyCore(-C) : C;
                                SymExpr u_coeff = simplifyCore(absC ^ SymExpr(Fraction(1, n)));
                                SymExpr u = simplifyCore(u_coeff * SymExpr::makeVar(var));
                                SymExpr du_dx = u_coeff;
                                
                                SymExpr n_sym = SymExpr(BigInt(n));
                                SymExpr res(BigInt(0));
                                SymExpr PI = SymExpr::makeVar("PI");
                                
                                auto makeCos = [&](SymExpr theta) {
                                    return SymExpr(std::make_shared<SymFunc>("cos", std::vector<std::shared_ptr<SymNode>>{theta.ptr}));
                                };
                                auto makeSin = [&](SymExpr theta) {
                                    return SymExpr(std::make_shared<SymFunc>("sin", std::vector<std::shared_ptr<SymNode>>{theta.ptr}));
                                };
                                auto makeLog = [&](SymExpr arg) {
                                    return SymExpr(std::make_shared<SymFunc>("log", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                                };
                                auto makeAtan = [&](SymExpr arg) {
                                    return SymExpr(std::make_shared<SymFunc>("atan", std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
                                };
                                
                                if (isPos) {
                                    if (n % 2 != 0) {
                                        res = res + (SymExpr(BigInt(1)) / n_sym) * makeLog(u + SymExpr(BigInt(1)));
                                        for (int k = 1; k <= (n - 1) / 2; ++k) {
                                            SymExpr theta = simplifyCore((SymExpr(BigInt(2 * k - 1)) * PI) / n_sym);
                                            SymExpr cos_th = makeCos(theta);
                                            SymExpr sin_th = makeSin(theta);
                                            SymExpr log_arg = simplifyCore((u ^ SymExpr(BigInt(2))) - SymExpr(BigInt(2)) * u * cos_th + SymExpr(BigInt(1)));
                                            SymExpr atan_arg = simplifyCore((u - cos_th) / sin_th);
                                            res = res - (cos_th / n_sym) * makeLog(log_arg) + (SymExpr(BigInt(2)) * sin_th / n_sym) * makeAtan(atan_arg);
                                        }
                                    } else {
                                        for (int k = 1; k <= n / 2; ++k) {
                                            SymExpr theta = simplifyCore((SymExpr(BigInt(2 * k - 1)) * PI) / n_sym);
                                            SymExpr cos_th = makeCos(theta);
                                            SymExpr sin_th = makeSin(theta);
                                            SymExpr log_arg = simplifyCore((u ^ SymExpr(BigInt(2))) - SymExpr(BigInt(2)) * u * cos_th + SymExpr(BigInt(1)));
                                            SymExpr atan_arg = simplifyCore((u - cos_th) / sin_th);
                                            res = res - (cos_th / n_sym) * makeLog(log_arg) + (SymExpr(BigInt(2)) * sin_th / n_sym) * makeAtan(atan_arg);
                                        }
                                    }
                                } else {
                                    SymExpr res_minus(BigInt(0));
                                    if (n % 2 != 0) {
                                        res_minus = res_minus + (SymExpr(BigInt(1)) / n_sym) * makeLog(u - SymExpr(BigInt(1)));
                                        for (int k = 1; k <= (n - 1) / 2; ++k) {
                                            SymExpr phi = simplifyCore((SymExpr(BigInt(2 * k)) * PI) / n_sym);
                                            SymExpr cos_ph = makeCos(phi);
                                            SymExpr sin_ph = makeSin(phi);
                                            SymExpr log_arg = simplifyCore((u ^ SymExpr(BigInt(2))) - SymExpr(BigInt(2)) * u * cos_ph + SymExpr(BigInt(1)));
                                            SymExpr atan_arg = simplifyCore((u - cos_ph) / sin_ph);
                                            res_minus = res_minus + (cos_ph / n_sym) * makeLog(log_arg) - (SymExpr(BigInt(2)) * sin_ph / n_sym) * makeAtan(atan_arg);
                                        }
                                    } else {
                                        res_minus = res_minus + (SymExpr(BigInt(1)) / n_sym) * makeLog(u - SymExpr(BigInt(1))) - (SymExpr(BigInt(1)) / n_sym) * makeLog(u + SymExpr(BigInt(1)));
                                        for (int k = 1; k <= n / 2 - 1; ++k) {
                                            SymExpr phi = simplifyCore((SymExpr(BigInt(2 * k)) * PI) / n_sym);
                                            SymExpr cos_ph = makeCos(phi);
                                            SymExpr sin_ph = makeSin(phi);
                                            SymExpr log_arg = simplifyCore((u ^ SymExpr(BigInt(2))) - SymExpr(BigInt(2)) * u * cos_ph + SymExpr(BigInt(1)));
                                            SymExpr atan_arg = simplifyCore((u - cos_ph) / sin_ph);
                                            res_minus = res_minus + (cos_ph / n_sym) * makeLog(log_arg) - (SymExpr(BigInt(2)) * sin_ph / n_sym) * makeAtan(atan_arg);
                                        }
                                    }
                                    res = -res_minus;
                                }
                                
                                return coeff * simplifyCore(res / (B * du_dx));
                            }
                        }
                    }
                }
            }

            // --- 2. 有理分式积分引擎 (Rational Function Integration) ---
            auto [num, den] = getFraction(varPart);
            bool isFraction = !den.isOne();

            if (isFraction && containsVar(den.ptr, var)) {
                SymExpr num_expanded = simplifyCore(expand(num, SymConfig::maxExpandTerms));
                SymExpr den_expanded = simplifyCore(expand(den, SymConfig::maxExpandTerms));
                
                int degN = getDegree(num_expanded, var);
                int degD = getDegree(den_expanded, var);
                
                // ★ 只有当分子和分母都是多项式时，才使用有理分式积分引擎
                if (degD >= 0 && (degN >= 0 || !containsVar(num_expanded.ptr, var))) {
                    try {
                        auto [ratPart, polyPart, cA, cD] = hermiteReduce(num_expanded, den_expanded, var);
                        SymExpr res = ratPart;
                        
                        if (!polyPart.isZero()) {
                            if (auto polyInt = doInteg(polyPart, depth)) {
                                res = res + *polyInt;
                            } else {
                                throw std::runtime_error("polyPart integration failed");
                            }
                        }
                        
                        if (!cA.isZero()) {
                            res = res + rothsteinTrager(cA, cD, var);
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
                        if (fn->name == "log" || fn->name == "asin" || fn->name == "acos" || fn->name == "atan") return 1;
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
                    // Try 1: u = factors[i], dv = rest
                    SymExpr u1 = factors[i];
                    SymExpr dv1(BigInt(1));
                    for (size_t j = 0; j < factors.size(); ++j) {
                        if (j != i) dv1 = dv1 * factors[j];
                    }

                    // Try 2: dv = factors[i], u = rest
                    SymExpr dv2 = factors[i];
                    SymExpr u2(BigInt(1));
                    for (size_t j = 0; j < factors.size(); ++j) {
                        if (j != i) u2 = u2 * factors[j];
                    }

                    auto tryPartsWith = [&](SymExpr u, SymExpr dv) -> std::optional<SymExpr> {
                        SymExpr du = simplifyCore(diff(u, var));
                        auto opt_v = doInteg(dv, depth + 1);
                        if (!opt_v) return std::nullopt;
                        SymExpr v = *opt_v;
                        SymExpr v_du = simplifyCore(v * du);
                        
                        // 循环分部积分检测 (1-step cycle)
                        SymExpr ratio1 = simplifyCore(v_du / varPart);
                        if (!containsVar(ratio1.ptr, var) && !ratio1.isZero()) {
                            SymExpr k = ratio1;
                            SymExpr one_plus_k = simplifyCore(SymExpr(BigInt(1)) + k);
                            if (!one_plus_k.isZero()) {
                                return coeff * simplify(expand((u * v) / one_plus_k, SymConfig::maxExpandTerms));
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
                            
                            for (size_t i2 : indices2) {
                                SymExpr u2_inner = factors2[i2];
                                SymExpr dv2_inner(BigInt(1));
                                for (size_t j2 = 0; j2 < factors2.size(); ++j2) {
                                    if (j2 != i2) dv2_inner = dv2_inner * factors2[j2];
                                }
                                SymExpr du2_inner = simplifyCore(diff(u2_inner, var));
                                auto opt_v2_inner = doInteg(dv2_inner, depth + 2);
                                if (opt_v2_inner) {
                                    SymExpr v2_inner = *opt_v2_inner;
                                    SymExpr v2_du2 = simplifyCore(v2_inner * du2_inner);
                                    
                                    SymExpr ratio2 = simplifyCore(v2_du2 / varPart);
                                    if (!containsVar(ratio2.ptr, var) && !ratio2.isZero()) {
                                        SymExpr k = ratio2;
                                        SymExpr one_minus_k = simplifyCore(SymExpr(BigInt(1)) - k);
                                        if (!one_minus_k.isZero()) {
                                            return coeff * simplifyCore((u * v - u2_inner * v2_inner) / one_minus_k);
                                        }
                                    }
                                }
                            }
                        }

                        auto opt_int_v_du = doInteg(v_du, depth + 1);
                        if (!opt_int_v_du) return std::nullopt;
                        return coeff * simplifyCore(u * v - *opt_int_v_du);
                    };

                    if (auto res1 = tryPartsWith(u1, dv1)) return *res1;
                    if (auto res2 = tryPartsWith(u2, dv2)) return *res2;
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
                    
                    SymExpr rational_t = simplifyCore(expand(num_t / den_t, SymConfig::maxExpandTerms));
                    SymExpr rational_var = subs(rational_t, t_var, SymExpr::makeVar(var));
                    if (auto opt_integrated_var = doInteg(rational_var, depth + 1)) {
                        SymExpr back_sub = SymExpr(std::make_shared<SymFunc>("tan", std::vector<std::shared_ptr<SymNode>>{(SymExpr::makeVar(var) / SymExpr(BigInt(2))).ptr}));
                        return coeff * simplifyCore(subs(*opt_integrated_var, var, back_sub));
                    }
                } catch (...) {}
            }

            // 启发式方法全部失效，移交 Risch 算法处理
            try {
                return coeff * rischIntegrate(varPart, var);
            } catch (...) {
                // 如果是在递归深层，静默返回 nullopt 让上层继续尝试其他分支
                // 如果是在最外层 (depth == 0)，则抛出异常以保留详细的 Risch 失败信息
                if (depth == 0) throw;
                return std::nullopt;
            }
        };

        doInteg = [&](const SymExpr& e, int depth) -> std::optional<SymExpr> {
            if (!e.ptr) return std::nullopt;
            std::string sig = e.ptr->getSignature();
            auto it = integCache.find(sig);
            if (it != integCache.end()) return it->second;
            
            auto res = doIntegImpl(e, depth);
            integCache[sig] = res;
            return res;
        };

        auto tryInteg = [&]() -> SymExpr {
            try {
                if (auto res = doInteg(expr, 0)) {
                    return simplify(*res);
                }
                throw std::runtime_error("Calculus Error: Function integration not supported or complex power.");
            } catch (const std::runtime_error& e) {
                std::string msg = e.what();
                SymExpr expanded;
                try { expanded = expand(expr, SymConfig::maxExpandTerms * 2); } catch (...) { expanded = expr; }
                
                // 严格防死锁：只有当展开后的表达式确实发生了变化，才允许重置 depth=0 再次尝试
                if (expanded != expr) {
                    try {
                        if (auto res2 = doInteg(expanded, 0)) {
                            return simplify(*res2);
                        }
                    } catch (const std::runtime_error& e2) {
                        std::string msg2 = e2.what();
                        // 如果展开后再次失败，保留新的错误信息
                        if (msg2.find("Risch Field") != std::string::npos || 
                            msg2.find("Hermite Reduction") != std::string::npos ||
                            msg2.find("Integration depth limit exceeded") != std::string::npos ||
                            msg2.find("Calculus Error:") == 0) {
                            msg = msg2;
                        }
                    } catch (...) {}
                }
                
                // 如果是 Risch 算法抛出的进度信息，直接向外传递
                if (msg.find("Risch Field") != std::string::npos || 
                    msg.find("Hermite Reduction") != std::string::npos || 
                    msg.find("Integration depth limit exceeded") != std::string::npos) {
                    throw std::runtime_error(msg);
                }
                
                if (msg.find("Calculus Error:") == 0) {
                    throw std::runtime_error(msg);
                }
                
                throw std::runtime_error("Calculus Error: Function integration not supported or complex power. (" + msg + ")");
            } catch (...) {
                throw std::runtime_error("Calculus Error: Function integration not supported or complex power.");
            }
        };

        return tryInteg();
    }

} // namespace jc
