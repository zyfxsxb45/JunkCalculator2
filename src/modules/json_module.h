#ifndef JC2_MODULE_JSON_H
#define JC2_MODULE_JSON_H

#include "Module.h"
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
            if (val.isBool()) return val.asBool() ? "true" : "false";

            // 1. 处理所有底层数值类型
            if (val.isDouble()) {
                double d = val.asDoubleRaw();
                double rounded = std::round(d);
                // 整数消除浮点尾巴
                if (jc::Tol::isEq(d, rounded, 1e5) && std::abs(rounded) < 1e15 && rounded == std::trunc(rounded)) {
                    return std::to_string(static_cast<int64_t>(rounded));
                }
                std::ostringstream oss;
                oss << d;
                return oss.str();
            }
            if (val.isInt32()) {
                return std::to_string(val.asInt32());
            }
            if (val.isObjType(ObjType::BIGINT)) {
                return static_cast<ObjBigInt*>(val.asObj())->num.toString();
            }
            if (val.isObjType(ObjType::FRACTION)) {
                std::ostringstream oss;
                oss << static_cast<ObjFraction*>(val.asObj())->frac.toDouble();
                return oss.str();
            }

            // 2. 字符串深层转义
            if (val.isString()) {
                std::string s = val.asString();
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
            if (val.isObjType(ObjType::LIST)) {
                const auto& raw = static_cast<ObjList*>(val.asObj())->vec;
                if (raw.empty()) return "[]";

                std::string r = "[" + nl;
                for (size_t i = 0; i < raw.size(); ++i) {
                    if (i > 0) r += "," + nl;
                    r += pad_inner + encode(raw[i], indent, level + 1);
                }
                r += nl + pad + "]";
                return r;
            }
            if (val.isObjType(ObjType::DICT)) {
                const auto& entries = static_cast<ObjDict*>(val.asObj())->elements;
                if (entries.empty()) return "{}";

                std::string r = "{" + nl;
                bool first = true;
                for (const auto& kv : entries) {
                    if (!first) r += "," + nl;
                    r += pad_inner + "\"" + kv.first.toString() + "\":" + sp;
                    try {
                        r += encode(kv.second, indent, level + 1);
                    }
                    catch (...) {
                        r += "null";
                    }
                    first = false;
                }
                r += nl + pad + "}";
                return r;
            }
            if (val.isObjType(ObjType::REAL_MATRIX)) {
                const auto& m = static_cast<ObjRealMatrix*>(val.asObj())->mat;
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
                    case 'u': {
                        std::string hexStr;
                        for (int i = 0; i < 4; ++i) {
                            pos++;
                            if (pos >= s.size()) throw std::runtime_error("JSON Parse Error: Unexpected end inside string.");
                            hexStr += s[pos];
                        }
                        try {
                            int cp = std::stoi(hexStr, nullptr, 16);
                            if (cp <= 0x7F) {
                                result += static_cast<char>(cp);
                            } else if (cp <= 0x7FF) {
                                result += static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
                                result += static_cast<char>(0x80 | (cp & 0x3F));
                            } else {
                                result += static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
                                result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                                result += static_cast<char>(0x80 | (cp & 0x3F));
                            }
                        } catch (...) {
                            result += "\\u" + hexStr; // 解析失败则原样保留
                        }
                        break;
                    }
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

            if (isFloat) {
                try { return jc::Value(std::stod(numStr)); }
                catch (...) { throw std::runtime_error("JSON Parse Error: Float out of range."); }
            }

            try { return jc::Value(jc::BigInt(numStr)); }
            catch (...) { 
                try { return jc::Value(std::stod(numStr)); }
                catch (...) { throw std::runtime_error("JSON Parse Error: Number out of range."); }
            }
        }

        jc::Value parseArray() {
            pos++; // skip '['
            std::vector<jc::Value> elements;
            skipWS();
            if (pos < s.size() && s[pos] == ']') {
                pos++;
                return jc::Value(GcHeap::get().allocate<ObjList>());
            }

            while (true) {
                elements.push_back(parseValue());
                skipWS();
                if (pos < s.size() && s[pos] == ',') { 
                    pos++; 
                    skipWS(); 
                    if (pos < s.size() && s[pos] == ']') break; // 允许尾随逗号
                }
                else break;
            }
            if (pos >= s.size() || s[pos] != ']') throw std::runtime_error("JSON Parse Error: Expected ']' array closer.");
            pos++;

            ObjList* L = GcHeap::get().allocate<ObjList>();
            L->vec = std::move(elements);
            return jc::Value(L);
        }

        jc::Value parseObject() {
            pos++; // skip '{'
            ObjDict* d = GcHeap::get().allocate<ObjDict>();
            skipWS();
            if (pos < s.size() && s[pos] == '}') { pos++; return jc::Value(d); }

            while (true) {
                skipWS();
                if (pos >= s.size() || s[pos] != '"') throw std::runtime_error("JSON Parse Error: Object keys must be strings.");
                std::string key = parseString();
                skipWS();
                if (pos >= s.size() || s[pos] != ':') throw std::runtime_error("JSON Parse Error: Expected ':' separator.");
                pos++; skipWS();

                jc::Value v = parseValue();
                d->keyMap[jc::Value(key)] = d->elements.size();
                d->elements.push_back({jc::Value(key), v});
                skipWS();

                if (pos < s.size() && s[pos] == ',') { 
                    pos++; 
                    skipWS();
                    if (pos < s.size() && s[pos] == '}') break; // 允许尾随逗号
                }
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

            if (s.compare(pos, 4, "true") == 0) { pos += 4; return jc::Value(true); }
            if (s.compare(pos, 5, "false") == 0) { pos += 5; return jc::Value(false); }
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
            if (!args[0].isString()) {
                throw std::runtime_error("Type Error: json_decode() expects a string.");
            }
            JsonParser parser(args[0].asString());
            return parser.parseValue();
            });
        R.reg("parse", { 1 }, [](const std::vector<jc::Value>& args) -> jc::Value {
            if (!args[0].isString()) {
                throw std::runtime_error("Type Error: parse() expects a string.");
            }
            JsonParser parser(args[0].asString());
            return parser.parseValue();
            });
    }

} // namespace jc
#endif // JC2_MODULE_JSON_H
