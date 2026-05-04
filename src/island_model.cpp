// =============================================================================
// island_model.cpp
// Implementación del Modelo de Islas (Variante 3) con OpenMP.
//
// Diseño de paralelismo:
//   - Cada isla corre en un hilo OpenMP independiente (#pragma omp parallel for).
//   - Cada isla tiene su propio mt19937 (semilla = base_seed + isla_id).
//   - La migración utiliza #pragma omp critical para evitar data races al
//     acceder a las islas vecinas durante el intercambio de individuos.
//   - Entre eventos de migración, cada isla evoluciona de forma completamente
//     independiente (sin sincronización), maximizando el paralelismo.
// =============================================================================
#include "island_model.hpp"
#include <algorithm>
#include <omp.h>
#include <iostream>

// ---------------------------------------------------------------------------
// Función auxiliar: hacer evolucionar una isla por 'gens' generaciones.
// Retorna true si se alcanzó el criterio de estancamiento.
// ---------------------------------------------------------------------------
static bool evolucionarIsla(
    std::vector<Individuo>& isla,
    const ProblemInstance&  inst,
    const IslandParams&     params,
    std::mt19937&           rng,
    int                     gens,
    int&                    sin_mejora,
    double&                 mejor_fitness_acum)
{
    const int n        = static_cast<int>(inst.items.size());
    const int pop_size = static_cast<int>(isla.size());
    const int n_parejas = pop_size / 2;

    for (int g = 0; g < gens; ++g) {
        // ---- Criterio de parada ----
        double best_now = std::max_element(isla.begin(), isla.end(),
            [](const Individuo& a, const Individuo& b){
                return a.fitness < b.fitness;
            })->fitness;

        if (best_now > mejor_fitness_acum) {
            mejor_fitness_acum = best_now;
            sin_mejora = 0;
        } else {
            ++sin_mejora;
        }
        if (sin_mejora >= params.sin_mejora_max) return true; // estancada

        // ---- Generar hijos ----
        std::vector<Individuo> hijos(pop_size);
        for (int p = 0; p < n_parejas; ++p) {
            const Individuo& p1 = seleccionTorneo(isla, params.torneo_k, rng);
            const Individuo& p2 = seleccionTorneo(isla, params.torneo_k, rng);

            Individuo h1, h2;
            std::uniform_real_distribution<double> prob(0.0, 1.0);
            if (prob(rng) < params.prob_cruzamiento) {
                cruzamientoUnPunto(p1, p2, h1, h2, rng);
            } else {
                h1 = p1; h2 = p2;
            }
            mutacion(h1, params.prob_mutacion, rng);
            mutacion(h2, params.prob_mutacion, rng);
            calcularFitness(h1, inst);
            calcularFitness(h2, inst);
            hijos[2 * p]     = std::move(h1);
            hijos[2 * p + 1] = std::move(h2);
        }

        // ---- Pool = padres + hijos, top pop_size ----
        std::vector<Individuo> pool;
        pool.reserve(pop_size * 2);
        for (auto& ind : isla)   pool.push_back(ind);
        for (auto& ind : hijos)  pool.push_back(ind);

        std::sort(pool.begin(), pool.end(),
                  [](const Individuo& a, const Individuo& b){
                      return a.fitness > b.fitness;
                  });
        pool.resize(pop_size);
        isla = std::move(pool);
    }
    return false;
}

