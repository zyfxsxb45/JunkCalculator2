#ifndef JC2_MODULE_IMAGE_H
#define JC2_MODULE_IMAGE_H

#include "../Module.h"
#include "../Image.h"
#include <fstream>
#include <sstream>

namespace jc_image {
    using namespace jc;

    inline std::shared_ptr<Image>& getImg(const Value& v) {
        if (!std::holds_alternative<std::shared_ptr<Instance>>(v.data))
            throw std::runtime_error("Type Error: Expected an Image.");
        auto inst = std::get<std::shared_ptr<Instance>>(v.data);
        if (!inst->nativeData.has_value())
            throw std::runtime_error("Type Error: Expected an Image.");
        return std::any_cast<std::shared_ptr<Image>&>(inst->nativeData);
    }

    inline Color parseColor(const Value& v) {
        if (std::holds_alternative<std::string>(v.data)) return Color::parse(std::get<std::string>(v.data));
        throw std::runtime_error("Type Error: Expected a color string.");
    }

    inline std::shared_ptr<ClassDefinition> imageClass;

    inline Value makeImage(std::shared_ptr<Image> img) {
        auto inst = std::make_shared<Instance>();
        inst->classDef = imageClass;
        inst->nativeData = std::move(img);
        return Value(inst);
    }
}

JC2_MODULE(image) {
    using namespace jc_image;
    jc::ModuleReg R(env, builtins, arity);

    imageClass = std::make_shared<jc::ClassDefinition>();
    imageClass->name = "Image";
    R.set("Image", jc::Value(imageClass));

    auto addImgMethod = [&](const std::string& name, jc::NativeCallable fn) {
        auto fc = std::make_shared<jc::FunctionClosure>(std::vector<std::string>{}, std::vector<bool>{}, name, nullptr);
        fc->nativeFn = std::make_any<jc::NativeCallable>(fn);
        imageClass->methods[name] = std::move(fc);
        };

    addImgMethod("width", [](const std::vector<jc::Value>&) -> jc::Value { return jc::Value(static_cast<double>(getImg(jc::helpers::getGlobalCallback("self"))->width())); });
    addImgMethod("height", [](const std::vector<jc::Value>&) -> jc::Value { return jc::Value(static_cast<double>(getImg(jc::helpers::getGlobalCallback("self"))->height())); });

    addImgMethod("setPixel", [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->setPixel(static_cast<int>(args[0].asDouble()), static_cast<int>(args[1].asDouble()), parseColor(args[2]));
        return selfVal;
        });

    addImgMethod("getPixel", [](const std::vector<jc::Value>& args) -> jc::Value {
        return jc::Value(getImg(jc::helpers::getGlobalCallback("self"))->getPixel(static_cast<int>(args[0].asDouble()), static_cast<int>(args[1].asDouble())).toHex());
        });

    addImgMethod("clear", [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->clear(parseColor(args[0]));
        return selfVal;
        });

    // ★ 支持浮点精度的坐标以及浮点线宽！
    addImgMethod("line", [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->line(args[0].asDouble(), args[1].asDouble(), args[2].asDouble(), args[3].asDouble(), parseColor(args[4]), (args.size() == 6) ? args[5].asDouble() : 1.0);
        return selfVal;
        });

    addImgMethod("rect", [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->rect(static_cast<int>(args[0].asDouble()), static_cast<int>(args[1].asDouble()), static_cast<int>(args[2].asDouble()), static_cast<int>(args[3].asDouble()), parseColor(args[4]), (args.size() == 6) ? args[5].asDouble() : 1.0);
        return selfVal;
        });

    addImgMethod("fillRect", [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->fillRect(static_cast<int>(args[0].asDouble()), static_cast<int>(args[1].asDouble()), static_cast<int>(args[2].asDouble()), static_cast<int>(args[3].asDouble()), parseColor(args[4]));
        return selfVal;
        });

    addImgMethod("circle", [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->circle(args[0].asDouble(), args[1].asDouble(), args[2].asDouble(), parseColor(args[3]), (args.size() == 5) ? args[4].asDouble() : 1.0);
        return selfVal;
        });

    addImgMethod("fillCircle", [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->fillCircle(args[0].asDouble(), args[1].asDouble(), args[2].asDouble(), parseColor(args[3]));
        return selfVal;
        });

    // ★ 新增的 text 接口调用！
    addImgMethod("text", [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        std::string txt;
        if (std::holds_alternative<std::string>(args[0].data)) txt = std::get<std::string>(args[0].data);
        else { std::ostringstream oss; oss << args[0]; txt = oss.str(); } // 自动兼容数字转换打印

        int x = static_cast<int>(args[1].asDouble());
        int y = static_cast<int>(args[2].asDouble());
        jc::Color c = parseColor(args[3]);
        int scale = (args.size() == 5) ? static_cast<int>(args[4].asDouble()) : 1;
        getImg(selfVal)->drawText(txt, x, y, c, scale);
        return selfVal;
        });

    addImgMethod("axes", [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        getImg(selfVal)->drawAxes(args[0].asDouble(), args[1].asDouble(), args[2].asDouble(), args[3].asDouble(), (args.size() == 5) ? parseColor(args[4]) : jc::Color{ 100, 100, 100 });
        return selfVal;
        });

    addImgMethod("save", [](const std::vector<jc::Value>& args) -> jc::Value {
        jc::Value selfVal = jc::helpers::getGlobalCallback("self");
        auto im = getImg(selfVal);
        std::string path = std::get<std::string>(args[0].data);
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
