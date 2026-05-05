#ifndef JC2_MODULE_SOCKET_H
#define JC2_MODULE_SOCKET_H

// =========================================================================
// [环境防御结界] 必须彻底屏蔽旧版 Windows 宏和旧版 Socket！
// =========================================================================
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET NativeSocket;

// ★★★ 核心杀毒区：直接干碎 Windows 头文件带来的全局宏污染！ ★★★
#undef IN
#undef OUT
#undef DELETE
#undef ERROR
#undef CONST
#undef VOID
#undef IGNORE
#undef STRICT
#undef THIS
#undef PASCAL
#undef FAR
#undef NEAR
#else
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
typedef int NativeSocket;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

// =========================================================================
// 现在拥有了极其纯净的命名空间，可以安全引入 JC2 的内部结构了
// =========================================================================
#include "../Module.h"
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <cmath>

namespace jc {

    inline void initNetwork() {
        static bool initialized = false;
        if (!initialized) {
#ifdef _WIN32
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
                throw std::runtime_error("Network Error: WSAStartup failed.");
            }
#endif
            initialized = true;
        }
    }

    struct SocketWrapper {
        NativeSocket sock;
        SocketWrapper(NativeSocket s) : sock(s) {}
        ~SocketWrapper() { if (sock != INVALID_SOCKET) closesocket(sock); }
    };

    inline std::shared_ptr<ClassDefinition> getSocketClass() {
        static auto cls = std::make_shared<ClassDefinition>();
        cls->name = "NativeSocket";
        return cls;
    }

    inline std::shared_ptr<SocketWrapper> getSock(const Value& v, const std::string& fn) {
        if (std::holds_alternative<std::shared_ptr<Instance>>(v.data)) {
            auto inst = std::get<std::shared_ptr<Instance>>(v.data);
            if (inst->nativeData.has_value() && inst->nativeData.type() == typeid(std::shared_ptr<SocketWrapper>)) {
                return std::any_cast<std::shared_ptr<SocketWrapper>>(inst->nativeData);
            }
        }
        throw std::runtime_error("Type Error: " + fn + " expects a valid Network Socket.");
    }

    JC2_MODULE(socket) {
        initNetwork();
        jc::ModuleReg R(env, builtins, arity);

        R.reg("net_tcp_connect", { 2 }, [](const std::vector<Value>& args) -> Value {
            std::string host = std::get<std::string>(args[0].data);
            std::string port = std::to_string(static_cast<int>(std::round(args[1].asDouble())));

            struct addrinfo hints = { 0 }, * res = nullptr;
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;

            if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0) {
                throw std::runtime_error("Network Error: Could not resolve host '" + host + "'.");
            }

            NativeSocket s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            if (s == INVALID_SOCKET) {
                freeaddrinfo(res);
                throw std::runtime_error("Network Error: Failed to create socket.");
            }

            if (connect(s, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
                closesocket(s);
                freeaddrinfo(res);
                throw std::runtime_error("Network Error: Connection refused to " + host + ":" + port);
            }
            freeaddrinfo(res);

            auto inst = std::make_shared<Instance>();
            inst->classDef = getSocketClass();
            inst->nativeData = std::make_any<std::shared_ptr<SocketWrapper>>(std::make_shared<SocketWrapper>(s));
            return Value(inst);
            });

        R.reg("net_send", { 2 }, [](const std::vector<Value>& args) -> Value {
            auto wrapper = getSock(args[0], "net_send");
            std::string data = std::get<std::string>(args[1].data);
            if (send(wrapper->sock, data.c_str(), (int)data.size(), 0) == SOCKET_ERROR) {
                throw std::runtime_error("Network Error: Connection lost during send.");
            }
            return Value(static_cast<double>(data.size()));
            });

        R.reg("net_recv", { 2 }, [](const std::vector<Value>& args) -> Value {
            auto wrapper = getSock(args[0], "net_recv");
            int max_bytes = static_cast<int>(args[1].asDouble());
            if (max_bytes <= 0) max_bytes = 4096;

            std::vector<char> buffer(max_bytes);
            int bytes_read = recv(wrapper->sock, buffer.data(), max_bytes, 0);

            if (bytes_read < 0) throw std::runtime_error("Network Error: Failed to receive data.");
            if (bytes_read == 0) return Value("");

            return Value(std::string(buffer.data(), bytes_read));
            });

        R.reg("net_close", { 1 }, [](const std::vector<Value>& args) -> Value {
            auto wrapper = getSock(args[0], "net_close");
            if (wrapper->sock != INVALID_SOCKET) {
                closesocket(wrapper->sock);
                wrapper->sock = INVALID_SOCKET;
            }
            return Value::none();
            });

        // ==========================================================
        // 接待前台: 监听本机端口 (Server Bind & Listen)
        // ==========================================================
        R.reg("net_tcp_server", { 2 }, [](const std::vector<Value>& args) -> Value {
            std::string host = std::get<std::string>(args[0].data);
            std::string port = std::to_string(static_cast<int>(std::round(args[1].asDouble())));

            struct addrinfo hints = { 0 }, * res = nullptr;
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_PASSIVE; // ★ 关键：告诉系统这是用来当 Server 的

            const char* host_ptr = (host == "0.0.0.0" || host == "") ? nullptr : host.c_str();

            if (getaddrinfo(host_ptr, port.c_str(), &hints, &res) != 0) {
                throw std::runtime_error("Network Error: Could not resolve bind address.");
            }

            NativeSocket s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
            if (s == INVALID_SOCKET) {
                freeaddrinfo(res);
                throw std::runtime_error("Network Error: Failed to create server socket.");
            }

            // 开启端口复用，防止重启脚本当场报 "Bind failed"
            int opt = 1;
#ifdef _WIN32
            setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
            setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

            if (bind(s, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
                closesocket(s);
                freeaddrinfo(res);
                throw std::runtime_error("Network Error: Bind failed on port " + port);
            }
            freeaddrinfo(res);

            if (listen(s, SOMAXCONN) == SOCKET_ERROR) {
                closesocket(s);
                throw std::runtime_error("Network Error: Listen failed.");
            }

            auto inst = std::make_shared<Instance>();
            inst->classDef = getSocketClass();
            inst->nativeData = std::make_any<std::shared_ptr<SocketWrapper>>(std::make_shared<SocketWrapper>(s));
            return Value(inst);
            });

        // ==========================================================
        // 迎接客人: 阻塞并接收浏览器连接 (Server Accept)
        // ==========================================================
        R.reg("net_tcp_accept", { 1 }, [](const std::vector<Value>& args) -> Value {
            auto server_wrapper = getSock(args[0], "net_tcp_accept");

            // accept 阻塞直到有新连接接入 (无视客户端IP以便保持绝对的跨平台跨头文件安全)
            NativeSocket client_sock = accept(server_wrapper->sock, nullptr, nullptr);

            if (client_sock == INVALID_SOCKET) {
                throw std::runtime_error("Network Error: Accept failed.");
            }

            auto inst = std::make_shared<Instance>();
            inst->classDef = getSocketClass();
            inst->nativeData = std::make_any<std::shared_ptr<SocketWrapper>>(std::make_shared<SocketWrapper>(client_sock));
            return Value(inst);
            });
    }
}
#endif // JC2_MODULE_SOCKET_H
