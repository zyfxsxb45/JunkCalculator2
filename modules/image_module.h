#ifndef JC2_MODULE_IMAGE_H
#define JC2_MODULE_IMAGE_H

#include "../Module.h"
#include "../Image.h"
#include <fstream>   // 追加这行：用于读取文件
#include <sstream>   // 追加这行：用于转换二进制流

namespace jc_image {
    using namespace jc;

    // ★ 从 Instance 中提取原生 Image 指针
    inline std::shared_ptr<Image>& getImg(const Value& v) {
        if (!std::holds_alternative<std::shared_ptr<Instance>>(v.data))
            throw std::runtime_error("Type Error: Expected an Image.");
        auto inst = std::get<std::shared_ptr<Instance>>(v.data);
        if (!inst->nativeData.has_value())
            throw std::runtime_error("Type Error: Expected an Image.");
        return std::any_cast<std::shared_ptr<Image>&>(inst->nativeData);
    }

    inline Color parseColor(const Value& v) {
        if (std::holds_alternative<std::string>(v.data))
            return Color::parse(std::get<std::string>(v.data));
        throw std::runtime_error("Type Error: Expected a color string.");
    }

    // ★ 全局类定义（模块加载时创建）
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

    R.reg("img", { 2, 3 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        int w = static_cast<int>(std::round(args[0].asDouble()));
        int h = static_cast<int>(std::round(args[1].asDouble()));
        jc::Color bg = { 255, 255, 255 };
        if (args.size() == 3) bg = parseColor(args[2]);
        return makeImage(std::make_shared<jc::Image>(w, h, bg));
        });

    R.reg("imgWidth", { 1 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        return jc::Value(static_cast<double>(getImg(args[0])->width()));
        });

    R.reg("imgHeight", { 1 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        return jc::Value(static_cast<double>(getImg(args[0])->height()));
        });

    R.reg("imgPixel", { 4 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        auto im = getImg(args[0]);
        im->setPixel(static_cast<int>(std::round(args[1].asDouble())),
            static_cast<int>(std::round(args[2].asDouble())),
            parseColor(args[3]));
        return args[0];
        });

    R.reg("imgClear", { 2 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        getImg(args[0])->clear(parseColor(args[1]));
        return args[0];
        });

    R.reg("imgLine", { 6, 7 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        auto im = getImg(args[0]);
        int x0 = static_cast<int>(std::round(args[1].asDouble()));
        int y0 = static_cast<int>(std::round(args[2].asDouble()));
        int x1 = static_cast<int>(std::round(args[3].asDouble()));
        int y1 = static_cast<int>(std::round(args[4].asDouble()));
        jc::Color c = parseColor(args[5]);
        int thick = (args.size() == 7) ? static_cast<int>(std::round(args[6].asDouble())) : 1;
        im->line(x0, y0, x1, y1, c, thick);
        return args[0];
        });

    R.reg("imgRect", { 6, 7 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        auto im = getImg(args[0]);
        int x = static_cast<int>(std::round(args[1].asDouble()));
        int y = static_cast<int>(std::round(args[2].asDouble()));
        int rw = static_cast<int>(std::round(args[3].asDouble()));
        int rh = static_cast<int>(std::round(args[4].asDouble()));
        jc::Color c = parseColor(args[5]);
        int thick = (args.size() == 7) ? static_cast<int>(std::round(args[6].asDouble())) : 1;
        im->rect(x, y, rw, rh, c, thick);
        return args[0];
        });

    R.reg("imgFillRect", { 6 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        auto im = getImg(args[0]);
        im->fillRect(static_cast<int>(std::round(args[1].asDouble())),
            static_cast<int>(std::round(args[2].asDouble())),
            static_cast<int>(std::round(args[3].asDouble())),
            static_cast<int>(std::round(args[4].asDouble())),
            parseColor(args[5]));
        return args[0];
        });

    R.reg("imgCircle", { 5, 6 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        auto im = getImg(args[0]);
        int cx = static_cast<int>(std::round(args[1].asDouble()));
        int cy = static_cast<int>(std::round(args[2].asDouble()));
        int r = static_cast<int>(std::round(args[3].asDouble()));
        jc::Color c = parseColor(args[4]);
        int thick = (args.size() == 6) ? static_cast<int>(std::round(args[5].asDouble())) : 1;
        im->circle(cx, cy, r, c, thick);
        return args[0];
        });

    R.reg("imgFillCircle", { 5 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        auto im = getImg(args[0]);
        im->fillCircle(static_cast<int>(std::round(args[1].asDouble())),
            static_cast<int>(std::round(args[2].asDouble())),
            static_cast<int>(std::round(args[3].asDouble())),
            parseColor(args[4]));
        return args[0];
        });

    R.reg("imgAxes", { 5, 6 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        auto im = getImg(args[0]);
        double xMin = args[1].asDouble(), xMax = args[2].asDouble();
        double yMin = args[3].asDouble(), yMax = args[4].asDouble();
        jc::Color c = (args.size() == 6) ? parseColor(args[5]) : jc::Color{ 100, 100, 100 };
        im->drawAxes(xMin, xMax, yMin, yMax, c);
        return args[0];
        });

    R.reg("imgScatter", { 7, 8 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        auto im = getImg(args[0]);
        if (!std::holds_alternative<jc::RealMatrix>(args[1].data) ||
            !std::holds_alternative<jc::RealMatrix>(args[2].data))
            throw std::runtime_error("Type Error: imgScatter() expects arrays for x and y.");
        auto xData = std::get<jc::RealMatrix>(args[1].data).rawData();
        auto yData = std::get<jc::RealMatrix>(args[2].data).rawData();
        if (xData.size() != yData.size())
            throw std::runtime_error("Math Error: x and y must have same length.");
        double xMin = args[3].asDouble(), xMax = args[4].asDouble();
        double yMin = args[5].asDouble(), yMax = args[6].asDouble();
        jc::Color c = (args.size() == 8) ? parseColor(args[7]) : jc::Color{ 255, 0, 0 };
        for (size_t i = 0; i < xData.size(); ++i) {
            int sx = im->mapPlotX(xData[i], xMin, xMax);
            int sy = im->mapPlotY(yData[i], yMin, yMax);
            im->fillCircle(sx, sy, 3, c);
        }
        return args[0];
        });

    R.reg("imgSave", { 2 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        auto im = getImg(args[0]);
        if (!std::holds_alternative<std::string>(args[1].data))
            throw std::runtime_error("Type Error: imgSave() expects a string path.");
        std::string path = std::get<std::string>(args[1].data);
        if (!im->saveBMP(path))
            throw std::runtime_error("IO Error: Failed to save image to '" + path + "'.");
        std::cout << "  Image saved: " << path << " (" << im->width() << "x" << im->height() << ")" << std::endl;
        return jc::Value::none();
        });

    R.reg("imgReadBytes", { 1 }, [](const std::vector<jc::Value>& args) -> jc::Value {
        if (!std::holds_alternative<std::string>(args[0].data))
            throw std::runtime_error("Type Error: imgReadBytes() expects a file path.");
        std::string path = std::get<std::string>(args[0].data);

        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("IO Error: Failed to read '" + path + "'.");

        std::ostringstream ss;
        ss << file.rdbuf(); // 将图片二进制数据无损吸入内存
        return jc::Value(ss.str()); // 塞回 JC2 虚拟机
        });
}
#endif
