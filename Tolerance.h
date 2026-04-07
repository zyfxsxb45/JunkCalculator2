#ifndef JC2_TOLERANCE_H
#define JC2_TOLERANCE_H

#include <algorithm>
#include <cmath>
#include <limits>

namespace jc {

    // =================================================================
    // 极客浮点制导引擎 (IEEE 754 Dynamic Tolerance)
    // 彻底消灭全局魔数，所有容差根据数据本身的 Scale 自主呼吸！
    // =================================================================
    struct Tol {
        // double 的机器极小分辨率 (约 2.22e-16)
        static constexpr double EPS = std::numeric_limits<double>::epsilon();

        // double 的最小正规化数 (绝对底线，约 2.22e-308)
        static constexpr double MIN_VAL = std::numeric_limits<double>::min();

        // 相对等价判定 (放宽一定 ULP 防累计误差)
        static bool isEq(double a, double b, double ulp_scale = 100.0) {
            if (a == b) return true;
            double diff = std::abs(a - b);
            return diff <= std::max(std::abs(a), std::abs(b)) * EPS * ulp_scale;
        }

        // 在给定基准规模 (refScale) 下，清洗噪音
        static double clean(double val, double refScale, double ulp_scale = 1e4) {
            if (std::abs(val) <= std::max(refScale * EPS * ulp_scale, MIN_VAL)) return 0.0;
            return val;
        }
    };

} // namespace jc

#endif // JC2_TOLERANCE_H
