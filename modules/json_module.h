#ifndef JC2_MODULE_JSON_H
#define JC2_MODULE_JSON_H

#include "../Module.h"
#include <sstream>
#include <cmath>
#include <iomanip>
#include <cctype>    // 用于 std::isdigit
#include <cstdio>    // 用于 std::snprintf
#include <stdexcept>
#include <string>
#include <vector>

namespace jc {

    // ═══ 纯静态无状态的序列化引擎 ═══
    struct JsonEngine {
        static std::string encode(const jc::Value& val, int indent, int level) {
            std::string pad = (indent > 0) ? std::string(level * indent, ' ') : "";
            std::string pad_inner = (indent > 0) ? std::string((level + 1) * indent, ' ') : "";
            std::string nl = (indent > 0) ? "\n" : "";
            std::string sp = (indent > 0) ? " " : "";

            if (val.isNone()) return "null";

            // 1. 处理所有底层数值类型
            if (std::holds_alternative<double>(val.data)) {
                double d = std::get<double>(val.data);
                double rounded = std::round(d);
                // 整数消除浮点尾巴
                if (jc::Tol::isEq(d, rounded, 1e5) && std::abs(rounded) < 1e15 && rounded == std::trunc(rounded)) {
                    return std::to_string(static_cast<int64_t>(rounded));
                }
                std::ostringstream oss;
                oss << d;
                return oss.str();
            }
            if (std::holds_alternative<jc::BigInt>(val.data)) {
                return std::get<jc::BigInt>(val.data).toString();
            }
            if (std::holds_alternative<jc::Fraction>(val.data)) {
                std::ostringstream oss;
                oss << std::get<jc::Fraction>(val.data).toDouble();
                return oss.str();
            }

            // 2. 字符串深层转义
            if (std::holds_alternative<std::string>(val.data)) {
                std::string s = std::get<std::string>(val.data);
                std::string escaped = "\"";
                for (unsigned char c : s) {
                    switch (c) {
                    case '"':  escaped += "\\\""; break;
                    case '\\': escaped += "\\\\"; break;
                    case '\b': escaped += "\\b";  break;
                    case '\f': escaped += "\\f";  break;
                    case '\n': escaped += "\\n";  break;
                    case '\r': escaped += "\\r";  break;
                    case '\t': escaped += "\\t";  break;
                    default:
                        if (c <= 0x1F) {
                            char buf[16];
                            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                            escaped += buf;
                        }
                        else {
                            escaped += c;
                        }
                    }
                }
                escaped += "\"";
                return escaped;
            }

            // 3. 递归容器
            if (std::holds_alternative<jc::List>(val.data)) {
                const auto& L = std::get<jc::List>(val.data);
                const auto& raw = L.raw();
                if (raw.empty()) return "[]";

                std::string r = "[" + nl;
                for (size_t i = 0; i < raw.size(); ++i) {
                    if (i > 0) r += "," + nl;
                    r += pad_inner + encode(std::any_cast<jc::Value>(raw[i]), indent, level + 1);
                }
                r += nl + pad + "]";
                return r;
            }
            if (std::holds_alternative<jc::Dict>(val.data)) {
                const auto& d = std::get<jc::Dict>(val.data);
                const auto entries = d.getEntries(); // 按值临时抽取
                if (entries.empty()) return "{}";

                std::string r = "{" + nl;
                bool first = true;
                for (const auto& kv : entries) {
                    if (!first) r += "," + nl;
                    r += pad_inner + "\"" + kv.first + "\":" + sp;
                    try {
                        r += encode(std::any_cast<jc::Value>(kv.second), indent, level + 1);
                    }
                    catch (...) {
                        r += "null";
                    }
                    first = false;
                }
                r += nl + pad + "}";
                return r;
            }
            if (std::holds_alternative<jc::RealMatrix>(val.data)) {
                const auto& m = std::get<jc::RealMatrix>(val.data);
                std::string r = "[";
                for (int i = 0; i < m.getRows(); ++i) {
                    if (i > 0) r += ", ";
                    if (m.getRows() > 1) r += "[";
                    for (int j = 0; j < m.getCols(); ++j) {
                        if (j > 0) r += ", ";
                        r += encode(jc::Value(m(i, j)), 0, 0); // 内部元素保持紧凑
                    }
                    if (m.getRows() > 1) r += "]";
                }
                return r + "]";
            }

            return "\"<unserializable_type>\"";
        }
    };


    // ═══ 状态隔离的反序列化分析器 ═══
    struct JsonParser {
        const std::string& s;
        size_t pos;

        JsonParser(const std::string& str) : s(str), pos(0) {}

        void skipWS() {
            while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) pos++;
        }

        std::string parseString() {
            pos++; // 越过开启的 "
            std::string result;
            while (pos < s.size() && s[pos] != '"') {
                if (s[pos] == '\\') {
                    pos++;
                    if (pos >= s.size()) throw std::runtime_error("JSON Parse Error: Unexpected end inside string.");
                    char esc = s[pos];
                    switch (esc) {
                    case '"':  result += '"'; break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/'; break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'u':
                        pos += 4; // 只跳过Unicode，不做转码防崩溃
                        break;
                    default:
                        result += '\\'; result += esc; break;
                    }
                }
                else {
                    result += s[pos];
                }
                pos++;
            }
            if (pos >= s.size()) throw std::runtime_error("JSON Parse Error: Unterminated string.");
            pos++; // 吃掉闭合的 "
            return result;
        }

