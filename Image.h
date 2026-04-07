#ifndef JC2_IMAGE_H
#define JC2_IMAGE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace jc {

    struct Color {
        uint8_t r, g, b;

        static Color fromHex(const std::string& hex) {
            std::string s = hex;
            if (!s.empty() && s[0] == '#') s = s.substr(1);
            if (s.size() != 6) return { 0, 0, 0 };
            auto h = [](char c) -> uint8_t {
                if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
                if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
                if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
                return 0;
                };
            return {
                static_cast<uint8_t>(h(s[0]) * 16 + h(s[1])),
                static_cast<uint8_t>(h(s[2]) * 16 + h(s[3])),
                static_cast<uint8_t>(h(s[4]) * 16 + h(s[5]))
            };
        }

        static Color fromName(const std::string& name) {
            static const std::unordered_map<std::string, Color> colors = {
                {"black",   {0,0,0}},       {"white",   {255,255,255}},
                {"red",     {255,0,0}},      {"green",   {0,128,0}},
                {"blue",    {0,0,255}},      {"yellow",  {255,255,0}},
                {"cyan",    {0,255,255}},    {"magenta", {255,0,255}},
                {"orange",  {255,165,0}},    {"purple",  {128,0,128}},
                {"pink",    {255,192,203}},  {"gray",    {128,128,128}},
                {"grey",    {128,128,128}},  {"brown",   {139,69,19}},
                {"lime",    {0,255,0}},      {"navy",    {0,0,128}},
                {"teal",    {0,128,128}},    {"maroon",  {128,0,0}},
                {"silver",  {192,192,192}},  {"olive",   {128,128,0}},
            };
            auto it = colors.find(name);
            if (it != colors.end()) return it->second;
            return fromHex(name);
        }

        static Color parse(const std::string& s) {
            if (!s.empty() && s[0] == '#') return fromHex(s);
            return fromName(s);
        }
    };

    class Image {
    private:
        int w, h;
        std::vector<uint8_t> pixels; // RGB, row-major

    public:
        Image() : w(0), h(0) {}
        Image(int width, int height, Color bg = { 255,255,255 })
            : w(width), h(height), pixels(width* height * 3) {
            if (w <= 0 || h <= 0) throw std::runtime_error("Image Error: Dimensions must be positive.");
            for (int i = 0; i < w * h; ++i) {
                pixels[i * 3] = bg.r;
                pixels[i * 3 + 1] = bg.g;
                pixels[i * 3 + 2] = bg.b;
            }
        }

        int width() const { return w; }
        int height() const { return h; }

        void setPixel(int x, int y, Color c) {
            if (x < 0 || x >= w || y < 0 || y >= h) return;
            int idx = (y * w + x) * 3;
            pixels[idx] = c.r; pixels[idx + 1] = c.g; pixels[idx + 2] = c.b;
        }

        Color getPixel(int x, int y) const {
            if (x < 0 || x >= w || y < 0 || y >= h) return { 0,0,0 };
            int idx = (y * w + x) * 3;
            return { pixels[idx], pixels[idx + 1], pixels[idx + 2] };
        }

        void clear(Color c) {
            for (int i = 0; i < w * h; ++i) {
                pixels[i * 3] = c.r; pixels[i * 3 + 1] = c.g; pixels[i * 3 + 2] = c.b;
            }
        }

        void line(int x0, int y0, int x1, int y1, Color c, int thickness = 1) {
            // Bresenham
            int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
            int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
            int err = dx - dy;
            while (true) {
                if (thickness <= 1) { setPixel(x0, y0, c); }
                else {
                    int r = thickness / 2;
                    for (int dy2 = -r; dy2 <= r; ++dy2)
                        for (int dx2 = -r; dx2 <= r; ++dx2)
                            if (dx2 * dx2 + dy2 * dy2 <= r * r)
                                setPixel(x0 + dx2, y0 + dy2, c);
                }
                if (x0 == x1 && y0 == y1) break;
                int e2 = 2 * err;
                if (e2 > -dy) { err -= dy; x0 += sx; }
                if (e2 < dx) { err += dx; y0 += sy; }
            }
        }

        void rect(int x0, int y0, int rw, int rh, Color c, int thickness = 1) {
            line(x0, y0, x0 + rw - 1, y0, c, thickness);
            line(x0 + rw - 1, y0, x0 + rw - 1, y0 + rh - 1, c, thickness);
            line(x0 + rw - 1, y0 + rh - 1, x0, y0 + rh - 1, c, thickness);
            line(x0, y0 + rh - 1, x0, y0, c, thickness);
        }

        void fillRect(int x0, int y0, int rw, int rh, Color c) {
            for (int y = y0; y < y0 + rh; ++y)
                for (int x = x0; x < x0 + rw; ++x)
                    setPixel(x, y, c);
        }

        void circle(int cx, int cy, int radius, Color c, int thickness = 1) {
            // Midpoint circle
            int x = radius, y = 0, err = 1 - radius;
            while (x >= y) {
                auto plot = [&](int px, int py) {
                    if (thickness <= 1) setPixel(px, py, c);
                    else {
                        int r = thickness / 2;
                        for (int dy = -r; dy <= r; ++dy)
                            for (int dx = -r; dx <= r; ++dx)
                                if (dx * dx + dy * dy <= r * r)
                                    setPixel(px + dx, py + dy, c);
                    }
                    };
                plot(cx + x, cy + y); plot(cx - x, cy + y);
                plot(cx + x, cy - y); plot(cx - x, cy - y);
                plot(cx + y, cy + x); plot(cx - y, cy + x);
                plot(cx + y, cy - x); plot(cx - y, cy - x);
                y++;
                if (err < 0) { err += 2 * y + 1; }
                else { x--; err += 2 * (y - x) + 1; }
            }
        }

        void fillCircle(int cx, int cy, int radius, Color c) {
            for (int y = -radius; y <= radius; ++y)
                for (int x = -radius; x <= radius; ++x)
                    if (x * x + y * y <= radius * radius)
                        setPixel(cx + x, cy + y, c);
        }

        void drawAxes(double xMin, double xMax, double yMin, double yMax, Color c,
            int marginL = 40, int marginR = 10, int marginT = 10, int marginB = 30) {
            int plotW = w - marginL - marginR;
            int plotH = h - marginT - marginB;

            // 坐标轴
            auto mapX = [&](double v) -> int { return marginL + static_cast<int>((v - xMin) / (xMax - xMin) * plotW); };
            auto mapY = [&](double v) -> int { return marginT + static_cast<int>((1.0 - (v - yMin) / (yMax - yMin)) * plotH); };

            // X 轴
            if (yMin <= 0 && yMax >= 0) {
                int y0 = mapY(0);
                line(marginL, y0, w - marginR, y0, c);
            }

            // Y 轴
            if (xMin <= 0 && xMax >= 0) {
                int x0 = mapX(0);
                line(x0, marginT, x0, h - marginB, c);
            }

            // 边框
            Color border = { 200, 200, 200 };
            rect(marginL, marginT, plotW, plotH, border);

            // 网格线（虚线效果）
            Color grid = { 230, 230, 230 };
            int numGridX = 10, numGridY = 8;
            for (int i = 1; i < numGridX; ++i) {
                int x = marginL + i * plotW / numGridX;
                for (int y = marginT; y < h - marginB; y += 3)
                    setPixel(x, y, grid);
            }
            for (int i = 1; i < numGridY; ++i) {
                int y = marginT + i * plotH / numGridY;
                for (int x = marginL; x < w - marginR; x += 3)
                    setPixel(x, y, grid);
            }
        }

        // 坐标映射辅助
        int mapPlotX(double v, double xMin, double xMax, int marginL = 40, int marginR = 10) const {
            int plotW = w - marginL - marginR;
            return marginL + static_cast<int>((v - xMin) / (xMax - xMin) * plotW);
        }
        int mapPlotY(double v, double yMin, double yMax, int marginT = 10, int marginB = 30) const {
            int plotH = h - marginT - marginB;
            return marginT + static_cast<int>((1.0 - (v - yMin) / (yMax - yMin)) * plotH);
        }

        bool saveBMP(const std::string& path) const {
            int rowBytes = w * 3;
            int rowPad = (4 - rowBytes % 4) % 4;
            int dataSize = (rowBytes + rowPad) * h;
            int fileSize = 54 + dataSize;

            std::ofstream f(path, std::ios::binary);
            if (!f) return false;

            // File header (14 bytes)
            auto writeLE16 = [&](uint16_t v) { f.write(reinterpret_cast<char*>(&v), 2); };
            auto writeLE32 = [&](uint32_t v) { f.write(reinterpret_cast<char*>(&v), 4); };

            f.write("BM", 2);
            writeLE32(static_cast<uint32_t>(fileSize));
            writeLE32(0); // reserved
            writeLE32(54); // pixel data offset

            // DIB header (40 bytes)
            writeLE32(40); // header size
            writeLE32(static_cast<uint32_t>(w));
            writeLE32(static_cast<uint32_t>(h));
            writeLE16(1);  // planes
            writeLE16(24); // bpp
            writeLE32(0);  // compression
            writeLE32(static_cast<uint32_t>(dataSize));
            writeLE32(2835); writeLE32(2835); // resolution (72 dpi)
            writeLE32(0); writeLE32(0); // palette

            // Pixel data (bottom-up, BGR)
            uint8_t pad[3] = { 0, 0, 0 };
            for (int y = h - 1; y >= 0; --y) {
                for (int x = 0; x < w; ++x) {
                    int idx = (y * w + x) * 3;
                    uint8_t bgr[3] = { pixels[idx + 2], pixels[idx + 1], pixels[idx] };
                    f.write(reinterpret_cast<char*>(bgr), 3);
                }
                if (rowPad > 0) f.write(reinterpret_cast<char*>(pad), rowPad);
            }
            f.close();
            return true;
        }
    };

} // namespace jc
#endif // JC2_IMAGE_H
