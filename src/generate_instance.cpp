// =============================================================================
// generate_instance.cpp
// Generador de instancias aleatorias para el problema de la mochila extendida.
//
// Uso:
//   ./generate_instance --size 100   --output data/small  --seed 42
//   ./generate_instance --size 1000  --output data/medium --seed 42
//   ./generate_instance --size 10000 --output data/large  --seed 42
//
// Genera los archivos:
//   items.csv, config.csv, category_rules.csv,
//   incompatibilities.csv, dependencies.csv
// =============================================================================
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <random>
#include <filesystem>
#include <stdexcept>
#include <algorithm>
#include <set>
#include <utility>
#include <cmath>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Estructura de ítem para la generación
// ---------------------------------------------------------------------------
struct Item {
    int id;
    int valor;
    int peso;
    int volumen;
    int categoria;
};

// ---------------------------------------------------------------------------
// Parseo de argumentos
// ---------------------------------------------------------------------------
struct GenArgs {
    int          size   = 100;
    std::string  output = "data/small";
    unsigned int seed   = 42;
};

static void printUsage(const std::string& prog) {
    std::cout << "Uso: " << prog << " --size <N> --output <carpeta> --seed <S>\n\n"
              << "Ejemplos:\n"
              << "  " << prog << " --size 100   --output data/small  --seed 42\n"
              << "  " << prog << " --size 1000  --output data/medium --seed 42\n"
              << "  " << prog << " --size 10000 --output data/large  --seed 42\n";
}

static GenArgs parseArgs(int argc, char* argv[]) {
    GenArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string flag(argv[i]);
        if (flag == "--help" || flag == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        }
        auto next = [&]() -> std::string {
            if (i + 1 >= argc)
                throw std::runtime_error("Falta valor para: " + flag);
            return std::string(argv[++i]);
        };
        if      (flag == "--size")   args.size   = std::stoi(next());
        else if (flag == "--output") args.output = next();
        else if (flag == "--seed")   args.seed   = static_cast<unsigned>(std::stoi(next()));
        else std::cerr << "[WARNING] Argumento ignorado: " << flag << "\n";
    }
    return args;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    GenArgs args;
    try {
        args = parseArgs(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        printUsage(argv[0]);
        return 1;
    }

    const int n = args.size;
    std::mt19937 rng(args.seed);

    // Crear carpeta de salida
    fs::create_directories(args.output);

    // ------------------------------------------------------------------
    // 1. Generar ítems
    // ------------------------------------------------------------------
    std::uniform_int_distribution<int> dist_valor(10, 1000);
    std::uniform_int_distribution<int> dist_peso(1, 100);
    std::uniform_int_distribution<int> dist_vol(1, 100);
    std::uniform_int_distribution<int> dist_cat(1, 5);

    std::vector<Item> items(n);
    long long total_peso = 0, total_vol = 0;

    for (int i = 0; i < n; ++i) {
        items[i].id        = i + 1;
        items[i].valor     = dist_valor(rng);
        items[i].peso      = dist_peso(rng);
        items[i].volumen   = dist_vol(rng);
        items[i].categoria = dist_cat(rng);
        total_peso += items[i].peso;
        total_vol  += items[i].volumen;
    }

    // Escribir items.csv
    {
        std::ofstream f(args.output + "/items.csv");
        f << "id,valor,peso,volumen,categoria\n";
        for (const auto& it : items) {
            f << it.id << "," << it.valor << "," << it.peso
              << "," << it.volumen << "," << it.categoria << "\n";
        }
    }

    // ------------------------------------------------------------------
    // 2. Calcular y escribir config.csv (W = 40%, V = 40%)
    // ------------------------------------------------------------------
    long long W = static_cast<long long>(0.40 * total_peso);
    long long V = static_cast<long long>(0.40 * total_vol);
    // Garantizar mínimo de 1
    if (W < 1) W = 1;
    if (V < 1) V = 1;

    {
        std::ofstream f(args.output + "/config.csv");
        f << "W,V\n" << W << "," << V << "\n";
    }

    // ------------------------------------------------------------------
    // 3. Escribir category_rules.csv
    //    Mínimo = 1, Máximo = n/5 por categoría
    // ------------------------------------------------------------------
    {
        std::ofstream f(args.output + "/category_rules.csv");
        f << "categoria,minimo,maximo\n";
        int max_cat = std::max(1, n / 5);
        for (int c = 1; c <= 5; ++c) {
            f << c << ",1," << max_cat << "\n";
        }
    }

    // ------------------------------------------------------------------
    // 4. Generar incompatibilidades (~2% de pares únicos)
    //    Usar un set de pares ordenados para evitar duplicados.
    // ------------------------------------------------------------------
    {
        // Calcular cuántos pares totales posibles hay
        long long pares_totales = static_cast<long long>(n) * (n - 1) / 2;
        int n_incomp = static_cast<int>(std::ceil(0.02 * pares_totales));
        // Limitar para no tardar demasiado con instancias grandes
        n_incomp = std::min(n_incomp, 50000);

        std::uniform_int_distribution<int> dist_item(0, n - 1);
        std::set<std::pair<int,int>> incomp_set;

        int intentos = 0;
        int max_intentos = n_incomp * 10;
        while (static_cast<int>(incomp_set.size()) < n_incomp && intentos < max_intentos) {
            int a = dist_item(rng);
            int b = dist_item(rng);
            if (a == b) { ++intentos; continue; }
            if (a > b) std::swap(a, b);
            incomp_set.insert({a, b});
            ++intentos;
        }

        std::ofstream f(args.output + "/incompatibilities.csv");
        f << "id_item_a,id_item_b\n";
        for (const auto& [a, b] : incomp_set) {
            // Escribir IDs 1-based
            f << (a + 1) << "," << (b + 1) << "\n";
        }

        std::cout << "[INFO] Incompatibilidades generadas: " << incomp_set.size() << "\n";
    }

    // ------------------------------------------------------------------
    // 5. Generar dependencias (~1% de ítems)
    //    Si se selecciona el ítem A, debe seleccionarse el ítem B.
    // ------------------------------------------------------------------
    {
        int n_deps = std::max(1, static_cast<int>(0.01 * n));
        std::uniform_int_distribution<int> dist_item(0, n - 1);
        std::set<int> deps_src; // Ítems que ya tienen dependencia (para unicidad del par)
        std::vector<std::pair<int,int>> deps;

        int intentos = 0;
        while (static_cast<int>(deps.size()) < n_deps && intentos < n_deps * 10) {
            int a = dist_item(rng);
            int b = dist_item(rng);
            if (a == b || deps_src.count(a)) { ++intentos; continue; }
            deps.emplace_back(a, b);
            deps_src.insert(a);
            ++intentos;
        }

        std::ofstream f(args.output + "/dependencies.csv");
        f << "id_item,id_requerido\n";
        for (const auto& [item, req] : deps) {
            f << (item + 1) << "," << (req + 1) << "\n";
        }

        std::cout << "[INFO] Dependencias generadas: " << deps.size() << "\n";
    }

    // ------------------------------------------------------------------
    // Resumen
    // ------------------------------------------------------------------
    std::cout << "=== Instancia generada ===\n"
              << "  Tamaño:  " << n << " ítems\n"
              << "  Carpeta: " << args.output << "\n"
              << "  W = " << W << "  (40% de " << total_peso << ")\n"
              << "  V = " << V << "  (40% de " << total_vol  << ")\n"
              << "  Semilla: " << args.seed << "\n";

    return 0;
}
