#ifndef JC2_MODULE_PROB_H
#define JC2_MODULE_PROB_H

#include "../Module.h"
#include "../Probability.h"

namespace jc_prob {
    using namespace jc;

    inline std::shared_ptr<ClassDefinition> distClass;

    inline std::shared_ptr<Distribution>& getDist(const Value& v) {
        if (!std::holds_alternative<std::shared_ptr<Instance>>(v.data))
            throw std::runtime_error("Type Error: Expected a Distribution.");
        auto inst = std::get<std::shared_ptr<Instance>>(v.data);
        if (!inst->nativeData.has_value())
            throw std::runtime_error("Type Error: Expected a Distribution.");
        return std::any_cast<std::shared_ptr<Distribution>&>(inst->nativeData);
    }

    inline Value makeDist(Distribution d) {
        auto inst = std::make_shared<Instance>();
        inst->classDef = distClass;
        inst->nativeData = std::make_shared<Distribution>(std::move(d));
        return Value(inst);
    }
}

JC2_MODULE(prob) {
    using namespace jc_prob;
    jc::ModuleReg R(env, builtins, arity);
    distClass = std::make_shared<jc::ClassDefinition>();
    distClass->name = "Distribution";
    R.set("Distribution", jc::Value(distClass));
    R.reg("gamma", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(jc::prob::tgamma(a[0].asDouble())); });
    R.reg("lgamma", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(jc::prob::lngamma(a[0].asDouble())); });
    R.reg("betaFn", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(jc::prob::betafn(a[0].asDouble(), a[1].asDouble())); });
    R.reg("erf", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(jc::prob::erf_impl(a[0].asDouble())); });
    R.reg("erfc", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(jc::prob::erfc_impl(a[0].asDouble())); });

    // ── 分布构造器 ──
    R.reg("Normal", { 0, 1, 2 }, [](const std::vector<jc::Value>& a) -> jc::Value {
        double mu = a.size() >= 1 ? a[0].asDouble() : 0;
        double sigma = a.size() >= 2 ? a[1].asDouble() : 1;
        return makeDist(Distribution::normal(mu, sigma));
        });

    R.reg("TDist", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::studentT(a[0].asDouble())); });
    R.reg("Chi2", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::chiSquared(a[0].asDouble())); });
    R.reg("FDist", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::fDist(a[0].asDouble(), a[1].asDouble())); });
    R.reg("ExpDist", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::exponential(a[0].asDouble())); });
    R.reg("GammaDist", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::gammaDist(a[0].asDouble(), a[1].asDouble())); });
    R.reg("BetaDist", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::betaDist(a[0].asDouble(), a[1].asDouble())); });
    R.reg("Uniform", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::uniformDist(a[0].asDouble(), a[1].asDouble())); });
    R.reg("Binom", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::binomial(static_cast<int>(std::round(a[0].asDouble())), a[1].asDouble())); });
    R.reg("Poisson", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::poisson(a[0].asDouble())); });
    R.reg("Geom", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return makeDist(Distribution::geometric(a[0].asDouble())); });
    // ── 泛型操作 ──
    R.reg("pdf", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(getDist(a[0])->pdf(a[1].asDouble())); });
    R.reg("pmf", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(getDist(a[0])->pdf(a[1].asDouble())); });
    R.reg("cdf", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(getDist(a[0])->cdf(a[1].asDouble())); });
    R.reg("quantile", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(getDist(a[0])->quantile(a[1].asDouble())); });
    R.reg("dmean", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(getDist(a[0])->distMean()); });
    R.reg("dvar", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(getDist(a[0])->distVar()); });
    R.reg("dstd", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value { return jc::Value(std::sqrt(getDist(a[0])->distVar())); });

    R.reg("sample", { 2 }, [](const std::vector<jc::Value>& a) -> jc::Value {
        int n = static_cast<int>(std::round(a[1].asDouble()));
        if (n <= 0) throw std::runtime_error("Runtime Error: sample() count must be positive.");
        auto data = getDist(a[0])->sample(n);
        return jc::Value(jc::RealMatrix(1, n, data));
        });

    R.reg("distInfo", { 1 }, [](const std::vector<jc::Value>& a) -> jc::Value {
        auto d = getDist(a[0]);
        std::cout << "  Distribution: " << d->toString() << std::endl;
        std::cout << "  Type:         " << (d->isDiscrete() ? "Discrete" : "Continuous") << std::endl;
        try { std::cout << "  Mean:         " << d->distMean() << std::endl; }
        catch (...) {}
        try { std::cout << "  Variance:     " << d->distVar() << std::endl; }
        catch (...) {}
        try { std::cout << "  Std Dev:      " << std::sqrt(d->distVar()) << std::endl; }
        catch (...) {}
        return Value::none();
        });

    // ── 假设检验 ──
    auto extractDS = [](const Value& v, const std::string& f) -> std::vector<double> {
        if (std::holds_alternative<RealMatrix>(v.data)) return std::get<RealMatrix>(v.data).rawData();
        if (std::holds_alternative<ComplexMatrix>(v.data)) {
            const auto& cd = std::get<ComplexMatrix>(v.data).rawData();
            std::vector<double> r(cd.size());
            for (size_t i = 0; i < cd.size(); ++i) {
                if (std::abs(cd[i].imag) > 1e-15) throw std::runtime_error(f + "() requires real data.");
                r[i] = cd[i].real;
            }
            return r;
        }
        throw std::runtime_error(f + "() requires a matrix/vector.");
        };

    auto printTest = [](const TestResult& r) {
        std::cout << "  " << r.name << std::endl;
        std::cout << "  statistic = " << r.statistic << ",  df = " << r.df
            << ",  p-value = " << r.pValue << std::endl;
        if (r.pValue < 0.001)      std::cout << "  Significance: *** (p < 0.001)" << std::endl;
        else if (r.pValue < 0.01)  std::cout << "  Significance: **  (p < 0.01)" << std::endl;
        else if (r.pValue < 0.05)  std::cout << "  Significance: *   (p < 0.05)" << std::endl;
        else                       std::cout << "  Significance: n.s. (p >= 0.05)" << std::endl;
        };

    R.reg("ttest", { 1, 2 }, [extractDS, printTest](const std::vector<jc::Value>& a) -> jc::Value {
        auto data = extractDS(a[0], "ttest");
        double mu0 = a.size() >= 2 ? a[1].asDouble() : 0.0;
        auto r = ttest1(data, mu0);
        printTest(r);
        return Value(RealMatrix(1, 3, { r.statistic, r.df, r.pValue }));
        });

    R.reg("ttest2", { 2 }, [extractDS, printTest](const std::vector<jc::Value>& a) -> jc::Value {
        auto d1 = extractDS(a[0], "ttest2"), d2 = extractDS(a[1], "ttest2");
        auto r = ttest2ind(d1, d2);
        printTest(r);
        return jc::Value(jc::RealMatrix(1, 3, { r.statistic, r.df, r.pValue }));
        });

    R.reg("ttestP", { 2 }, [extractDS, printTest](const std::vector<jc::Value>& a) -> jc::Value {
        auto d1 = extractDS(a[0], "ttestP"), d2 = extractDS(a[1], "ttestP");
        auto r = ttestPaired(d1, d2);
        printTest(r);
        return jc::Value(jc::RealMatrix(1, 3, { r.statistic, r.df, r.pValue }));
        });

    R.reg("chi2test", { 2 }, [extractDS, printTest](const std::vector<jc::Value>& a) -> jc::Value {
        auto obs = extractDS(a[0], "chi2test"), exp = extractDS(a[1], "chi2test");
        auto r = chi2test(obs, exp);
        printTest(r);
        return jc::Value(jc::RealMatrix(1, 3, { r.statistic, r.df, r.pValue }));
        });

    // ★ 覆盖 mean/var/std 以支持 Distribution
    {
        auto oldMean = builtins["mean"];
        R.reg("mean", { 1 }, [oldMean](const std::vector<jc::Value>& args) -> jc::Value {
            if (std::holds_alternative<std::shared_ptr<jc::Instance>>(args[0].data)) {
                auto inst = std::get<std::shared_ptr<jc::Instance>>(args[0].data);
                if (inst->nativeData.has_value() &&
                    inst->nativeData.type() == typeid(std::shared_ptr<jc::Distribution>)) {
                    return jc::Value(getDist(args[0])->distMean());
                }
            }
            return oldMean(args);
            });
    }
    {
        auto oldVar = builtins["var"];
        R.reg("var", { 1 }, [oldVar](const std::vector<jc::Value>& args) -> jc::Value {
            if (std::holds_alternative<std::shared_ptr<jc::Instance>>(args[0].data)) {
                auto inst = std::get<std::shared_ptr<jc::Instance>>(args[0].data);
                if (inst->nativeData.has_value() &&
                    inst->nativeData.type() == typeid(std::shared_ptr<jc::Distribution>)) {
                    return jc::Value(getDist(args[0])->distVar());
                }
            }
            return oldVar(args);
            });
    }
    {
        auto oldStd = builtins["std"];
        R.reg("std", { 1 }, [oldStd](const std::vector<jc::Value>& args) -> jc::Value {
            if (std::holds_alternative<std::shared_ptr<jc::Instance>>(args[0].data)) {
                auto inst = std::get<std::shared_ptr<jc::Instance>>(args[0].data);
                if (inst->nativeData.has_value() &&
                    inst->nativeData.type() == typeid(std::shared_ptr<jc::Distribution>)) {
                    return jc::Value(std::sqrt(getDist(args[0])->distVar()));
                }
            }
            return oldStd(args);
            });
    }
}

#endif
