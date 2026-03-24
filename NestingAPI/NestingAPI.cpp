#include <iostream>
#include <fstream>
#include <vector>
#include <clocale>

#define LIBNEST2D_GEOMETRIES_clipper
#define LIBNEST2D_OPTIMIZER_nlopt

#include <nlohmann/json.hpp>
#include <polyclipping/clipper.hpp>
#include <libnest2d/libnest2d.hpp>
#include <libnest2d/optimizers/nlopt/simplex.hpp>

using json = nlohmann::json;

using ItemType = libnest2d::Item;
using BinType = libnest2d::Box;
using Coord = libnest2d::Coord;
using PointImpl = libnest2d::Point;
using PolygonImpl = libnest2d::PolygonImpl;

// МАСШТАБ: 1 мм = 100 одиниць. 
// Цього достатньо, щоб мікро-фаска не перетворилася на нуль, і безпечно для процесора.
inline Coord to_internal(double mm) {
    return static_cast<Coord>(mm * 100.0);
}

inline double to_mm(Coord internal) {
    return static_cast<double>(internal) / 100.0;
}

int main(int argc, char* argv[]) {
    std::setlocale(LC_ALL, "C");

    // ТИМЧАСОВО для дебагу жорстко прописуємо імена файлів
    std::string in_file = "input.json";
    std::string out_file = "output.json";

    try {
        std::cout << "[1/4] Reading " << in_file << std::endl;
        std::ifstream input_file(in_file);
        if (!input_file.is_open()) {
            std::cerr << "Cannot find input.json! Make sure it is in the same folder as the .exe" << std::endl;
            return 1;
        }
        json data;
        input_file >> data;

        Coord sheet_w = to_internal(data["sheet_width"].get<double>());
        Coord sheet_h = to_internal(data["sheet_height"].get<double>());
        BinType sheet_box(sheet_w, sheet_h);

        std::vector<ItemType> items;

        Coord gap = to_internal(1.0);
        Coord d = to_internal(0.02); // НАШ РЯТІВНИК: Фаска 0.02 мм

        for (const auto& part : data["parts"]) {
            int qty = part.value("qty", 1);
            Coord w = to_internal(part["w"].get<double>()) + gap;
            Coord h = to_internal(part["h"].get<double>()) + gap;

            // БУДУЄМО ФІГУРУ ЗІ ЗРІЗАНИМИ КУТАМИ
            // Замість 4 точок, робимо 8. Це повністю блокує баг Clipper-а.
            libnest2d::PathImpl rect_path;
            rect_path.push_back(PointImpl(d, 0));
            rect_path.push_back(PointImpl(0, d));
            rect_path.push_back(PointImpl(0, h - d));
            rect_path.push_back(PointImpl(d, h));
            rect_path.push_back(PointImpl(w - d, h));
            rect_path.push_back(PointImpl(w, h - d));
            rect_path.push_back(PointImpl(w, d));
            rect_path.push_back(PointImpl(w - d, 0));

            PolygonImpl poly(rect_path);

            for (int i = 0; i < qty; ++i) {
                items.emplace_back(poly);
            }
        }

        std::cout << "[2/4] Starting nesting engine (NfpPlacer with Chamfer Hack)..." << std::endl;

        // Розкрій без автоматичних зазорів (щоб бібліотека не псувала наші контури)
        size_t num_sheets = libnest2d::nest(items, sheet_box);

        std::cout << "[3/4] Nesting finished! Sheets: " << num_sheets << std::endl;

        json result_json;
        result_json["status"] = "success";
        result_json["sheets_count"] = (int)num_sheets;
        result_json["sheets"] = json::array();

        for (size_t s = 0; s < num_sheets; ++s) {
            json sheet_data;
            sheet_data["sheet_id"] = (int)s;
            sheet_data["placed_parts"] = json::array();

            for (const auto& item : items) {
                if (item.binId() == (int)s) {
                    json p;
                    p["x"] = to_mm(item.translation().X);
                    p["y"] = to_mm(item.translation().Y);
                    p["rotation"] = (double)item.rotation();
                    sheet_data["placed_parts"].push_back(p);
                }
            }
            result_json["sheets"].push_back(sheet_data);
        }

        std::cout << "[4/4] Saving results to " << argv[2] << std::endl;
        std::ofstream output_file(argv[2]);
        output_file << result_json.dump(4);

        std::cout << "SUCCESS!" << std::endl;

    }
    catch (const std::exception& e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}