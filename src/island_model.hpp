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
//
// Justificación de cada parámetro:
//
//   n_islas = 4 (configurable vía --threads):
//     Una isla por hilo OpenMP. Con 4 islas se logra buen balance entre
//     diversidad de subpoblaciones y overhead de sincronización en migración.
//     Con menos de 2 islas el modelo degenera en un AG simple; con más de 8
//     las subpoblaciones son demasiado pequeñas para mantener diversidad.
//
//   pop_por_isla = 50 (mínimo garantizado 20):
//     Calculado como max(20, pop_size / n_islas). Con pop_size=200 y 4 islas
//     se obtienen 50 individuos por isla, suficiente para torneo K=5 y
//     cruzamiento en pares sin pérdida de diversidad prematura.
//     El mínimo de 20 garantiza al menos 10 parejas por generación.
//
//   frec_migracion = 25 generaciones:
//     Valor estándar en la literatura (Whitley et al., 1999). Intervalo
//     suficiente para que cada isla desarrolle sus propias características
//     antes de recibir material genético externo. Un intervalo más corto
//     homogeneiza las islas; uno más largo reduce el beneficio del modelo.
//
//   n_migrantes = 2:
//     Migrar solo los 2 mejores individuos por evento mantiene la presión
//     selectiva sin inundar la isla destino con material genético externo.
//     Representa el 4% de la subpoblación (2/50), proporción recomendada
//     entre 1% y 10% (Cantu-Paz, 1998).
//
//   Criterio de selección de migrantes:
//     Elitismo: los N_MIGRANTES individuos con mayor fitness de la isla origen.
//     Reemplazan a los N_MIGRANTES individuos con menor fitness de la destino.
//     Esto garantiza que el material genético que se transfiere es el mejor
//     disponible, acelerando la convergencia global.
//
//   Topología en anillo:
//     Cada isla envía migrantes a la isla siguiente (i → i+1 mod N_ISLAS).
//     El anillo garantiza que el material genético se propague a todas las
//     islas tras N_ISLAS eventos de migración (conectividad garantizada).
//     Alternativas como topología aleatoria tienen mayor varianza; el modelo
//     maestro-esclavo centraliza la migración y crea cuello de botella.
// ---------------------------------------------------------------------------
struct IslandParams {
    int    n_islas             = 4;    // Número de islas (= número de hilos OpenMP)
    int    pop_por_isla        = 50;   // Individuos por isla; mínimo garantizado: 20
    int    max_generaciones    = 500;  // Máximo de generaciones totales por isla
    int    frec_migracion      = 25;   // Cada cuántas generaciones se migra
    int    n_migrantes         = 2;    // Cuántos individuos se transfieren por evento
    double prob_cruzamiento    = 0.85; // Igual que Variante 1 y 2 para comparabilidad
    double prob_mutacion       = 0.02; // Igual que Variante 1 y 2 para comparabilidad
    int    torneo_k            = 5;    // Igual que Variante 1 y 2 para comparabilidad
    int    sin_mejora_max      = 100;  // Parada anticipada si la isla se estanca
    unsigned int seed          = 42;   // Semilla base; isla i usa seed + i
};

// ---------------------------------------------------------------------------
// Ejecuta el modelo de islas y retorna el mejor individuo factible global
// (o el de mayor fitness si no hay ninguno factible).
// ---------------------------------------------------------------------------
GAResult runIslandModel(const ProblemInstance& inst, const IslandParams& params);
