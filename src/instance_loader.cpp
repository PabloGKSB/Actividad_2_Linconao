// =============================================================================
// instance_loader.cpp
// Implementación de la carga de instancias del problema de la mochila
// extendida desde archivos CSV.
// =============================================================================
#include "instance_loader.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <algorithm>

// ---------------------------------------------------------------------------
// Función auxiliar: trim de espacios y '\r' (compatibilidad Windows)
// ---------------------------------------------------------------------------
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// ---------------------------------------------------------------------------
// Carga items.csv
// Formato esperado: id,valor,peso,volumen,categoria
// ---------------------------------------------------------------------------
static std::vector<Item> loadItems(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("No se pudo abrir: " + filepath);
    }

    std::vector<Item> items;
    std::string line;

    // Saltar cabecera
    std::getline(file, line);

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string token;
        Item item;

        std::getline(ss, token, ','); item.id       = std::stoi(trim(token));
        std::getline(ss, token, ','); item.valor    = std::stoi(trim(token));
        std::getline(ss, token, ','); item.peso     = std::stoi(trim(token));
        std::getline(ss, token, ','); item.volumen  = std::stoi(trim(token));
        std::getline(ss, token, ','); item.categoria = std::stoi(trim(token));

        items.push_back(item);
    }
    return items;
}

// ---------------------------------------------------------------------------
// Carga config.csv
// Formato esperado: W,V (segunda línea contiene los valores)
// ---------------------------------------------------------------------------
static void loadConfig(const std::string& filepath, long long& W, long long& V) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("No se pudo abrir: " + filepath);
    }

    std::string line;
    // Saltar cabecera
    std::getline(file, line);
    // Leer valores
    std::getline(file, line);
    line = trim(line);

    std::istringstream ss(line);
    std::string token;
    std::getline(ss, token, ','); W = std::stoll(trim(token));
    std::getline(ss, token, ','); V = std::stoll(trim(token));
}

// ---------------------------------------------------------------------------
// Carga category_rules.csv
// Formato esperado: categoria,minimo,maximo
// ---------------------------------------------------------------------------
static std::vector<CategoryRule> loadCategoryRules(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("No se pudo abrir: " + filepath);
    }

    std::vector<CategoryRule> rules;
    std::string line;

    // Saltar cabecera
    std::getline(file, line);

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string token;
        CategoryRule rule;

        std::getline(ss, token, ','); rule.categoria = std::stoi(trim(token));
        std::getline(ss, token, ','); rule.minimo    = std::stoi(trim(token));
        std::getline(ss, token, ','); rule.maximo    = std::stoi(trim(token));

        rules.push_back(rule);
    }
    return rules;
}

// ---------------------------------------------------------------------------
// Carga incompatibilities.csv
// Formato: id_item_a,id_item_b (IDs 1-based, convertimos a índices 0-based)
// ---------------------------------------------------------------------------
static std::vector<std::pair<int,int>> loadIncompatibilities(
    const std::string& filepath, int n)
{
    std::vector<std::pair<int,int>> pairs;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        // Archivo opcional; si no existe simplemente retornamos vacío
        return pairs;
    }

    std::string line;
    std::getline(file, line); // cabecera

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string token;
        std::getline(ss, token, ','); int a = std::stoi(trim(token)) - 1;
        std::getline(ss, token, ','); int b = std::stoi(trim(token)) - 1;

        // Validar rango
        if (a >= 0 && a < n && b >= 0 && b < n && a != b) {
            pairs.emplace_back(a, b);
        }
    }
    return pairs;
}

// ---------------------------------------------------------------------------
// Carga dependencies.csv
// Formato: id_item,id_requerido (IDs 1-based, convertimos a índices 0-based)
// Si id_item está en la mochila, id_requerido también debe estarlo.
// ---------------------------------------------------------------------------
static std::vector<std::pair<int,int>> loadDependencies(
    const std::string& filepath, int n)
{
    std::vector<std::pair<int,int>> deps;
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return deps;
    }

    std::string line;
    std::getline(file, line); // cabecera

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string token;
        std::getline(ss, token, ','); int item     = std::stoi(trim(token)) - 1;
        std::getline(ss, token, ','); int required = std::stoi(trim(token)) - 1;

        if (item >= 0 && item < n && required >= 0 && required < n && item != required) {
            deps.emplace_back(item, required);
        }
    }
    return deps;
}

// ---------------------------------------------------------------------------
// loadInstance: función pública
// ---------------------------------------------------------------------------
ProblemInstance loadInstance(const std::string& folder) {
    ProblemInstance inst;

    // Construir rutas de archivos
    auto path = [&](const std::string& name) {
        return folder + "/" + name;
    };

    // Cargar datos
    inst.items           = loadItems(path("items.csv"));
    int n = static_cast<int>(inst.items.size());

    loadConfig(path("config.csv"), inst.W, inst.V);
    inst.category_rules  = loadCategoryRules(path("category_rules.csv"));
    inst.incompatibilities = loadIncompatibilities(path("incompatibilities.csv"), n);
    inst.dependencies    = loadDependencies(path("dependencies.csv"), n);

    std::cout << "[Instancia cargada] items=" << n
              << "  W=" << inst.W
              << "  V=" << inst.V
              << "  incomp=" << inst.incompatibilities.size()
              << "  deps=" << inst.dependencies.size()
              << "\n";

    return inst;
}
