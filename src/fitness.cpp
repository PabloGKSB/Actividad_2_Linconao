// =============================================================================
// fitness.cpp
// Implementación de la función de aptitud para el problema de la mochila
// extendida con restricciones duras y blandas.
// =============================================================================
#include "fitness.hpp"
#include <algorithm>
#include <unordered_map>

// ---------------------------------------------------------------------------
// calcularFitness
// Evalúa un individuo contra todas las restricciones del problema y
// asigna su aptitud, valor total y bandera de factibilidad.
// ---------------------------------------------------------------------------
void calcularFitness(Individuo& ind, const ProblemInstance& inst) {
    const int n = static_cast<int>(inst.items.size());
    const auto& crom = ind.cromosoma;

    // ------------------------------------------------------------------
    // 1. Calcular métricas básicas: valor, peso y volumen totales
    // ------------------------------------------------------------------
    long long peso_total   = 0;
    long long vol_total    = 0;
    int       valor_total  = 0;

    for (int i = 0; i < n; ++i) {
        if (crom[i]) {
            valor_total += inst.items[i].valor;
            peso_total  += inst.items[i].peso;
            vol_total   += inst.items[i].volumen;
        }
    }

    // ------------------------------------------------------------------
    // 2. Restricciones duras: peso y volumen (excesos continuos)
    // ------------------------------------------------------------------
    long long exceso_peso   = std::max(0LL, peso_total - inst.W);
    long long exceso_vol    = std::max(0LL, vol_total  - inst.V);

    // ------------------------------------------------------------------
    // 3. Restricciones de categoría (blandas → penalizadas)
    //    Contar cuántos ítems seleccionados hay por categoría
    // ------------------------------------------------------------------
    std::unordered_map<int,int> cuenta_cat;
    for (int i = 0; i < n; ++i) {
        if (crom[i]) {
            cuenta_cat[inst.items[i].categoria]++;
        }
    }

    int violaciones_cat = 0;
    for (const auto& rule : inst.category_rules) {
        int cnt = 0;
        auto it = cuenta_cat.find(rule.categoria);
        if (it != cuenta_cat.end()) cnt = it->second;

        if (cnt < rule.minimo) violaciones_cat += (rule.minimo - cnt);
        if (cnt > rule.maximo) violaciones_cat += (cnt - rule.maximo);
    }

    // ------------------------------------------------------------------
    // 4. Incompatibilidades (restricciones duras)
    //    Contar pares de ítems incompatibles que están ambos seleccionados
    // ------------------------------------------------------------------
    int incomp_violadas = 0;
    for (const auto& [a, b] : inst.incompatibilities) {
        if (crom[a] && crom[b]) {
            ++incomp_violadas;
        }
    }

    // ------------------------------------------------------------------
    // 5. Dependencias (restricciones duras)
    //    Si crom[item]=1 pero crom[required]=0 → violación
    // ------------------------------------------------------------------
    int deps_incumplidas = 0;
    for (const auto& [item, required] : inst.dependencies) {
        if (crom[item] && !crom[required]) {
            ++deps_incumplidas;
        }
    }

    // ------------------------------------------------------------------
    // 6. Factibilidad: cumple restricciones DURAS
    //    Duras: peso, volumen, incompatibilidades, dependencias
    //    Blandas: categorías (solo penalizadas)
    // ------------------------------------------------------------------
    ind.es_factible = (exceso_peso == 0) &&
                      (exceso_vol  == 0) &&
                      (incomp_violadas == 0) &&
                      (deps_incumplidas == 0);

    // ------------------------------------------------------------------
    // 7. Calcular fitness como función de penalización
    // ------------------------------------------------------------------
    ind.valor_total = valor_total;
    ind.fitness = static_cast<double>(valor_total)
                  - FitnessParams::ALPHA   * static_cast<double>(exceso_peso)
                  - FitnessParams::BETA    * static_cast<double>(exceso_vol)
                  - FitnessParams::GAMMA   * static_cast<double>(violaciones_cat)
                  - FitnessParams::DELTA   * static_cast<double>(incomp_violadas)
                  - FitnessParams::EPSILON * static_cast<double>(deps_incumplidas);
}
