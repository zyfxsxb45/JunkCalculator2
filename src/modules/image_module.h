#ifndef JC2_MODULE_IMAGE_H
#define JC2_MODULE_IMAGE_H

#include "Module.h"
#include "../vm/BuiltinRegistry.h"
#include "../vm/VM.h"
#include "Image.h"
#include <fstream>
#include <sstream>

namespace jc_image {
    using namespace jc;

    inline std::shared_ptr<Image>& getImg(const Value& v) {
        if (!v.isInstance())
            throw std::runtime_error("Type Error: Expected an Image.");
        auto inst = v.asInstance();
        if (!inst->nativeData.has_value())
            throw std::runtime_error("Type Error: Expected an Image.");
        return std::any_cast<std::shared_ptr<Image>&>(inst->nativeData);
    }

    inline Color parseColor(const Value& v) {
        if (v.isString()) return Color::parse(v.asString());
        throw std::runtime_error("Type Error: Expected a color string.");
    }

    inline ObjClass* imageClass = nullptr;

    inline Value makeImage(std::shared_ptr<Image> img) {
        auto inst = GcHeap::get().allocate<ObjInstance>();
        inst->classDef = imageClass;
        inst->nativeData = std::move(img);
        return Value(inst);
    }
}

JC2_MODULE(image) {
    using namespace jc_image;
    jc::ModuleReg R(env, builtins, arity);

    imageClass = GcHeap::get().allocate<jc::ObjClass>();
    jc::Value imgClassVal(imageClass);

    imageClass->name = "Image";
    R.set("Image", imgClassVal);

    auto addImgMethod = [&](const std::string& name, int maxArgs, jc::NativeCallable fn) {
        std::vector<std::string> pNames(maxArgs, "_");
        std::vector<bool> pRefs(maxArgs, false);
        auto fc = GcHeap::get().allocate<jc::ObjClosure>(pNames, pRefs, name, nullptr);
        fc->defaultValues.resize(maxArgs, jc::Value::none()); // 允许参数缺省
        fc->nativeFn = jc::VM::makeNativeFn(fn);
        imageClass->methods[name] = fc;
        };

    addImgMethod("width", 0, [](const std::vector<jc::Value>&) -> jc::Value { return jc::Value(static_cast<double>(getImg(jc::helpers::getGlobalCallback("self"))->width())); });
    addImgMethod("height", 0, [](const std::vector<jc::Value>&) -> jc::Value { return jc::Value(static_cast<double>(getImg(jc::helpers::getGlobalCallback("self"))->height())); });

    addImgMethod("setPixel", 3, [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->setPixel(static_cast<int>(args[0].asDouble()), static_cast<int>(args[1].asDouble()), parseColor(args[2]));
        return selfVal;
        });

    addImgMethod("getPixel", 2, [](const std::vector<jc::Value>& args) -> jc::Value {
        return jc::Value(getImg(jc::helpers::getGlobalCallback("self"))->getPixel(static_cast<int>(args[0].asDouble()), static_cast<int>(args[1].asDouble())).toHex());
        });

    addImgMethod("clear", 1, [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->clear(parseColor(args[0]));
        return selfVal;
        });

    // ★ 支持浮点精度的坐标以及浮点线宽！
    addImgMethod("line", 6, [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->line(args[0].asDouble(), args[1].asDouble(), args[2].asDouble(), args[3].asDouble(), parseColor(args[4]), (args.size() == 6 && !args[5].isNone()) ? args[5].asDouble() : 1.0);
        return selfVal;
        });

    addImgMethod("rect", 6, [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->rect(static_cast<int>(args[0].asDouble()), static_cast<int>(args[1].asDouble()), static_cast<int>(args[2].asDouble()), static_cast<int>(args[3].asDouble()), parseColor(args[4]), (args.size() == 6 && !args[5].isNone()) ? args[5].asDouble() : 1.0);
        return selfVal;
        });

    addImgMethod("fillRect", 5, [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->fillRect(static_cast<int>(args[0].asDouble()), static_cast<int>(args[1].asDouble()), static_cast<int>(args[2].asDouble()), static_cast<int>(args[3].asDouble()), parseColor(args[4]));
        return selfVal;
        });

    addImgMethod("circle", 5, [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->circle(args[0].asDouble(), args[1].asDouble(), args[2].asDouble(), parseColor(args[3]), (args.size() == 5 && !args[4].isNone()) ? args[4].asDouble() : 1.0);
        return selfVal;
        });

    addImgMethod("fillCircle", 4, [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->fillCircle(args[0].asDouble(), args[1].asDouble(), args[2].asDouble(), parseColor(args[3]));
        return selfVal;
        });

    // ★ 新增的 text 接口调用！
    addImgMethod("text", 5, [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        std::string txt;
        if (args[0].isString()) txt = args[0].asString();
        else { std::ostringstream oss; oss << args[0]; txt = oss.str(); } // 自动兼容数字转换打印

        int x = static_cast<int>(args[1].asDouble());
        int y = static_cast<int>(args[2].asDouble());
        jc::Color c = parseColor(args[3]);
        int scale = (args.size() == 5 && !args[4].isNone()) ? static_cast<int>(args[4].asDouble()) : 1;
        getImg(selfVal)->drawText(txt, x, y, c, scale);
        return selfVal;
        });

    addImgMethod("axes", 5, [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->drawAxes(args[0].asDouble(), args[1].asDouble(), args[2].asDouble(), args[3].asDouble(), (args.size() == 5 && !args[4].isNone()) ? parseColor(args[4]) : jc::Color{ 100, 100, 100 });
        return selfVal;
        });

    addImgMethod("save", 1, [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        auto im = getImg(selfVal);
        std::string path = args[0].asString();
        if (!im->saveBMP(path)) throw std::runtime_error("IO Error: Failed to save image.");
        return selfVal;
        });

    R.reg("img", { 2, 3 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        return makeImage(std::make_shared<jc::Image>(
            static_cast<int>(args[0].asDouble()), static_cast<int>(args[1].asDouble()),
            args.size() == 3 ? parseColor(args[2]) : jc::Color{ 255, 255, 255 }));
        });
}
#endif
