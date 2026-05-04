#!/bin/bash
# =============================================================================
# run_experiments.sh
# Script de experimentos para el problema de la mochila extendida.
#
# Ejecuta todas las combinaciones:
#   3 variantes × 3 instancias × 4 configuraciones de hilos × 15 semillas
#   Total: 540 ejecuciones
#
# Uso:
#   chmod +x scripts/run_experiments.sh
#   bash scripts/run_experiments.sh
# =============================================================================

set -e  # Salir si algún comando falla

# ---------------------------------------------------------------------------
# Colores para salida en consola
# ---------------------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
RESET='\033[0m'

log()    { echo -e "${GREEN}[OK]${RESET}  $*"; }
info()   { echo -e "${CYAN}[INFO]${RESET} $*"; }
warn()   { echo -e "${YELLOW}[WARN]${RESET} $*"; }
error()  { echo -e "${RED}[ERROR]${RESET} $*"; exit 1; }

echo -e "${CYAN}============================================================${RESET}"
echo -e "${CYAN}  Experimentos: Mochila GA con OpenMP                       ${RESET}"
echo -e "${CYAN}============================================================${RESET}"

# ---------------------------------------------------------------------------
# PASO 1: Compilación
# ---------------------------------------------------------------------------
info "Compilando mochila_ga ..."
g++ -O2 -fopenmp -std=c++17 src/*.cpp \
    $(ls src/*.cpp | grep -v generate_instance) \
    -o mochila_ga 2>/dev/null || \
g++ -O2 -fopenmp -std=c++17 \
    src/instance_loader.cpp \
    src/fitness.cpp \
    src/genetic_algorithm.cpp \
    src/island_model.cpp \
    src/main.cpp \
    -o mochila_ga
log "mochila_ga compilado."

info "Compilando generate_instance ..."
g++ -O2 -std=c++17 src/generate_instance.cpp -o generate_instance
log "generate_instance compilado."

# ---------------------------------------------------------------------------
# PASO 2: Generar instancias
# ---------------------------------------------------------------------------
info "Generando instancias ..."

./generate_instance --size 100   --output data/small  --seed 42
log "Instancia small  (100 ítems) generada."

./generate_instance --size 1000  --output data/medium --seed 42
log "Instancia medium (1000 ítems) generada."

./generate_instance --size 10000 --output data/large  --seed 42
log "Instancia large  (10000 ítems) generada."

# ---------------------------------------------------------------------------
# PASO 3: Configuración del experimento
# ---------------------------------------------------------------------------
VARIANTES=("sequential" "parallel" "islands")
INSTANCIAS=("data/small" "data/medium" "data/large")
HILOS=(1 2 4 8)
SEMILLAS=(1 2 3 4 5 6 7 8 9 10 11 12 13 14 15)
GENERACIONES=500
POBLACION=200

TOTAL=$(( ${#VARIANTES[@]} * ${#INSTANCIAS[@]} * ${#HILOS[@]} * ${#SEMILLAS[@]} ))
ACTUAL=0

echo ""
info "Total de ejecuciones: $TOTAL"
echo ""

# ---------------------------------------------------------------------------
# PASO 4: Bucle de experimentos
# ---------------------------------------------------------------------------
for variante in "${VARIANTES[@]}"; do
    for instancia in "${INSTANCIAS[@]}"; do
        inst_name=$(basename "$instancia")
        for hilos in "${HILOS[@]}"; do
            for semilla in "${SEMILLAS[@]}"; do
                ACTUAL=$(( ACTUAL + 1 ))
                PORCENT=$(( ACTUAL * 100 / TOTAL ))

                printf "${CYAN}[%3d/%d | %3d%%]${RESET} variante=%-10s instancia=%-8s hilos=%d semilla=%2d\n" \
                       "$ACTUAL" "$TOTAL" "$PORCENT" \
                       "$variante" "$inst_name" "$hilos" "$semilla"

                ./mochila_ga \
                    --instance    "$instancia" \
                    --variant     "$variante" \
                    --threads     "$hilos" \
                    --seed        "$semilla" \
                    --generations "$GENERACIONES" \
                    --population  "$POBLACION" \
                    > /dev/null 2>&1

                if [ $? -ne 0 ]; then
                    warn "Fallo en: variante=$variante instancia=$inst_name hilos=$hilos semilla=$semilla"
                fi
            done
        done
    done
done

# ---------------------------------------------------------------------------
# PASO 5: Resumen
# ---------------------------------------------------------------------------
echo ""
echo -e "${GREEN}============================================================${RESET}"
echo -e "${GREEN}  Experimentos completados: $ACTUAL/$TOTAL ejecuciones      ${RESET}"
echo -e "${GREEN}  Resultados guardados en:  results/resultados.csv          ${RESET}"
echo -e "${GREEN}============================================================${RESET}"

# Mostrar las últimas 5 filas del CSV como vista previa
echo ""
info "Vista previa de resultados (últimas 5 filas):"
tail -n 6 results/resultados.csv
