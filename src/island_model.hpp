// =============================================================================
// island_model.hpp
// Declaración del Modelo de Islas (Variante 3) para el algoritmo genético
// paralelo del problema de la mochila extendida.
//
// Topología: anillo (isla i → isla (i+1) % N_ISLAS)
// Migración:  los N_MIGRANTES mejores de isla i reemplazan a los N_MIGRANTES
//             peores de la isla destino.
// =============================================================================
#pragma once

#include "fitness.hpp"
#include "instance_loader.hpp"
#include "genetic_algorithm.hpp"
#include <vector>

// ---------------------------------------------------------------------------
// Parámetros del Modelo de Islas
// ---------------------------------------------------------------------------
struct IslandParams {
    int    n_islas             = 4;    // Número de islas
    int    pop_por_isla        = 50;   // Individuos por isla (total = n_islas × pop_por_isla)
    int    max_generaciones    = 500;  // Máximo de generaciones por isla
    int    frec_migracion      = 25;   // Frecuencia de migración (en generaciones)
    int    n_migrantes         = 2;    // Número de migrantes por evento
    double prob_cruzamiento    = 0.85;
    double prob_mutacion       = 0.02;
    int    torneo_k            = 5;
    int    sin_mejora_max      = 100;
    unsigned int seed          = 42;   // Semilla base; cada isla usa seed + id_isla
};

// ---------------------------------------------------------------------------
// Ejecuta el modelo de islas y retorna el mejor individuo factible global
// (o el de mayor fitness si no hay ninguno factible).
// ---------------------------------------------------------------------------
GAResult runIslandModel(const ProblemInstance& inst, const IslandParams& params);
