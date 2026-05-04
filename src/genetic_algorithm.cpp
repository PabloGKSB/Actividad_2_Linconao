// =============================================================================
// genetic_algorithm.cpp
// Implementación del algoritmo genético secuencial (Variante 1) y paralelo
// con OpenMP (Variante 2) para el problema de la mochila extendida.
//
// Variante 2 – Zonas paralelizadas y justificación:
//
//  A) Evaluación de fitness (#pragma omp parallel for schedule(dynamic))
//     - Cada evaluación es INDEPENDIENTE: fitness(X_i) no depende de X_j.
//     - Variables privadas: índice i, cálculos intermedios de calcularFitness.
//     - Variables compartidas (solo lectura): pool[], inst (const).
//     - Sin race condition garantizado porque cada hilo escribe en pool[i] distinto.
//     - schedule(dynamic) equilibra carga si los cromosomas tienen distinto costo.
//
//  B) Generación de hijos (#pragma omp parallel for schedule(static))
//     - Cada pareja (2k, 2k+1) produce sus hijos de forma independiente.
//     - Cada hilo usa su PROPIO mt19937 (semilla = seed + thread_id) para evitar
//       data races en el generador aleatorio.
//     - Cada hilo escribe en índices distintos del vector hijos[].
//     - schedule(static) es adecuado porque el costo es homogéneo.
//
//  C) Selección por torneo (cada torneo es independiente)
//     - Cada torneo lee la población (solo lectura) y escribe en padres[i] propio.
//     - Variables privadas: índice i, rng_local, índices de candidatos.
//     - Sin escritura compartida: padres[i] solo lo escribe el hilo i.
// =============================================================================
#include "genetic_algorithm.hpp"
#include <algorithm>
#include <numeric>
#include <omp.h>
#include <stdexcept>
#include <iostream>

// ---------------------------------------------------------------------------
// generarIndividuoAleatorio
// ---------------------------------------------------------------------------
Individuo generarIndividuoAleatorio(int n, std::mt19937& rng) {
    Individuo ind;
    ind.cromosoma.resize(n);
    std::uniform_int_distribution<int> dist(0, 1);
    for (int i = 0; i < n; ++i) {
        ind.cromosoma[i] = dist(rng);
    }
    ind.fitness      = 0.0;
    ind.es_factible  = false;
    ind.valor_total  = 0;
    return ind;
}

// ---------------------------------------------------------------------------
// seleccionTorneo
// ---------------------------------------------------------------------------
const Individuo& seleccionTorneo(const std::vector<Individuo>& poblacion,
                                  int k, std::mt19937& rng) {
    int n = static_cast<int>(poblacion.size());
    std::uniform_int_distribution<int> dist(0, n - 1);

    int mejor_idx = dist(rng);
    for (int i = 1; i < k; ++i) {
        int idx = dist(rng);
        if (poblacion[idx].fitness > poblacion[mejor_idx].fitness) {
            mejor_idx = idx;
        }
    }
    return poblacion[mejor_idx];
}

// ---------------------------------------------------------------------------
// cruzamientoUnPunto
// ---------------------------------------------------------------------------
void cruzamientoUnPunto(const Individuo& padre1, const Individuo& padre2,
                         Individuo& hijo1, Individuo& hijo2,
                         std::mt19937& rng) {
    int n = static_cast<int>(padre1.cromosoma.size());
    std::uniform_int_distribution<int> dist(1, n - 1);
    int punto = dist(rng);

    hijo1.cromosoma.resize(n);
    hijo2.cromosoma.resize(n);

    for (int i = 0; i < punto; ++i) {
        hijo1.cromosoma[i] = padre1.cromosoma[i];
        hijo2.cromosoma[i] = padre2.cromosoma[i];
    }
    for (int i = punto; i < n; ++i) {
        hijo1.cromosoma[i] = padre2.cromosoma[i];
        hijo2.cromosoma[i] = padre1.cromosoma[i];
    }

    hijo1.fitness = 0.0; hijo1.es_factible = false; hijo1.valor_total = 0;
    hijo2.fitness = 0.0; hijo2.es_factible = false; hijo2.valor_total = 0;
}

