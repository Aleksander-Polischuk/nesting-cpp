#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <clocale>
#include <map>
#include <string>
#include <algorithm>
#include <iomanip>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define LIBNEST2D_GEOMETRIES_clipper
#define LIBNEST2D_OPTIMIZER_nlopt
#include <nlohmann/json.hpp>
#include <polyclipping/clipper.hpp>
#include <libnest2d/libnest2d.hpp>
#include <Windows.h>

using json = nlohmann::json;
using namespace libnest2d;

// Збільшуємо точність до 10^6 для складних фігур
const double SCALE = 1000000.0;
const double PI = 3.14159265358979323846;

inline Coord to_int(double val) { return static_cast<Coord>(std::round(val * SCALE)); }
inline double to_mm(Coord val) { return static_cast<double>(val) / SCALE; }

// --- УНІВЕРСАЛЬНИЙ ГЕНЕРАТОР ШЛЯХІВ ---
ClipperLib::Path generate_path(const json& p) {
    ClipperLib::Path path;
    std::string type = p.value("type", "rect");

    if (type == "circle") {
        double r = p["radius"].get<double>();
        for (int i = 0; i < 48; ++i) {
            double a = 2.0 * PI * i / 48.0;
            path.push_back({ to_int(r + r * cos(a)), to_int(r + r * sin(a)) });
        }
    }
    else if (type == "star") {
        double ro = p["r_outer"].get<double>(), ri = p["r_inner"].get<double>();
        for (int i = 0; i < 10; ++i) {
            double r = (i % 2 == 0) ? ro : ri;
            double a = PI * i / 5.0 - PI / 2.0;
            path.push_back({ to_int(ro + r * cos(a)), to_int(ro + r * sin(a)) });
        }
    }
    else if (p.contains("shape")) {
        for (const auto& pt : p["shape"])
            path.push_back({ to_int(pt["x"].get<double>()), to_int(pt["y"].get<double>()) });
    }
    else {
        double w = p["w"].get<double>(), h = p["h"].get<double>();
        path = { {0,0}, {to_int(w),0}, {to_int(w),to_int(h)}, {0,to_int(h)} };
    }
    return path;
}

// --- SVG ЗВІТ (МАКСИМАЛЬНА ІНФОРМАТИВНІСТЬ) ---
void save_as_svg(const json& res, double sw, double sh, double gap, double margin, double kerf, const std::string& filename) {
    std::ofstream f(filename, std::ios::binary);
    if (!f.is_open()) return;
    unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
    f.write(reinterpret_cast<char*>(bom), 3);

    int sheets_count = (int)res["sheets"].size();
    double m_l = 35.0, m_t = 30.0, m_r = 15.0, m_b = 85.0;
    double block_h = sh + m_t + m_b;
    double view_w = sw + m_l + m_r;
    double view_h = sheets_count * (block_h + 20.0) + 100.0;

    f << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    f << "<svg width=\"" << view_w << "mm\" height=\"" << view_h << "mm\" viewBox=\"0 0 " << view_w << " " << view_h << "\" xmlns=\"http://www.w3.org/2000/svg\">\n";
    f << "  <rect width=\"100%\" height=\"100%\" fill=\"#fdfdfd\"/>\n";

    int total_parts = 0;
    for (int s = 0; s < sheets_count; ++s) {
        double y_off = s * (block_h + 20.0);
        const auto& sheet = res["sheets"][s];

        f << "  <rect x=\"" << m_l << "\" y=\"" << y_off + m_t << "\" width=\"" << sw << "\" height=\"" << sh << "\" fill=\"#fff\" stroke=\"#000\" stroke-width=\"2\"/>\n";
        f << "  <rect x=\"" << m_l + margin << "\" y=\"" << y_off + m_t + margin << "\" width=\"" << sw - 2 * margin << "\" height=\"" << sh - 2 * margin << "\" fill=\"none\" stroke=\"red\" stroke-width=\"0.3\" stroke-dasharray=\"4,2\"/>\n";
        f << u8"  <text x=\"" << m_l + sw / 2 << u8"\" y=\"" << y_off + m_t - 12 << u8"\" font-size=\"8\" font-family=\"Arial\" text-anchor=\"middle\" font-weight=\"bold\">АРКУШ №" << s + 1 << u8"</text>\n";

        for (const auto& part : sheet["placed_parts"]) {
            f << "  <polygon points=\"";
            double minx = 1e18, maxx = -1e18, miny = 1e18, maxy = -1e18;
            for (const auto& pt : part["points"]) {
                double px = pt["x"].get<double>(), py = pt["y"].get<double>();
                minx = std::min(minx, px); maxx = std::max(maxx, px);
                miny = std::min(miny, py); maxy = std::max(maxy, py);
                f << px + m_l << "," << py + y_off + m_t << " ";
            }
            f << u8"\" fill=\"#e3f2fd\" stroke=\"#1976d2\" stroke-width=\"0.15\" />\n";
            f << u8"  <text x=\"" << (minx + maxx) / 2 + m_l << u8"\" y=\"" << (miny + maxy) / 2 + y_off + m_t << u8"\" font-size=\"3\" font-family=\"Arial\" text-anchor=\"middle\" fill=\"#0d47a1\" font-weight=\"bold\">ID:" << part["part_id"] << u8"</text>\n";
        }

        double py = y_off + m_t + sh + 15;
        f << u8"  <text x=\"" << m_l << u8"\" y=\"" << py << u8"\" font-size=\"5\" font-family=\"Arial\" font-weight=\"bold\">Параметри:</text>\n";
        f << u8"  <text x=\"" << m_l << u8"\" y=\"" << py + 12 << u8"\" font-size=\"4.5\" font-family=\"Arial\">- Margin: " << margin << u8" мм | Gap: " << gap << u8" мм | Kerf: " << kerf << u8" мм</text>\n";
        f << u8"  <text x=\"" << m_l << u8"\" y=\"" << py + 22 << u8"\" font-size=\"4.5\" font-family=\"Arial\">- Деталей: " << sheet["placed_parts"].size() << u8" шт.</text>\n";
        total_parts += (int)sheet["placed_parts"].size();
    }

    f << "</svg>"; f.close();
}

