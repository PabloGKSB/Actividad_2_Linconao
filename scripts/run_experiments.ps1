# =============================================================================
# run_experiments.ps1
# Script de experimentos para el problema de la mochila extendida.
# Equivalente PowerShell de run_experiments.sh
#
# Ejecuta todas las combinaciones:
#   3 variantes x 3 instancias x 4 hilos x 15 semillas = 540 ejecuciones
#
# Uso (desde la raiz del proyecto):
#   .\scripts\run_experiments.ps1
#
# Si da error de politica de ejecucion:
#   Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
# =============================================================================

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Colores de consola
# ---------------------------------------------------------------------------
function Log-OK($msg)   { Write-Host "[OK]   $msg" -ForegroundColor Green }
function Log-Info($msg) { Write-Host "[INFO] $msg" -ForegroundColor Cyan }
function Log-Warn($msg) { Write-Host "[WARN] $msg" -ForegroundColor Yellow }

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Experimentos: Mochila GA con OpenMP (PowerShell)          " -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

# ---------------------------------------------------------------------------
# PASO 1: Compilacion
# ---------------------------------------------------------------------------
Log-Info "Compilando mochila_ga ..."
& g++ -O2 -fopenmp -std=c++17 `
    src/instance_loader.cpp `
    src/fitness.cpp `
    src/genetic_algorithm.cpp `
    src/island_model.cpp `
    src/main.cpp `
    -o mochila_ga
if ($LASTEXITCODE -ne 0) { throw "Error compilando mochila_ga" }
Log-OK "mochila_ga compilado."

Log-Info "Compilando generate_instance ..."
& g++ -O2 -std=c++17 src/generate_instance.cpp -o generate_instance
if ($LASTEXITCODE -ne 0) { throw "Error compilando generate_instance" }
Log-OK "generate_instance compilado."

# ---------------------------------------------------------------------------
# PASO 2: Generar instancias
#
# NOTA sobre --incomp-rate:
#   small  (100 items):    sin parametro → usa default 0.002 (~0 pares, trivial)
#                          se usa 0.02 para mantener algo de dificultad en small.
#   medium (1.000 items):  0.002 → ~999 pares (~2 por item). El valor original
#                          0.02 generaba ~10.000 pares (~20 por item), creando
#                          un grafo tan denso que el AG nunca encontraba factibles.
#   large  (10.000 items): 0.001 → ~10.000 pares (~2 por item).
# ---------------------------------------------------------------------------
Log-Info "Generando instancias ..."

& .\generate_instance --size 100   --output data/small  --seed 42 --incomp-rate 0.02
if ($LASTEXITCODE -ne 0) { throw "Error generando instancia small" }
Log-OK "Instancia small  (100 items) generada."

& .\generate_instance --size 1000  --output data/medium --seed 42 --incomp-rate 0.002
if ($LASTEXITCODE -ne 0) { throw "Error generando instancia medium" }
Log-OK "Instancia medium (1000 items) generada."

& .\generate_instance --size 10000 --output data/large  --seed 42 --incomp-rate 0.001
if ($LASTEXITCODE -ne 0) { throw "Error generando instancia large" }
Log-OK "Instancia large  (10000 items) generada."

# ---------------------------------------------------------------------------
# PASO 3: Configuracion del experimento
# ---------------------------------------------------------------------------
$variantes  = @("sequential", "parallel", "islands")
$instancias = @("data/small", "data/medium", "data/large")
$hilos      = @(1, 2, 4, 8)
$semillas   = @(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)
$generaciones = 500
$poblacion    = 200

$total  = $variantes.Count * $instancias.Count * $hilos.Count * $semillas.Count
$actual = 0

Write-Host ""
Log-Info "Total de ejecuciones: $total"
Write-Host ""

# Limpiar CSV previo para no mezclar resultados viejos con nuevos
if (Test-Path "results\resultados.csv") {
    Remove-Item "results\resultados.csv"
    Log-Info "CSV anterior eliminado (se generara uno nuevo limpio)."
}

$tiempoInicio = Get-Date

# ---------------------------------------------------------------------------
# PASO 4: Bucle de experimentos
# ---------------------------------------------------------------------------
foreach ($variante in $variantes) {
    foreach ($instancia in $instancias) {
        $instNombre = Split-Path $instancia -Leaf
        foreach ($h in $hilos) {
            foreach ($semilla in $semillas) {
                $actual++
                $pct = [int]($actual * 100 / $total)

                # Barra de progreso de PowerShell
                Write-Progress -Activity "Experimentos Mochila GA" `
                    -Status "$actual/$total ($pct%) | $variante | $instNombre | hilos=$h | semilla=$semilla" `
                    -PercentComplete $pct

                # Ejecutar
                $salida = & .\mochila_ga `
                    --instance    $instancia `
                    --variant     $variante `
                    --threads     $h `
                    --seed        $semilla `
                    --generations $generaciones `
                    --population  $poblacion 2>&1

                if ($LASTEXITCODE -ne 0) {
                    Log-Warn "Fallo: variante=$variante instancia=$instNombre hilos=$h semilla=$semilla"
                }
            }
        }
    }
}

Write-Progress -Activity "Experimentos Mochila GA" -Completed

# ---------------------------------------------------------------------------
# PASO 5: Resumen
# ---------------------------------------------------------------------------
$tiempoFin  = Get-Date
$duracion   = ($tiempoFin - $tiempoInicio).ToString("hh\:mm\:ss")

Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host "  Experimentos completados: $actual/$total ejecuciones      " -ForegroundColor Green
Write-Host "  Tiempo total: $duracion                                   " -ForegroundColor Green
Write-Host "  Resultados en: results\resultados.csv                     " -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green

# Vista previa de las ultimas 5 filas
Write-Host ""
Log-Info "Vista previa (ultimas 5 filas del CSV):"
Get-Content "results\resultados.csv" | Select-Object -Last 6