// ---------------------------------------------------------------------------
// runIslandModel
// ---------------------------------------------------------------------------
GAResult runIslandModel(const ProblemInstance& inst, const IslandParams& params) {
    const int n          = static_cast<int>(inst.items.size());
    const int n_islas    = params.n_islas;
    const int pop_por_isla = params.pop_por_isla;

    // ------------------------------------------------------------------
    // Inicializar cada isla con su propio RNG
    // ------------------------------------------------------------------
    std::vector<std::vector<Individuo>> islas(n_islas,
        std::vector<Individuo>(pop_por_isla));

    std::vector<std::mt19937> rngs(n_islas);
    for (int i = 0; i < n_islas; ++i) {
        rngs[i].seed(params.seed + static_cast<unsigned int>(i));
        for (auto& ind : islas[i]) {
            ind = generarIndividuoAleatorio(n, rngs[i]);
            calcularFitness(ind, inst);
        }
    }

    // Estado de parada por estancamiento por isla
    std::vector<int>    sin_mejora(n_islas, 0);
    std::vector<double> mejor_fit_acum(n_islas, -1e18);

    int gen_usadas   = 0;
    double t_inicio  = omp_get_wtime();

    // ------------------------------------------------------------------
    // Bucle de épocas: evolución entre migraciones
    // ------------------------------------------------------------------
    int epocas = params.max_generaciones / params.frec_migracion;
    for (int epoca = 0; epoca < epocas; ++epoca) {
        gen_usadas += params.frec_migracion;

        // ---- Evolución paralela de las islas ----
        // Cada isla corre en un hilo OpenMP distinto.
        // Variables privadas por hilo: id_isla, sus propias estructuras de datos.
        // Variables compartidas (solo lectura): inst, params.
        // Sin escritura cruzada entre islas en esta fase.
        #pragma omp parallel for schedule(static) num_threads(n_islas)
        for (int i = 0; i < n_islas; ++i) {
            evolucionarIsla(islas[i], inst, params, rngs[i],
                            params.frec_migracion,
                            sin_mejora[i], mejor_fit_acum[i]);
        }

        // ---- Migración en anillo ----
        // La sección crítica garantiza exclusión mutua al modificar islas vecinas.
        // Se procesan las migraciones secuencialmente para evitar que una isla
        // reciba y envíe migrantes en el mismo evento (consistencia del anillo).
        #pragma omp critical
        {
            for (int i = 0; i < n_islas; ++i) {
                int destino = (i + 1) % n_islas;

                // Ordenar isla i para obtener los N mejores
                std::sort(islas[i].begin(), islas[i].end(),
                          [](const Individuo& a, const Individuo& b){
                              return a.fitness > b.fitness;
                          });

                // Ordenar isla destino para identificar los N peores
                std::sort(islas[destino].begin(), islas[destino].end(),
                          [](const Individuo& a, const Individuo& b){
                              return a.fitness > b.fitness;
                          });

                int n_mig = std::min(params.n_migrantes, pop_por_isla);

                // Copiar los N_MIGRANTES mejores de isla i
                // sobre los N_MIGRANTES peores de isla destino
                for (int m = 0; m < n_mig; ++m) {
                    islas[destino][pop_por_isla - 1 - m] = islas[i][m];
                }
            }
        }

        // ---- Verificar si todas las islas se estancaron ----
        bool todas_estancadas = true;
        for (int i = 0; i < n_islas; ++i) {
            if (sin_mejora[i] < params.sin_mejora_max) {
                todas_estancadas = false;
                break;
            }
        }
        if (todas_estancadas) break;
    }

    double t_fin = omp_get_wtime();

    // ------------------------------------------------------------------
    // Recolectar el mejor individuo factible de todas las islas
    // ------------------------------------------------------------------
    Individuo mejor_global;
    mejor_global.fitness    = -1e18;
    mejor_global.es_factible = false;
    mejor_global.valor_total = 0;

    bool encontro_factible = false;

    for (const auto& isla : islas) {
        for (const auto& ind : isla) {
            if (ind.es_factible) {
                if (!encontro_factible || ind.fitness > mejor_global.fitness) {
                    mejor_global     = ind;
                    encontro_factible = true;
                }
            }
        }
    }

    // Si no hay factible, tomar el de mayor fitness general
    if (!encontro_factible) {
        for (const auto& isla : islas) {
            for (const auto& ind : isla) {
                if (ind.fitness > mejor_global.fitness) {
                    mejor_global = ind;
                }
            }
        }
    }

    GAResult resultado;
    resultado.mejor               = mejor_global;
    resultado.generaciones_usadas = gen_usadas;
    resultado.tiempo_ms           = (t_fin - t_inicio) * 1000.0;

    return resultado;
}