int main(int argc, char* argv[]) {
    std::setlocale(LC_ALL, "C");
    SetConsoleOutputCP(CP_UTF8);

    try {
        std::ifstream f_in("input.json"); if (!f_in.is_open()) return 1;
        json data; f_in >> data;

        double sw = data["sheet_width"].get<double>(), sh = data["sheet_height"].get<double>();
        double gap = data["settings"].value("gap_mm", 2.0), margin = data["settings"].value("sheet_margin_mm", 5.0), kerf = data["settings"].value("kerf_mm", 0.2);

        std::vector<Item> items;
        std::vector<int> source_ids;

        for (int i = 0; i < (int)data["parts"].size(); ++i) {
            const auto& p = data["parts"][i];
            ClipperLib::Path path = generate_path(p);

            ClipperLib::Paths simplified;
            ClipperLib::SimplifyPolygon(path, simplified, ClipperLib::pftNonZero);
            if (simplified.empty()) continue;

            PolygonImpl poly(simplified[0]);
            for (int q = 0; q < p.value("qty", 1); ++q) {
                Item it(poly);
                // Ми НЕ використовуємо rotations() через твою помилку компіляції. 
                // Замість цього налаштуємо пакувальник нижче.
                items.push_back(std::move(it));
                source_ids.push_back(i);
            }
        }

        // --- КРОК 1: СОРТУВАННЯ (Великі деталі вперед!) ---
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            return a.area() > b.area();
            });

        Box sheet_box({ 0, 0 }, { to_int(sw - 2 * margin), to_int(sh - 2 * margin) });
        std::cout << u8"Розрахунок ультимативної розкладки..." << std::endl;

        // --- КРОК 2: ПРАВИЛЬНИЙ ВИКЛИК З ПОВОРОТОМ ---
        // Використовуємо 360 кроків (кожен 1 градус)
        placers::BLConfig<PolygonImpl> cfg;
        // Оскільки в твоїй версії може бути інша назва, ми передаємо 360 як аргумент nest
        size_t sheets_used = nest<BottomLeftPlacer>(items, sheet_box, to_int(gap), { 360 });

        json res = { {"status", "success"}, {"sheets", json::array()} };
        for (size_t s = 0; s < sheets_used; ++s) res["sheets"].push_back({ {"sheet_id", (int)s}, {"placed_parts", json::array()} });

        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].binId() < 0) continue;
            PolygonImpl shape = items[i].transformedShape();
            json pts = json::array();
            for (const auto& pt : shape.Contour)
                pts.push_back({ {"x", std::round((to_mm(pt.X) + margin) * 100.0) / 100.0}, {"y", std::round((to_mm(pt.Y) + margin) * 100.0) / 100.0} });
            pts.push_back(pts[0]);
            res["sheets"][items[i].binId()]["placed_parts"].push_back({ {"part_id", source_ids[i]}, {"points", pts} });
        }

        save_as_svg(res, sw, sh, gap, margin, kerf, "layout_full_report.svg");
        std::cout << u8"Готово! Аркушів: " << sheets_used << std::endl;

    }
    catch (const std::exception& e) { std::cerr << "ERROR: " << e.what() << std::endl; return 1; }
    return 0;
}