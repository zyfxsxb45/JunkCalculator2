#ifndef JC2_MODULE_WINDOW_H
#define JC2_MODULE_WINDOW_H

#include "../Module.h"
#include "image_module.h"

#ifdef _WIN32
#include <windows.h>
#include <thread>
#include <mutex>
#include <atomic>
#endif

namespace jc_window {
    using namespace jc;

#ifdef _WIN32
    class NativeWindow {
    private:
        HWND hwnd = NULL;
        std::atomic<bool> running{ true };
        std::thread winThread;

        int width, height;
        std::vector<uint8_t> displayBuffer;
        std::mutex bufMutex;

        // 窗口的独立守护线程
        void threadFunc(std::string title) {
            WNDCLASS wc = { 0 };
            wc.lpfnWndProc = staticWndProc;
            wc.hInstance = GetModuleHandle(NULL);
            wc.lpszClassName = "JC2WindowMT";
            // 确保窗口类只被注册一次
            RegisterClass(&wc);
            RECT rect = { 0, 0, width, height };
            AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
            // ★ 修改点：直接使用类成员 this->hwnd，避免任何局部声明隐藏
            this->hwnd = CreateWindow("JC2WindowMT", title.c_str(),
                WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                rect.right - rect.left, rect.bottom - rect.top,
                NULL, NULL, GetModuleHandle(NULL), this);
            if (!this->hwnd) { running = false; return; }
            // 完美的 Windows 标准消息循环 (阻塞等待，极低 CPU 占用)
            MSG msg;
            while (GetMessage(&msg, NULL, 0, 0) > 0) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            running = false; // 窗口被关闭
        }

        // 静态分发器，将 Win32 C API 路由到 C++ 对象
        static LRESULT CALLBACK staticWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
            NativeWindow* win = NULL;
            if (msg == WM_NCCREATE) {
                CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
                win = (NativeWindow*)pCreate->lpCreateParams;
                SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)win);
            }
            else {
                win = (NativeWindow*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
            }
            if (win) return win->wndProc(hWnd, msg, wParam, lParam);
            return DefWindowProc(hWnd, msg, wParam, lParam);
        }

        // 真正的面向对象消息处理器
        LRESULT wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
            if (msg == WM_CLOSE) {
                PostQuitMessage(0); // 干净地终结独立线程的消息循环
                return 0;
            }
            if (msg == WM_PAINT) {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hWnd, &ps);

                // 线程安全：锁定缓冲区并高速刷入画面
                std::lock_guard<std::mutex> lock(bufMutex);
                if (!displayBuffer.empty()) {
                    BITMAPINFO bmi = { 0 };
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = width;
                    bmi.bmiHeader.biHeight = -height; // Top-Down 渲染
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 24;
                    bmi.bmiHeader.biCompression = BI_RGB;
                    SetDIBitsToDevice(hdc, 0, 0, width, height, 0, 0, 0, height,
                        displayBuffer.data(), &bmi, DIB_RGB_COLORS);
                }
                EndPaint(hWnd, &ps);
                return 0;
            }
            return DefWindowProc(hWnd, msg, wParam, lParam);
        }

    public:
        NativeWindow(const std::string& title, int w, int h) : width(w), height(h) {
            displayBuffer.resize(w * h * 3, 0); // 黑色初始化背景
            // 剥离 OS 线程
            winThread = std::thread(&NativeWindow::threadFunc, this, title);

            // 等待 OS 线程将窗口句柄初始化完毕
            while (running && hwnd == NULL) std::this_thread::yield();
        }

        ~NativeWindow() {
            if (running && hwnd) {
                // 安全释放：向子线程投递关闭消息
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
            if (winThread.joinable()) winThread.join();
        }

        bool isOpen() const { return running; }

        void show(const std::shared_ptr<Image>& img) {
            if (!running || !hwnd) return;
            const auto& src = img->getRawPixels();

            {
                // 上锁保证不和 WM_PAINT 绘制过程冲突
                std::lock_guard<std::mutex> lock(bufMutex);
                for (size_t i = 0; i < src.size(); i += 3) {
                    displayBuffer[i] = src[i + 2]; // B
                    displayBuffer[i + 1] = src[i + 1]; // G
                    displayBuffer[i + 2] = src[i];     // R
                }
            }
            // 核心魔法：异步通知 Win32 线程“可以开始下一帧重绘了”
            InvalidateRect(hwnd, NULL, FALSE);
        }
    };
#else
    class NativeWindow {
    public:
        NativeWindow(const std::string&, int, int) { throw std::runtime_error("Window module is strictly Win32 currently."); }
        bool isOpen() { return false; }
        void show(const std::shared_ptr<Image>&) {}
    };
#endif

    inline std::shared_ptr<ClassDefinition> windowClass;
}

JC2_MODULE(window) {
    using namespace jc_window;
    jc::ModuleReg R(env, builtins, arity);

    windowClass = std::make_shared<jc::ClassDefinition>();
    windowClass->name = "Window";
    // ★ 注册为唯一的类名，不再有命名冲突！
    R.set("Window", jc::Value(windowClass));

    auto addWinMethod = [&](const std::string& name, jc::NativeCallable fn) {
        auto fc = std::make_shared<FunctionClosure>(std::vector<std::string>{}, std::vector<bool>{}, name, nullptr);
        fc->nativeFn = std::make_any<NativeCallable>(fn);
        windowClass->methods[name] = std::move(fc);
        };

    addWinMethod("init", [](const std::vector<jc::Value>& args) -> jc::Value {
        if (args.size() != 3) {
            throw std::runtime_error("TypeError: Window() constructor takes exactly 3 arguments (title, width, height).");
        }
        std::string title = std::get<std::string>(args[0].data);
        int w = static_cast<int>(std::round(args[1].asDouble()));
        int h = static_cast<int>(std::round(args[2].asDouble()));

        // 获取 VM 自动分配的空 Instance (即 self)
        auto selfVal = jc::helpers::getGlobalCallback("self");
        auto inst = std::get<std::shared_ptr<Instance>>(selfVal.data);

        // 实例化 C++ 底层视窗，并注入 nativeData
        inst->nativeData = std::make_shared<NativeWindow>(title, w, h);

        return jc::Value::none(); // init 规范：无需返回值
        });

    addWinMethod("isOpen", [](const std::vector<jc::Value>&) -> jc::Value {
        auto inst = std::get<std::shared_ptr<Instance>>(jc::helpers::getGlobalCallback("self").data);
        auto win = std::any_cast<std::shared_ptr<NativeWindow>&>(inst->nativeData);
        return jc::Value(win->isOpen() ? 1.0 : 0.0);
        });

    addWinMethod("show", [](const std::vector<jc::Value>& args) -> jc::Value {
        auto inst = std::get<std::shared_ptr<Instance>>(jc::helpers::getGlobalCallback("self").data);
        auto win = std::any_cast<std::shared_ptr<NativeWindow>&>(inst->nativeData);
        auto im = jc_image::getImg(args[0]);
        win->show(im);
        return jc::Value::none();
        });
}
#endif
