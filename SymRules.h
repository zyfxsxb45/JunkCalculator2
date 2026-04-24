#ifndef JC2_SYMRULES_H
#define JC2_SYMRULES_H

#include "Symbolic.h"
#include <vector>
#include <utility>
#include <string>

namespace jc {

    // =================================================================
    // 积分规则字典表 (动态生成，绑定特定的积分变量 var)
    // =================================================================
    inline std::vector<std::pair<SymExpr, SymExpr>> getIntegRules(const std::string& var) {
        SymExpr x = SymExpr::makeVar(var);
        SymExpr _n = SymExpr::makeVar("_n");

        auto func = [](const std::string& name, const SymExpr& arg) {
            return SymExpr(std::make_shared<SymFunc>(name, std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
        };

        return {
            // 幂函数与对数
            { x ^ SymExpr(-1), func("log", x) },
            { x ^ _n, (SymExpr(1) / (_n + SymExpr(1))) * (x ^ (_n + SymExpr(1))) },
            { x, (SymExpr(1) / SymExpr(2)) * (x ^ SymExpr(2)) },
            
            // 指数函数
            { func("exp", x), func("exp", x) },
            
            // 绝对值与符号函数
            { func("sgn", x), func("abs", x) },
            
            // 基础三角函数
            { func("sin", x), -func("cos", x) },
            { func("cos", x), func("sin", x) },
            { SymExpr(1) / (func("cos", x) ^ SymExpr(2)), func("tan", x) },
            { SymExpr(1) / (func("sin", x) ^ SymExpr(2)), -SymExpr(1) / func("tan", x) },
            
            // 三角函数平方
            { func("tan", x) ^ SymExpr(2), func("tan", x) - x },
            
            // 反三角函数导数逆运算
            { SymExpr(1) / (SymExpr(1) + (x ^ SymExpr(2))), func("atan", x) },
            { SymExpr(1) / ((SymExpr(1) - (x ^ SymExpr(2))) ^ SymExpr(Fraction(1, 2))), func("asin", x) },
            
            // 反双曲函数导数逆运算
            { SymExpr(1) / ((SymExpr(1) + (x ^ SymExpr(2))) ^ SymExpr(Fraction(1, 2))), func("asinh", x) },
            { SymExpr(1) / (((x ^ SymExpr(2)) - SymExpr(1)) ^ SymExpr(Fraction(1, 2))), func("acosh", x) },
            
            // 双曲函数
            { func("sinh", x), func("cosh", x) },
            { func("cosh", x), func("sinh", x) },
            { func("tanh", x), func("log", func("cosh", x)) },
            { SymExpr(1) / (func("cosh", x) ^ SymExpr(2)), func("tanh", x) },
            { SymExpr(1) / (func("sinh", x) ^ SymExpr(2)), -SymExpr(1) / func("tanh", x) },
            { func("tanh", x) ^ SymExpr(2), x - func("tanh", x) },
            
            // 特殊函数 (误差函数与菲涅尔积分)
            { func("exp", -(x ^ SymExpr(2))), (SymExpr(Fraction(1, 2)) * (SymExpr::makeVar("PI") ^ SymExpr(Fraction(1, 2)))) * func("erf", x) },
            { func("sin", x ^ SymExpr(2)), ((SymExpr::makeVar("PI") / SymExpr(2)) ^ SymExpr(Fraction(1, 2))) * func("fresnel_s", ((SymExpr(2) / SymExpr::makeVar("PI")) ^ SymExpr(Fraction(1, 2))) * x) },
            { func("cos", x ^ SymExpr(2)), ((SymExpr::makeVar("PI") / SymExpr(2)) ^ SymExpr(Fraction(1, 2))) * func("fresnel_c", ((SymExpr(2) / SymExpr::makeVar("PI")) ^ SymExpr(Fraction(1, 2))) * x) },
            
            // 积分正弦、余弦与指数积分
            { func("sin", x) / x, func("Si", x) },
            { func("cos", x) / x, func("Ci", x) },
            { func("exp", x) / x, func("Ei", x) }
        };
    }

    // =================================================================
    // 三角化简规则字典表 (静态缓存，使用通配符 _x, _c)
    // =================================================================
    inline const std::vector<std::pair<SymExpr, SymExpr>>& getTrigRules() {
        static std::vector<std::pair<SymExpr, SymExpr>> rules;
        if (rules.empty()) {
            SymExpr _x = SymExpr::makeVar("_x");
            SymExpr _c = SymExpr::makeVar("_c");

            auto func = [](const std::string& name, const SymExpr& arg) {
                return SymExpr(std::make_shared<SymFunc>(name, std::vector<std::shared_ptr<SymNode>>{arg.ptr}));
            };

            SymExpr sin_x = func("sin", _x);
            SymExpr cos_x = func("cos", _x);
            SymExpr tan_x = func("tan", _x);
            SymExpr sin_2x = func("sin", SymExpr(2) * _x);
            SymExpr cos_2x = func("cos", SymExpr(2) * _x);

            rules = {
                // 平方和恒等式
                { (sin_x ^ SymExpr(2)) + (cos_x ^ SymExpr(2)), SymExpr(1) },
                { _c * (sin_x ^ SymExpr(2)) + _c * (cos_x ^ SymExpr(2)), _c },
                
                // 平方差转换
                { SymExpr(1) - (sin_x ^ SymExpr(2)), cos_x ^ SymExpr(2) },
                { SymExpr(1) - (cos_x ^ SymExpr(2)), sin_x ^ SymExpr(2) },
                { _c - _c * (sin_x ^ SymExpr(2)), _c * (cos_x ^ SymExpr(2)) },
                { _c - _c * (cos_x ^ SymExpr(2)), _c * (sin_x ^ SymExpr(2)) },
                
                // 商数关系
                { sin_x / cos_x, tan_x },
                { cos_x / sin_x, SymExpr(1) / tan_x },
                { (sin_x ^ SymExpr(2)) / (cos_x ^ SymExpr(2)), tan_x ^ SymExpr(2) },
                { (cos_x ^ SymExpr(2)) / (sin_x ^ SymExpr(2)), SymExpr(1) / (tan_x ^ SymExpr(2)) },
                
                // 乘积与消去关系
                { tan_x * cos_x, sin_x },
                { cos_x * tan_x, sin_x },
                { sin_x / tan_x, cos_x },
                { tan_x / sin_x, SymExpr(1) / cos_x },
                
                // 割线与正切的平方关系 (1 + tan^2 = sec^2)
                { SymExpr(1) + (tan_x ^ SymExpr(2)), SymExpr(1) / (cos_x ^ SymExpr(2)) },
                { _c + _c * (tan_x ^ SymExpr(2)), _c / (cos_x ^ SymExpr(2)) },
                { (SymExpr(1) / (cos_x ^ SymExpr(2))) - (tan_x ^ SymExpr(2)), SymExpr(1) },
                { (SymExpr(1) / (cos_x ^ SymExpr(2))) - SymExpr(1), tan_x ^ SymExpr(2) },

                // 倍角公式逆向化简
                { sin_x * cos_x, (SymExpr(1) / SymExpr(2)) * sin_2x },
                { SymExpr(2) * sin_x * cos_x, sin_2x },
                { _c * sin_x * cos_x, (_c / SymExpr(2)) * sin_2x },
                { (cos_x ^ SymExpr(2)) - (sin_x ^ SymExpr(2)), cos_2x },
                { _c * (cos_x ^ SymExpr(2)) - _c * (sin_x ^ SymExpr(2)), _c * cos_2x },
                { SymExpr(1) - SymExpr(2) * (sin_x ^ SymExpr(2)), cos_2x },
                { SymExpr(2) * (cos_x ^ SymExpr(2)) - SymExpr(1), cos_2x },

                // 倍角公式正向约分
                { sin_2x / cos_x, SymExpr(2) * sin_x },
                { sin_2x / sin_x, SymExpr(2) * cos_x },

                // 负角化简
                { func("sin", SymExpr(-1) * _x), -sin_x },
                { func("cos", SymExpr(-1) * _x), cos_x },
                { func("tan", SymExpr(-1) * _x), -tan_x },

                // 反三角函数嵌套化简
                { func("sin", func("asin", _x)), _x },
                { func("cos", func("acos", _x)), _x },
                { func("tan", func("atan", _x)), _x },
                { func("cos", func("asin", _x)), (SymExpr(1) - (_x ^ SymExpr(2))) ^ SymExpr(Fraction(1, 2)) },
                { func("sin", func("acos", _x)), (SymExpr(1) - (_x ^ SymExpr(2))) ^ SymExpr(Fraction(1, 2)) },
                { func("sin", func("atan", _x)), _x / ((SymExpr(1) + (_x ^ SymExpr(2))) ^ SymExpr(Fraction(1, 2))) },
                { func("cos", func("atan", _x)), SymExpr(1) / ((SymExpr(1) + (_x ^ SymExpr(2))) ^ SymExpr(Fraction(1, 2))) },
                { func("tan", func("asin", _x)), _x / ((SymExpr(1) - (_x ^ SymExpr(2))) ^ SymExpr(Fraction(1, 2))) },
                { func("tan", func("acos", _x)), ((SymExpr(1) - (_x ^ SymExpr(2))) ^ SymExpr(Fraction(1, 2))) / _x }
            };
        }
        return rules;
    }

} // namespace jc

#endif // JC2_SYMRULES_H
