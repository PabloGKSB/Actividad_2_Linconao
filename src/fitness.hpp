// =============================================================================
// fitness.hpp
// Declaración de la función de aptitud y la estructura Individuo para el
// algoritmo genético del problema de la mochila extendida.
// =============================================================================
#pragma once

#include "instance_loader.hpp"
#include <vector>

// ---------------------------------------------------------------------------
// Parámetros de penalización de la función de aptitud
// Se justifican en README.md; aquí quedan como constantes configurables.
// ---------------------------------------------------------------------------
namespace FitnessParams {
    constexpr double ALPHA   = 10.0;    // Penalización por unidad de peso excedida
    constexpr double BETA    = 10.0;    // Penalización por unidad de volumen excedida
    constexpr double GAMMA   = 500.0;   // Penalización por violación de restricción de categoría
    constexpr double DELTA   = 1000.0;  // Penalización por par incompatible seleccionado
    constexpr double EPSILON = 1000.0;  // Penalización por dependencia incumplida
}

// ---------------------------------------------------------------------------
// Estructura: Individuo
// Representa una solución candidata dentro de la población del AG.
// ---------------------------------------------------------------------------
struct Individuo {
    std::vector<int> cromosoma;  // Bits 0/1: 1 = ítem en la mochila
    double fitness;              // Aptitud calculada (puede ser negativa)
    bool   es_factible;          // true si cumple todas las restricciones duras
    int    valor_total;          // Suma de valores de ítems seleccionados
};

// ---------------------------------------------------------------------------
// Calcula la aptitud de un individuo dado una instancia del problema.
//
// Fórmula:
//   fitness = valor_total
//             - α × exceso_peso
//             - β × exceso_volumen
//             - γ × violaciones_categoria
//             - δ × incompatibilidades_violadas
//             - ε × dependencias_incumplidas
//
// También actualiza:
//   - individuo.valor_total
//   - individuo.es_factible
// ---------------------------------------------------------------------------
void calcularFitness(Individuo& ind, const ProblemInstance& inst);
