#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <clocale>

#define LIBNEST2D_GEOMETRIES_clipper
#define LIBNEST2D_OPTIMIZER_nlopt

#include <nlohmann/json.hpp>
#include <polyclipping/clipper.hpp>
#include <libnest2d/libnest2d.hpp>

using json = nlohmann::json;
using namespace libnest2d;

// МАСШТАБ: 1000 одиниць = 1 мм (найкраще для Clipper)
const double SCALE = 1000.0;
inline Coord to_int(double val) { return static_cast<Coord>(std::round(val * SCALE)); }
inline double to_mm(Coord val) { return static_cast<double>(val) / SCALE; }

int main(int argc, char* argv[]) {
    std::setlocale(LC_ALL, "C");

    // Читаємо аргументи правильно з командного рядка
    std::string in_file = (argc > 1) ? argv[1] : "input.json";
    std::string out_file = (argc > 2) ? argv[2] : "output.json";

    try {
        std::ifstream f_in(in_file);
        if (!f_in.is_open()) {
            std::cerr << "Error: Cannot open " << in_file << std::endl;
            return 1;
        }
        json data;
        f_in >> data;

        // Налаштування аркуша
        Box sheet(to_int(data["sheet_width"].get<double>()),
            to_int(data["sheet_height"].get<double>()));

        double gap_mm = data.contains("settings") ? data["settings"].value("gap_mm", 0.0) : 0.0;

        std::vector<Item> items;
        std::vector<int> source_ids;

        for (int i = 0; i < (int)data["parts"].size(); ++i) {
            const auto& part = data["parts"][i];
            int qty = part.value("qty", 1);

            ClipperLib::Path path;
            if (part.contains("shape")) {
                for (const auto& pt : part["shape"])
                    path.push_back({ to_int(pt["x"].get<double>()), to_int(pt["y"].get<double>()) });
            }
            else {
                double w = part["w"].get<double>();
                double h = part["h"].get<double>();
                path = { {0,0}, {to_int(w), 0}, {to_int(w), to_int(h)}, {0, to_int(h)} };
            }

            // ВАЖЛИВО: Очищення та фіксація орієнтації (Clipper)
            ClipperLib::CleanPolygon(path, 1.0);
            if (!ClipperLib::Orientation(path)) ClipperLib::ReversePath(path);

            if (path.size() < 3) continue;

            PolygonImpl poly(path);
            for (int q = 0; q < qty; ++q) {
                items.emplace_back(poly);
                source_ids.push_back(i);
            }
        }

        std::cout << "[1/2] Nesting " << items.size() << " parts on "
            << data["sheet_width"] << "x" << data["sheet_height"] << "..." << std::endl;

        // Використовуємо BottomLeftPlacer для стабільності на старті
        size_t sheets_used = nest<BottomLeftPlacer>(items, sheet, to_int(gap_mm));

        json res = { {"status", "success"}, {"sheets_count", (int)sheets_used}, {"sheets", json::array()} };
        for (size_t s = 0; s < sheets_used; ++s)
            res["sheets"].push_back({ {"sheet_id", (int)s}, {"placed_parts", json::array()} });

        int placed_count = 0;
        for (size_t i = 0; i < items.size(); ++i) {
            const auto& item = items[i];
            int b_id = item.binId();
            if (b_id < 0) continue; // Ця деталь не влізла на аркуш!

            placed_count++;
            PolygonImpl shape = item.transformedShape();
            json pts = json::array();
            for (const auto& pt : shape.Contour) pts.push_back({ {"x", to_mm(pt.X)}, {"y", to_mm(pt.Y)} });
            if (!pts.empty()) pts.push_back(pts[0]);

            res["sheets"][b_id]["placed_parts"].push_back({
                {"part_id", source_ids[i]},
                {"rotation", (double)item.rotation()},
                {"points", pts}
                });
        }

        std::ofstream f_out(out_file);
        f_out << res.dump(4);
        std::cout << "[2/2] Done! Placed " << placed_count << " parts. Saved to " << out_file << std::endl;

    }
    catch (const std::exception& e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}