        jc::Value parseNumber() {
            size_t start = pos;
            if (pos < s.size() && s[pos] == '-') pos++;
            while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) pos++;
            bool isFloat = false;

            if (pos < s.size() && s[pos] == '.') {
                isFloat = true; pos++;
                while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) pos++;
            }
            if (pos < s.size() && (s[pos] == 'e' || s[pos] == 'E')) {
                isFloat = true; pos++;
                if (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) pos++;
                while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) pos++;
            }

            std::string numStr = s.substr(start, pos - start);
            if (numStr == "-" || numStr.empty()) throw std::runtime_error("JSON Parse Error: Invalid number structure.");

            if (isFloat) return jc::Value(std::stod(numStr));

            try { return jc::Value(jc::BigInt(numStr)); }
            catch (...) { return jc::Value(std::stod(numStr)); }
        }

        jc::Value parseArray() {
            pos++; // skip '['
            std::vector<std::any> elements;
            skipWS();
            if (pos < s.size() && s[pos] == ']') {
                pos++;
                return jc::Value(jc::List());
            }

            while (true) {
                elements.push_back(std::make_any<jc::Value>(parseValue()));
                skipWS();
                if (pos < s.size() && s[pos] == ',') { pos++; skipWS(); }
                else break;
            }
            if (pos >= s.size() || s[pos] != ']') throw std::runtime_error("JSON Parse Error: Expected ']' array closer.");
            pos++;

            jc::List L;
            for (auto& e : elements) L.push_back(std::move(e));
            return jc::Value(L);
        }

        jc::Value parseObject() {
            pos++; // skip '{'
            jc::Dict d;
            skipWS();
            if (pos < s.size() && s[pos] == '}') { pos++; return jc::Value(d); }

            while (true) {
                skipWS();
                if (pos >= s.size() || s[pos] != '"') throw std::runtime_error("JSON Parse Error: Object keys must be strings.");
                std::string key = parseString();
                skipWS();
                if (pos >= s.size() || s[pos] != ':') throw std::runtime_error("JSON Parse Error: Expected ':' separator.");
                pos++; skipWS();

                d.set(key, std::make_any<jc::Value>(parseValue()));
                skipWS();

                if (pos < s.size() && s[pos] == ',') { pos++; }
                else break;
            }
            if (pos >= s.size() || s[pos] != '}') throw std::runtime_error("JSON Parse Error: Expected '}' object closer.");
            pos++;
            return jc::Value(d);
        }

        jc::Value parseValue() {
            skipWS();
            if (pos >= s.size()) throw std::runtime_error("JSON Parse Error: Unexpected end of input.");
            char c = s[pos];
            if (c == '"') return parseString();
            if (c == '[') return parseArray();
            if (c == '{') return parseObject();

            if (s.compare(pos, 4, "true") == 0) { pos += 4; return jc::Value(1.0); }
            if (s.compare(pos, 5, "false") == 0) { pos += 5; return jc::Value(0.0); }
            if (s.compare(pos, 4, "null") == 0) { pos += 4; return jc::Value::none(); }

            if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parseNumber();

            throw std::runtime_error(std::string("JSON Parse Error: Unrecognized token at position ") + std::to_string(pos));
        }
    };


    // ═══ 注册模块路由表 ═══
    JC2_MODULE(json) {
        jc::ModuleReg R(env, builtins, arity);

        // serialize 函数挂载
        R.reg("json_encode", { 1 }, [](const std::vector<jc::Value>& args) -> jc::Value {
            return jc::Value(JsonEngine::encode(args[0], 0, 0));
            });
        R.reg("stringify", { 1 }, [](const std::vector<jc::Value>& args) -> jc::Value {
            return jc::Value(JsonEngine::encode(args[0], 0, 0));
            });

        // 格式化输出函数 (默认使用 4 个缩进空格)
        R.reg("json_pretty", { 1, 2 }, [](const std::vector<jc::Value>& args) -> jc::Value {
            int indent = (args.size() == 2) ? static_cast<int>(std::round(args[1].asDouble())) : 4;
            return jc::Value(JsonEngine::encode(args[0], indent, 0));
            });

        // deserialize 函数挂载
        R.reg("json_decode", { 1 }, [](const std::vector<jc::Value>& args) -> jc::Value {
            if (!std::holds_alternative<std::string>(args[0].data)) {
                throw std::runtime_error("Type Error: json_decode() expects a string.");
            }
            JsonParser parser(std::get<std::string>(args[0].data));
            return parser.parseValue();
            });
        R.reg("parse", { 1 }, [](const std::vector<jc::Value>& args) -> jc::Value {
            if (!std::holds_alternative<std::string>(args[0].data)) {
                throw std::runtime_error("Type Error: parse() expects a string.");
            }
            JsonParser parser(std::get<std::string>(args[0].data));
            return parser.parseValue();
            });
    }

} // namespace jc
#endif // JC2_MODULE_JSON_H
