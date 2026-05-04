// =============================================================================
// genetic_algorithm.hpp
// Declaración del algoritmo genético secuencial y paralelo con OpenMP
// para el problema de la mochila extendida.
// =============================================================================
#pragma once

#include "fitness.hpp"
#include "instance_loader.hpp"
#include <vector>
#include <random>

// ---------------------------------------------------------------------------
// Parámetros del algoritmo genético
// ---------------------------------------------------------------------------
struct GAParams {
    int    pop_size         = 200;   // Tamaño de la población
    int    max_generaciones = 500;   // Máximo de generaciones
    double prob_cruzamiento = 0.85;  // Probabilidad de cruzamiento
    double prob_mutacion    = 0.02;  // Probabilidad de mutación por bit
    int    torneo_k         = 5;     // Tamaño del torneo
    int    sin_mejora_max   = 100;   // Criterio de parada por estancamiento
    unsigned int seed       = 42;    // Semilla del RNG principal
};

// ---------------------------------------------------------------------------
// Resultado de una ejecución del algoritmo genético
// ---------------------------------------------------------------------------
struct GAResult {
    Individuo mejor;              // Mejor individuo encontrado (factible si existe)
    int       generaciones_usadas; // Cuántas generaciones se ejecutaron
    double    tiempo_ms;           // Tiempo de ejecución en milisegundos
};

// ---------------------------------------------------------------------------
// Variante 1: Algoritmo Genético Secuencial
// ---------------------------------------------------------------------------
GAResult runSequential(const ProblemInstance& inst, const GAParams& params);

// ---------------------------------------------------------------------------
// Variante 2: Algoritmo Genético Paralelo con OpenMP
// Paraleliza: evaluación de fitness, generación de hijos, selección por torneo
// ---------------------------------------------------------------------------
GAResult runParallel(const ProblemInstance& inst, const GAParams& params);

// ---------------------------------------------------------------------------
// Funciones de operadores genéticos (compartidas por ambas variantes)
// ---------------------------------------------------------------------------

/**
 * Genera un individuo aleatorio.
 * @param n    Número de ítems.
 * @param rng  Generador de números aleatorios (mt19937).
 */
Individuo generarIndividuoAleatorio(int n, std::mt19937& rng);

/**
 * Selección por torneo: elige K individuos al azar y retorna el de mayor fitness.
 * @param poblacion  Población actual.
 * @param k          Tamaño del torneo.
 * @param rng        Generador de números aleatorios.
 */
const Individuo& seleccionTorneo(const std::vector<Individuo>& poblacion,
                                  int k, std::mt19937& rng);

/**
 * Cruzamiento de un punto: genera 2 hijos a partir de 2 padres.
 * @param padre1, padre2  Padres.
 * @param hijo1, hijo2    Hijos generados (output).
 * @param rng             Generador de números aleatorios.
 */
void cruzamientoUnPunto(const Individuo& padre1, const Individuo& padre2,
                         Individuo& hijo1, Individuo& hijo2,
                         std::mt19937& rng);

/**
 * Mutación por bit flip: invierte cada bit con probabilidad prob_mutacion.
 * @param ind            Individuo a mutar (modificado in-place).
 * @param prob_mutacion  Probabilidad de inversión por bit.
 * @param rng            Generador de números aleatorios.
 */
void mutacion(Individuo& ind, double prob_mutacion, std::mt19937& rng);
