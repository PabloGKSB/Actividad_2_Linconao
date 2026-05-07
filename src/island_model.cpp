// =============================================================================
// island_model.cpp
// Implementación del Modelo de Islas (Variante 3) con OpenMP.
//
// ── Diseño de paralelismo ────────────────────────────────────────────────────
//
//  FASE A — Evolución paralela de islas (#pragma omp parallel for)
//    Cada isla corre en un hilo OpenMP distinto durante las generaciones
//    entre migraciones. No hay comunicación entre islas en esta fase.
//
//    Variables PRIVADAS por hilo (cada hilo tiene su propia copia):
//      - i         : índice de isla (variable de inducción del bucle)
//      - rngs[i]   : generador mt19937 exclusivo de la isla i
//      - islas[i]  : población de la isla i (escritura exclusiva)
//      - sin_mejora[i], mejor_fit_acum[i] : estado de parada de la isla i
//
//    Variables COMPARTIDAS (solo lectura en esta fase):
//      - inst      : instancia del problema (const, nunca modificada)
//      - params    : parámetros del algoritmo (const, nunca modificados)
//
//    Sin race condition garantizado porque:
//      - Cada hilo i accede únicamente a islas[i], rngs[i], sin_mejora[i]
//        y mejor_fit_acum[i]; ningún otro hilo toca esos índices.
//      - inst y params son de solo lectura → múltiples hilos pueden leerlos
//        simultáneamente sin sincronización.
//      - schedule(static) distribuye las islas equitativamente; con n_islas=4
//        y 4 hilos, cada hilo recibe exactamente 1 isla → carga perfectamente
//        balanceada.
//
//  FASE B — Selección por torneo dentro de evolucionarIsla
//    Ejecutada de forma SECUENCIAL dentro de cada hilo (la función
//    evolucionarIsla no usa OpenMP internamente). Por lo tanto:
//
//    - No hay concurrencia en la selección por torneo: un único hilo ejecuta
//      todos los torneos de su isla, usando su propio rng privado.
//    - isla[] es leída y escrita solo por el hilo propietario → sin race.
//    - No se requieren directivas adicionales de sincronización.
//
//    Si en el futuro se paralelizara la generación de hijos dentro de cada
//    isla (segundo nivel de paralelismo), habría que replicar la estrategia
//    de la Variante 2: un mt19937 adicional por sub-hilo y escritura en
//    índices exclusivos del vector hijos[].
//
//  FASE C — Migración en anillo (#pragma omp critical)
//    La migración ocurre FUERA del bucle paralelo (después de que todos los
//    hilos terminaron sus generaciones). Se ejecuta en una sección crítica
//    para evitar race conditions al acceder a islas vecinas.
//
//    Sin #pragma omp critical, dos hilos podrían leer/escribir en la misma
//    isla simultáneamente durante la migración (isla i escribe en islas[i+1]
//    mientras isla i+1 intenta leer de islas[i+1] para su propia migración).
//
//    La sección crítica serializa la migración completa, lo cual es correcto
//    porque la migración es O(n_islas × n_migrantes) y su costo es despreciable
//    frente al costo de evolucionar las islas (O(pop × gen × n)).
//
// ── Análisis de race conditions ─────────────────────────────────────────────
//
//  Posible race condition 1: RNG compartido
//    RESUELTA: cada isla i usa rngs[i] con semilla = base_seed + i.
//    Los rngs son elementos distintos del vector → sin acceso cruzado.
//
//  Posible race condition 2: escritura en islas vecinas durante evolución
//    RESUELTA: evolucionarIsla solo escribe en islas[i] (su propio índice).
//    No hay escritura en islas[j] para j ≠ i en la fase de evolución.
//
//  Posible race condition 3: migración sin sincronización
//    RESUELTA: la migración usa #pragma omp critical, garantizando que solo
//    un hilo a la vez lee y escribe en el vector islas[] durante el intercambio.
//
//  Posible race condition 4: sin_mejora[] y mejor_fit_acum[]
//    RESUELTA: sin_mejora[i] y mejor_fit_acum[i] son escritos exclusivamente
//    por el hilo i. No se accede desde otros hilos durante la fase paralela.
// =============================================================================
#include "island_model.hpp"
#include <algorithm>
#include <omp.h>
#include <iostream>

