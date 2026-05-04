// =============================================================================
// instance_loader.hpp
// Definición de estructuras de datos y declaración de funciones para
// cargar instancias del problema de la mochila extendida desde archivos CSV.
// =============================================================================
#pragma once

#include <vector>
#include <string>
#include <unordered_set>
#include <utility>

// ---------------------------------------------------------------------------
// Estructura: Item
// Representa un objeto candidato para incluir en la mochila.
// ---------------------------------------------------------------------------
struct Item {
    int id;         // Identificador único (1-based en los CSV)
    int valor;      // Beneficio si el ítem es seleccionado
    int peso;       // Peso del ítem
    int volumen;    // Volumen del ítem
    int categoria;  // Grupo al que pertenece (1..5)
};

// ---------------------------------------------------------------------------
// Estructura: CategoryRule
// Define cuántos ítems de una categoría pueden/deben incluirse.
// ---------------------------------------------------------------------------
struct CategoryRule {
    int categoria;
    int minimo;
    int maximo;
};

// ---------------------------------------------------------------------------
// Estructura: ProblemInstance
// Agrupa todos los datos de una instancia del problema.
// ---------------------------------------------------------------------------
struct ProblemInstance {
    std::vector<Item> items;                         // Lista de ítems
    long long W;                                     // Capacidad máxima de peso
    long long V;                                     // Capacidad máxima de volumen
    std::vector<CategoryRule> category_rules;        // Reglas por categoría
    std::vector<std::pair<int,int>> incompatibilities; // Pares de ítems incompatibles (índices 0-based)
    std::vector<std::pair<int,int>> dependencies;     // Par (ítem, requerido) (índices 0-based)
};

// ---------------------------------------------------------------------------
// Funciones públicas
// ---------------------------------------------------------------------------

/**
 * Carga una instancia completa desde una carpeta que contiene:
 *   items.csv, config.csv, category_rules.csv,
 *   incompatibilities.csv, dependencies.csv
 *
 * @param folder  Ruta a la carpeta con los archivos CSV.
 * @return        ProblemInstance completamente cargada.
 * @throws std::runtime_error si algún archivo no puede abrirse.
 */
ProblemInstance loadInstance(const std::string& folder);
