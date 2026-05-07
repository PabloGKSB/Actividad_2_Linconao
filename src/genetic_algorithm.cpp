// =============================================================================
// genetic_algorithm.cpp
// Implementación del algoritmo genético secuencial (Variante 1) y paralelo
// con OpenMP (Variante 2) para el problema de la mochila extendida.
//
// ── Inicialización híbrida (50% greedy + 50% aleatoria) ─────────────────────
//
//   La población inicial mezcla individuos generados por heurística greedy
//   (generarIndividuoGreedy) con individuos aleatorios (generarIndividuoAleatorio).
//   Esto es necesario porque en instancias con muchas incompatibilidades, la
//   inicialización 100% aleatoria produce cromosomas con cientos de violaciones
//   y fitness muy negativos (-1.5M en medium), creando un pozo de penalización
//   del que el AG estocástico no puede escapar. La mitad greedy garantiza la
//   presencia de soluciones factibles desde la generación 0.
//
// ── Variante 2 — Análisis completo de paralelismo ───────────────────────────
//
//  ZONA A: Evaluación de fitness (#pragma omp parallel for schedule(dynamic))
//
//    Por qué es paralelizable:
//      fitness(X_i) depende únicamente del cromosoma i y de inst (const).
//      No hay dependencia de datos entre individuos: fitness(X_i) no lee
//      ni modifica fitness(X_j) para ningún j ≠ i.
//
//    Variables PRIVADAS (cada hilo tiene su copia):
//      - i              : índice de inducción del bucle.
//      - Todos los cálculos intermedios dentro de calcularFitness (peso_total,
//        vol_total, excesos, contadores de violaciones, etc.).
//
//    Variables COMPARTIDAS:
//      - pool[]         : compartido en LECTURA (pool[i].cromosoma) y ESCRITURA
//                         acotada (pool[i].fitness, pool[i].es_factible).
//                         Sin race condition: hilo i solo escribe en pool[i].
//      - inst           : solo lectura (const ProblemInstance&). Múltiples hilos
//                         pueden leerla simultáneamente sin sincronización.
//
//    schedule(dynamic): distribuye iteraciones dinámicamente. Útil porque el
//      costo de calcularFitness varía según cuántas restricciones activa cada
//      cromosoma (más ítems seleccionados → más incompatibilidades a revisar).
//
//  ZONA B: Generación de hijos — cruzamiento + mutación
//          (#pragma omp parallel for schedule(static))
//
//    Por qué es paralelizable:
//      Cada pareja p = (2p, 2p+1) produce hijos independientes de las demás
//      parejas. No hay dependencia de datos entre parejas distintas.
//
//    Variables PRIVADAS:
//      - p              : índice de pareja.
//      - tid            : ID del hilo (omp_get_thread_num()).
//      - rng_local      : referencia al mt19937 del hilo (rngs[tid]).
//                         CLAVE: cada hilo tiene su propio generador con semilla
//                         seed + thread_id. Si todos usaran el mismo RNG,
//                         habría race condition en la actualización del estado
//                         interno del generador (Marsaglia, 2003).
//      - padre1, padre2 : referencias const a poblacion[] (solo lectura).
//      - h1, h2         : objetos Individuo locales al hilo.
//      - prob           : distribución local al hilo.
//
//    Variables COMPARTIDAS:
//      - poblacion[]    : solo lectura durante selección. Ningún hilo la modifica
//                         en esta fase → sin race condition.
//      - hijos[]        : compartido en ESCRITURA acotada. Hilo p solo escribe
//                         en hijos[2*p] y hijos[2*p+1]. Como 2*p y 2*p+1 son
//                         únicos para cada p, no hay escritura cruzada.
//
//    schedule(static): distribución estática porque el costo por pareja es
//      homogéneo (cruzamiento un punto + mutación tienen costo O(n) fijo).
//
//  ZONA C: Selección por torneo (ejecutada dentro de ZONA B)
//
//    Por qué es paralelizable:
//      Cada torneo es una lectura independiente de poblacion[] con k accesos
//      aleatorios. No hay escritura en poblacion[] durante esta fase.
//
//    Variables PRIVADAS:
//      - rng_local      : privado al hilo (ver ZONA B). Resuelve el data race
//                         en el generador aleatorio que tendría un RNG compartido.
//      - mejor_idx, idx : índices locales al torneo.
//
//    Variables COMPARTIDAS:
//      - poblacion[]    : solo lectura → sin race condition.
//
//  Race conditions eliminadas:
//    1. RNG compartido     → RESUELTA con rngs[thread_id].
//    2. Escritura en hijos → RESUELTA con índices exclusivos 2*p, 2*p+1.
//    3. Lectura de padres  → NO HAY (poblacion[] es solo lectura en ZONA B/C).
//    4. Evaluación fitness → RESUELTA porque pool[i] solo lo escribe hilo i.
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
// generarIndividuoGreedy
// Construye un individuo factible mediante heurística greedy:
//   - Ordena ítems por valor/peso descendente (con shuffle parcial para diversidad).
//   - Agrega ítems respetando peso, volumen e incompatibilidades.
// Esto garantiza que la población inicial contenga individuos factibles,
// lo que acelera enormemente la convergencia cuando hay muchas incompatibilidades.
// ---------------------------------------------------------------------------
Individuo generarIndividuoGreedy(const ProblemInstance& inst, std::mt19937& rng) {
    const int n = static_cast<int>(inst.items.size());

    // Ordenar por ratio valor/peso con perturbación aleatoria para diversidad
    std::vector<int> orden(n);
    std::iota(orden.begin(), orden.end(), 0);
    // Añadir ruido: shuffle de bloques del 20% para diversidad entre individuos
    int bloque = std::max(1, n / 5);
    for (int b = 0; b < n; b += bloque) {
        int fin = std::min(b + bloque, n);
        std::shuffle(orden.begin() + b, orden.begin() + fin, rng);
    }
    // Ordenar bloques por valor/peso
    std::sort(orden.begin(), orden.end(), [&](int a, int b_idx) {
        double ra = (double)inst.items[a].valor / std::max(1, inst.items[a].peso);
        double rb = (double)inst.items[b_idx].valor / std::max(1, inst.items[b_idx].peso);
        return ra > rb;
    });

    // Construir solución greedy
    Individuo ind;
    ind.cromosoma.assign(n, 0);
    ind.fitness = 0.0; ind.es_factible = false; ind.valor_total = 0;

    // Índice de incompatibilidades para lookup O(1)
    std::vector<std::vector<int>> adj(n);
    for (const auto& [a, b] : inst.incompatibilities) {
        adj[a].push_back(b);
        adj[b].push_back(a);
    }

    long long peso_usado = 0;
    long long vol_usado  = 0;

    for (int i : orden) {
        const auto& item = inst.items[i];
        if (peso_usado + item.peso > inst.W) continue;
        if (vol_usado  + item.volumen > inst.V) continue;
        // Verificar incompatibilidades
        bool incompatible = false;
        for (int j : adj[i]) {
            if (ind.cromosoma[j] == 1) { incompatible = true; break; }
        }
        if (incompatible) continue;
        // Verificar dependencias: si i requiere j, asegurar que j también se incluya.
        // Si j no puede incluirse (peso/vol/incomp), no incluimos i.
        bool dep_ok = true;
        for (const auto& [dep_item, dep_req] : inst.dependencies) {
            if (dep_item == i && ind.cromosoma[dep_req] == 0) {
                // Intentar agregar el requerido primero
                const auto& req = inst.items[dep_req];
                bool req_incomp = false;
                for (int jj : adj[dep_req]) {
                    if (ind.cromosoma[jj] == 1) { req_incomp = true; break; }
                }
                if (!req_incomp && peso_usado + req.peso <= inst.W && vol_usado + req.volumen <= inst.V) {
                    ind.cromosoma[dep_req] = 1;
                    peso_usado += req.peso;
                    vol_usado  += req.volumen;
                } else {
                    dep_ok = false; break;
                }
            }
        }
        if (!dep_ok) continue;
        ind.cromosoma[i] = 1;
        peso_usado += item.peso;
        vol_usado  += item.volumen;
    }
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
    // Inicializar población (50% greedy + 50% aleatoria)
    // La mitad greedy garantiza individuos factibles desde la gen 0,
    // lo que es critico con muchas incompatibilidades (medium/large).
    // La mitad aleatoria mantiene diversidad genetica.
    // ------------------------------------------------------------------
    std::vector<Individuo> poblacion(params.pop_size);
    {
        int n_greedy = params.pop_size / 2;
        for (int i = 0; i < params.pop_size; ++i) {
            poblacion[i] = (i < n_greedy)
                ? generarIndividuoGreedy(inst, rng)
                : generarIndividuoAleatorio(n, rng);
            calcularFitness(poblacion[i], inst);
        }
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
    // Inicializar población (50% greedy + 50% aleatoria, secuencial)
    // ------------------------------------------------------------------
    std::vector<Individuo> poblacion(params.pop_size);
    {
        int n_greedy = params.pop_size / 2;
        for (int i = 0; i < params.pop_size; ++i) {
            poblacion[i] = (i < n_greedy)
                ? generarIndividuoGreedy(inst, rng_master)
                : generarIndividuoAleatorio(n, rng_master);
        }
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