// ---------------------------------------------------------------------------
// evolucionarIsla
// Hace evolucionar una isla por 'gens' generaciones de forma SECUENCIAL.
// Es llamada desde un contexto paralelo (un hilo por isla), por lo que
// todos los recursos que usa (isla, rng, sin_mejora, mejor_fitness_acum)
// son exclusivos del hilo que la invoca → sin necesidad de sincronización
// interna.
//
// Análisis de concurrencia de la selección por torneo:
//   - seleccionTorneo() lee isla[] (solo lectura) y escribe en rng (privado).
//   - Como isla[] es propiedad exclusiva de este hilo en esta fase, no hay
//     race condition posible aunque se llame repetidamente en el bucle.
//   - Variables privadas: p, p1, p2, h1, h2, prob, best_now.
//   - Variables compartidas (solo lectura): inst, params.
//
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
    const int pop_size  = static_cast<int>(isla.size());
    const int n_parejas = pop_size / 2;

    for (int g = 0; g < gens; ++g) {
        // ---- Criterio de parada por estancamiento ----
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
        if (sin_mejora >= params.sin_mejora_max) return true;

        // ---- Selección + Cruzamiento + Mutación ----
        // seleccionTorneo() lee isla[] (solo lectura) y usa rng (privado).
        // Cada pareja escribe en hijos[2*p] y hijos[2*p+1] (índices exclusivos).
        // Sin concurrencia: este bucle es secuencial dentro del hilo de la isla.
        std::vector<Individuo> hijos(pop_size);
        for (int p = 0; p < n_parejas; ++p) {
            // Selección por torneo: isla[] es de solo lectura; rng es privado.
            // No hay race condition aunque el vector isla[] sea accedido por
            // múltiples llamadas a seleccionTorneo() en el mismo bucle,
            // porque el bucle es secuencial (un único hilo lo ejecuta).
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

        // ---- Elitismo: pool = padres + hijos, top pop_size ----
        std::vector<Individuo> pool;
        pool.reserve(pop_size * 2);
        for (auto& ind : isla)  pool.push_back(ind);
        for (auto& ind : hijos) pool.push_back(ind);

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
    const int n            = static_cast<int>(inst.items.size());
    const int n_islas      = params.n_islas;
    const int pop_por_isla = params.pop_por_isla;

    // ------------------------------------------------------------------
    // Inicializar cada isla con su propio RNG (semilla = base_seed + isla_id)
    // Esto garantiza reproducibilidad: con la misma semilla base y número
    // de islas, los resultados son idénticos en cualquier ejecución.
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

    // Estado de parada por estancamiento (uno por isla, sin acceso cruzado)
    std::vector<int>    sin_mejora(n_islas, 0);
    std::vector<double> mejor_fit_acum(n_islas, -1e18);

    int    gen_usadas = 0;
    double t_inicio   = omp_get_wtime();

    // ------------------------------------------------------------------
    // Bucle principal de épocas (cada época = frec_migracion generaciones)
    // ------------------------------------------------------------------
    int epocas = params.max_generaciones / params.frec_migracion;

    for (int epoca = 0; epoca < epocas; ++epoca) {
        gen_usadas += params.frec_migracion;

        // ---- FASE A: Evolución paralela de islas ----
        // Cada hilo i evoluciona islas[i] de forma completamente independiente.
        // Variables privadas: i, y todos los recursos internos de evolucionarIsla.
        // Variables compartidas (solo lectura): inst, params.
        // Sin race condition: hilo i solo accede a islas[i], rngs[i],
        // sin_mejora[i] y mejor_fit_acum[i].
        // num_threads(n_islas) asegura un hilo por isla (carga balanceada).
        #pragma omp parallel for schedule(static) num_threads(n_islas)
        for (int i = 0; i < n_islas; ++i) {
            evolucionarIsla(islas[i], inst, params, rngs[i],
                            params.frec_migracion,
                            sin_mejora[i], mejor_fit_acum[i]);
        }
        // Punto de sincronización implícito al final del parallel for:
        // todos los hilos terminaron sus generaciones antes de continuar.

        // ---- FASE C: Migración en anillo (sección crítica) ----
        // La sección crítica garantiza exclusión mutua durante el intercambio
        // de individuos entre islas vecinas. Sin ella, dos hilos podrían
        // modificar la misma isla simultáneamente (isla i escribe en islas[i+1]
        // mientras isla i+1 también modifica islas[i+1]).
        // El costo de la migración es O(n_islas × n_migrantes) → despreciable.
        #pragma omp critical
        {
            for (int i = 0; i < n_islas; ++i) {
                int destino = (i + 1) % n_islas;  // Topología en anillo

                // Ordenar isla origen: los mejores quedan al inicio
                std::sort(islas[i].begin(), islas[i].end(),
                          [](const Individuo& a, const Individuo& b){
                              return a.fitness > b.fitness;
                          });

                // Ordenar isla destino: los peores quedan al final
                std::sort(islas[destino].begin(), islas[destino].end(),
                          [](const Individuo& a, const Individuo& b){
                              return a.fitness > b.fitness;
                          });

                // Reemplazar los N_MIGRANTES peores del destino
                // con los N_MIGRANTES mejores del origen.
                // Criterio de selección de migrantes: elitismo (mejores N).
                // Criterio de reemplazo: peores N de la isla destino.
                int n_mig = std::min(params.n_migrantes, pop_por_isla);
                for (int m = 0; m < n_mig; ++m) {
                    islas[destino][pop_por_isla - 1 - m] = islas[i][m];
                }
            }
        }

        // ---- Verificar criterio de parada global ----
        // El algoritmo termina cuando TODAS las islas se estancaron.
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
    // Recolectar el mejor individuo factible de todas las islas.
    // Se prefiere la mejor solución factible; si no existe ninguna,
    // se reporta el individuo de mayor fitness (puede ser infactible).
    // ------------------------------------------------------------------
    Individuo mejor_global;
    mejor_global.fitness     = -1e18;
    mejor_global.es_factible = false;
    mejor_global.valor_total = 0;

    bool encontro_factible = false;

    for (const auto& isla : islas) {
        for (const auto& ind : isla) {
            if (ind.es_factible) {
                if (!encontro_factible || ind.fitness > mejor_global.fitness) {
                    mejor_global      = ind;
                    encontro_factible = true;
                }
            }
        }
    }

    // Sin factible: reportar el de mayor fitness de la última generación
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