// ---------------------------------------------------------------------------
// mutacion – bit flip con probabilidad prob_mutacion por bit
// ---------------------------------------------------------------------------
void mutacion(Individuo& ind, double prob_mutacion, std::mt19937& rng) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (int& bit : ind.cromosoma) {
        if (dist(rng) < prob_mutacion) {
            bit = 1 - bit;
        }
    }
}

// ---------------------------------------------------------------------------
// Función auxiliar: encontrar el mejor individuo factible en un vector.
// Retorna nullptr si no hay ninguno factible.
// ---------------------------------------------------------------------------
static const Individuo* mejorFactible(const std::vector<Individuo>& pop) {
    const Individuo* best = nullptr;
    for (const auto& ind : pop) {
        if (ind.es_factible) {
            if (!best || ind.fitness > best->fitness) {
                best = &ind;
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Función auxiliar: elitismo combinado (pool = padres + hijos, top N)
// ---------------------------------------------------------------------------
static std::vector<Individuo> elitismo(std::vector<Individuo>& pool, int pop_size) {
    // Ordenar por fitness descendente
    std::sort(pool.begin(), pool.end(),
              [](const Individuo& a, const Individuo& b) {
                  return a.fitness > b.fitness;
              });
    pool.resize(pop_size);
    return pool;
}

// =============================================================================
// VARIANTE 1: ALGORITMO GENÉTICO SECUENCIAL
// =============================================================================
GAResult runSequential(const ProblemInstance& inst, const GAParams& params) {
    const int n = static_cast<int>(inst.items.size());

    // RNG principal con la semilla indicada
    std::mt19937 rng(params.seed);

    // ------------------------------------------------------------------
    // Inicializar población
    // ------------------------------------------------------------------
    std::vector<Individuo> poblacion(params.pop_size);
    for (auto& ind : poblacion) {
        ind = generarIndividuoAleatorio(n, rng);
        calcularFitness(ind, inst);
    }

    // Historial del mejor factible para la solución final
    Individuo mejor_factible_global;
    mejor_factible_global.fitness = -1e18;
    bool encontro_factible = false;

    double mejor_fitness_gen = -1e18;
    int sin_mejora           = 0;
    int gen_usadas           = 0;

    double t_inicio = omp_get_wtime();

    // ------------------------------------------------------------------
    // Bucle principal de generaciones
    // ------------------------------------------------------------------
    for (int gen = 0; gen < params.max_generaciones; ++gen) {
        gen_usadas = gen + 1;

        // ---- Actualizar mejor factible global ----
        for (const auto& ind : poblacion) {
            if (ind.es_factible && ind.fitness > mejor_factible_global.fitness) {
                mejor_factible_global = ind;
                encontro_factible = true;
            }
        }

        // ---- Criterio de parada por estancamiento ----
        double best_now = std::max_element(poblacion.begin(), poblacion.end(),
            [](const Individuo& a, const Individuo& b) {
                return a.fitness < b.fitness;
            })->fitness;

        if (best_now > mejor_fitness_gen) {
            mejor_fitness_gen = best_now;
            sin_mejora = 0;
        } else {
            ++sin_mejora;
        }
        if (sin_mejora >= params.sin_mejora_max) break;

        // ---- Selección + Cruzamiento + Mutación ----
        std::vector<Individuo> hijos;
        hijos.reserve(params.pop_size);

        while (static_cast<int>(hijos.size()) < params.pop_size) {
            const Individuo& p1 = seleccionTorneo(poblacion, params.torneo_k, rng);
            const Individuo& p2 = seleccionTorneo(poblacion, params.torneo_k, rng);

            Individuo h1, h2;
            std::uniform_real_distribution<double> prob(0.0, 1.0);

            if (prob(rng) < params.prob_cruzamiento) {
                cruzamientoUnPunto(p1, p2, h1, h2, rng);
            } else {
                h1 = p1;
                h2 = p2;
            }

            mutacion(h1, params.prob_mutacion, rng);
            mutacion(h2, params.prob_mutacion, rng);

            calcularFitness(h1, inst);
            calcularFitness(h2, inst);

            hijos.push_back(std::move(h1));
            if (static_cast<int>(hijos.size()) < params.pop_size) {
                hijos.push_back(std::move(h2));
            }
        }

        // ---- Elitismo: pool = padres + hijos, top pop_size ----
        std::vector<Individuo> pool;
        pool.reserve(params.pop_size * 2);
        for (auto& ind : poblacion) pool.push_back(ind);
        for (auto& ind : hijos)     pool.push_back(ind);

        poblacion = elitismo(pool, params.pop_size);
    }

    double t_fin = omp_get_wtime();

    // ------------------------------------------------------------------
    // Elegir solución final: mejor factible o mejor de la última gen
    // ------------------------------------------------------------------
    // Revisar última generación en busca de factible
    for (const auto& ind : poblacion) {
        if (ind.es_factible && ind.fitness > mejor_factible_global.fitness) {
            mejor_factible_global = ind;
            encontro_factible = true;
        }
    }

    GAResult resultado;
    resultado.generaciones_usadas = gen_usadas;
    resultado.tiempo_ms = (t_fin - t_inicio) * 1000.0;

    if (encontro_factible) {
        resultado.mejor = mejor_factible_global;
    } else {
        // Sin factible: reportar el de mayor fitness de la última generación
        resultado.mejor = *std::max_element(poblacion.begin(), poblacion.end(),
            [](const Individuo& a, const Individuo& b) {
                return a.fitness < b.fitness;
            });
    }

    return resultado;
}

// =============================================================================
// VARIANTE 2: ALGORITMO GENÉTICO PARALELO CON OpenMP
// =============================================================================
GAResult runParallel(const ProblemInstance& inst, const GAParams& params) {
    const int n = static_cast<int>(inst.items.size());

    // Número de hilos disponibles (configurado externamente vía omp_set_num_threads)
    int num_threads = omp_get_max_threads();

    // ------------------------------------------------------------------
    // Crear un mt19937 por hilo para evitar data races en el RNG.
    // Semilla de cada hilo = seed_base + thread_id
    // ------------------------------------------------------------------
    std::vector<std::mt19937> rngs(num_threads);
    for (int t = 0; t < num_threads; ++t) {
        rngs[t].seed(params.seed + static_cast<unsigned int>(t));
    }

    // RNG del hilo maestro para inicialización secuencial
    std::mt19937 rng_master(params.seed);

    // ------------------------------------------------------------------
    // Inicializar población (secuencial; costo mínimo comparado con el AG)
    // ------------------------------------------------------------------
    std::vector<Individuo> poblacion(params.pop_size);
    for (auto& ind : poblacion) {
        ind = generarIndividuoAleatorio(n, rng_master);
    }

    // ---- Evaluación inicial de fitness (paralelo) ----
    // ZONA PARALELA A: cada individuo se evalúa independientemente.
    // Variables compartidas (solo lectura): poblacion[], inst.
    // Variables privadas: i, cálculos internos de calcularFitness.
    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < params.pop_size; ++i) {
        calcularFitness(poblacion[i], inst);
    }

    Individuo mejor_factible_global;
    mejor_factible_global.fitness = -1e18;
    bool encontro_factible = false;

    double mejor_fitness_gen = -1e18;
    int sin_mejora           = 0;
    int gen_usadas           = 0;

    double t_inicio = omp_get_wtime();

    // ------------------------------------------------------------------
    // Bucle principal de generaciones
    // ------------------------------------------------------------------
    for (int gen = 0; gen < params.max_generaciones; ++gen) {
        gen_usadas = gen + 1;

        // ---- Actualizar mejor factible global (secuencial; lectura de flags) ----
        for (const auto& ind : poblacion) {
            if (ind.es_factible && ind.fitness > mejor_factible_global.fitness) {
                mejor_factible_global = ind;
                encontro_factible = true;
            }
        }

        // ---- Criterio de parada ----
        double best_now = std::max_element(poblacion.begin(), poblacion.end(),
            [](const Individuo& a, const Individuo& b) {
                return a.fitness < b.fitness;
            })->fitness;

        if (best_now > mejor_fitness_gen) {
            mejor_fitness_gen = best_now;
            sin_mejora = 0;
        } else {
            ++sin_mejora;
        }
        if (sin_mejora >= params.sin_mejora_max) break;

        // ---- ZONA PARALELA B: Selección + Cruzamiento + Mutación ----
        // Generamos params.pop_size hijos en pares (i*2, i*2+1).
        // Cada hilo escribe en índices propios: no hay escritura compartida.
        // Cada hilo usa rngs[omp_get_thread_num()] → sin data race en RNG.
        int n_parejas = params.pop_size / 2;
        std::vector<Individuo> hijos(params.pop_size);

        #pragma omp parallel for schedule(static)
        for (int p = 0; p < n_parejas; ++p) {
            int tid = omp_get_thread_num();
            std::mt19937& rng_local = rngs[tid];

            // Selección por torneo (ZONA PARALELA C)
            // poblacion[] es de solo lectura aquí → sin race condition.
            const Individuo& padre1 = seleccionTorneo(poblacion, params.torneo_k, rng_local);
            const Individuo& padre2 = seleccionTorneo(poblacion, params.torneo_k, rng_local);

            Individuo h1, h2;
            std::uniform_real_distribution<double> prob(0.0, 1.0);

            if (prob(rng_local) < params.prob_cruzamiento) {
                cruzamientoUnPunto(padre1, padre2, h1, h2, rng_local);
            } else {
                h1 = padre1;
                h2 = padre2;
            }

            mutacion(h1, params.prob_mutacion, rng_local);
            mutacion(h2, params.prob_mutacion, rng_local);

            // Escritura en índices exclusivos: hijos[2*p] y hijos[2*p+1]
            hijos[2 * p]     = std::move(h1);
            hijos[2 * p + 1] = std::move(h2);
        }

        // ---- ZONA PARALELA A: Evaluación del pool (padres + hijos) ----
        // Construimos el pool combinado
        std::vector<Individuo> pool;
        pool.reserve(params.pop_size * 2);
        for (auto& ind : poblacion) pool.push_back(ind);
        for (auto& ind : hijos)     pool.push_back(ind);

        int pool_size = static_cast<int>(pool.size());

        // Evaluar hijos (los padres ya tienen fitness calculado)
        // Solo evaluamos la segunda mitad del pool (los hijos)
        #pragma omp parallel for schedule(dynamic)
        for (int i = params.pop_size; i < pool_size; ++i) {
            calcularFitness(pool[i], inst);
        }

        poblacion = elitismo(pool, params.pop_size);
    }

    double t_fin = omp_get_wtime();

    // ---- Revisar última generación ----
    for (const auto& ind : poblacion) {
        if (ind.es_factible && ind.fitness > mejor_factible_global.fitness) {
            mejor_factible_global = ind;
            encontro_factible = true;
        }
    }

    GAResult resultado;
    resultado.generaciones_usadas = gen_usadas;
    resultado.tiempo_ms = (t_fin - t_inicio) * 1000.0;

    if (encontro_factible) {
        resultado.mejor = mejor_factible_global;
    } else {
        resultado.mejor = *std::max_element(poblacion.begin(), poblacion.end(),
            [](const Individuo& a, const Individuo& b) {
                return a.fitness < b.fitness;
            });
    }

    return resultado;
}
