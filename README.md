# Problema de la Mochila Extendida — Algoritmos Genéticos Paralelos con OpenMP

## Descripción

Implementación completa de tres variantes de algoritmos genéticos para resolver
el **Problema de la Mochila Extendida con múltiples restricciones**:

| Variante | Descripción |
|----------|-------------|
| `sequential` | AG clásico sin paralelismo |
| `parallel`   | AG con paralelismo OpenMP (fitness, cruzamiento, selección) |
| `islands`    | Modelo de Islas con topología en anillo y migración periódica |

Cada ítem tiene 5 atributos: **ID, Valor, Peso, Volumen, Categoría**.  
Las restricciones incluyen capacidades (peso/volumen), reglas de categoría,
incompatibilidades y dependencias entre ítems.

---

## Requisitos

- **Compilador**: `g++` con soporte C++17 y OpenMP
  - Linux/macOS: GCC ≥ 7, Clang ≥ 7
  - Windows: MinGW-w64 con `-fopenmp` o MSYS2
- **Sistema de archivos**: soporte para `std::filesystem` (C++17)
- **Bash**: para ejecutar `scripts/run_experiments.sh` (Linux/macOS/WSL)

---

## Estructura del Proyecto

```
actividad_mochila_paralela/
├── src/
│   ├── main.cpp                # Punto de entrada y CLI
│   ├── generate_instance.cpp   # Generador de instancias
│   ├── genetic_algorithm.cpp   # Variantes 1 y 2
│   ├── genetic_algorithm.hpp
│   ├── fitness.cpp             # Función de aptitud
│   ├── fitness.hpp
│   ├── island_model.cpp        # Variante 3
│   ├── island_model.hpp
│   ├── instance_loader.cpp     # Carga de CSV
│   └── instance_loader.hpp
├── data/
│   ├── small/                  # 100 ítems
│   ├── medium/                 # 1.000 ítems
│   └── large/                  # 10.000 ítems
├── results/
│   └── resultados.csv          # Resultados de experimentos
├── scripts/
│   └── run_experiments.sh
└── README.md
```

---

## Compilación

```bash
# Compilar el programa principal (mochila_ga)
g++ -O2 -fopenmp -std=c++17 \
    src/instance_loader.cpp \
    src/fitness.cpp \
    src/genetic_algorithm.cpp \
    src/island_model.cpp \
    src/main.cpp \
    -o mochila_ga

# Compilar el generador de instancias (generate_instance)
g++ -O2 -std=c++17 src/generate_instance.cpp -o generate_instance
```

> **Requisito**: GCC 8+ con OpenMP. Recomendado **MSYS2 MinGW-w64 ucrt64** (GCC 15.x).

> **Windows — si `g++` en PATH apunta a MinGW antiguo**: usar la ruta completa o
> agregar `C:\msys64\ucrt64\bin` al **inicio** del PATH del sistema:
> ```bash
> C:/msys64/ucrt64/bin/g++ -O2 -fopenmp -std=c++17 \
>     src/instance_loader.cpp src/fitness.cpp \
>     src/genetic_algorithm.cpp src/island_model.cpp src/main.cpp \
>     -o mochila_ga
> ```

> También se puede compilar con el `Makefile`:
> ```bash
> make                                         # usa g++ en PATH
> make CXX=C:/msys64/ucrt64/bin/g++           # fuerza GCC 15 de MSYS2
> ```


---

## Generar Instancias

```bash
./generate_instance --size 100   --output data/small  --seed 42
./generate_instance --size 1000  --output data/medium --seed 42
./generate_instance --size 10000 --output data/large  --seed 42
```

Cada instancia genera 5 archivos CSV en la carpeta indicada:

| Archivo | Contenido |
|---------|-----------|
| `items.csv` | `id,valor,peso,volumen,categoria` |
| `config.csv` | `W,V` (40% de la suma total) |
| `category_rules.csv` | `categoria,minimo,maximo` |
| `incompatibilities.csv` | `id_item_a,id_item_b` |
| `dependencies.csv` | `id_item,id_requerido` |

---

## Correr el Programa

```bash
./mochila_ga --instance data/small \
             --variant sequential \
             --threads 1 \
             --seed 7 \
             --generations 500 \
             --population 200
```

### Parámetros disponibles

| Parámetro | Valores válidos | Default |
|-----------|-----------------|---------|
| `--instance` | Carpeta de instancia | `data/small` |
| `--variant` | `sequential`, `parallel`, `islands` | `sequential` |
| `--threads` | 1, 2, 4, 8, ... | `1` |
| `--seed` | Cualquier entero | `42` |
| `--generations` | Entero positivo | `500` |
| `--population` | Entero positivo | `200` |

---

## Correr los Experimentos Completos

```bash
chmod +x scripts/run_experiments.sh
bash scripts/run_experiments.sh
```

Ejecuta **540 combinaciones**:
- 3 variantes × 3 instancias × 4 configuraciones de hilos (1,2,4,8) × 15 semillas

---

## Formato de `results/resultados.csv`

```
variante,instancia,hilos,semilla,tiempo_ms,mejor_valor,mejor_fitness,es_factible,generaciones_usadas
sequential,small,1,1,234.5,8750,8750.0,true,312
parallel,small,4,1,89.2,8820,8820.0,true,312
islands,small,4,1,95.3,8800,8800.0,true,500
```

| Campo | Descripción |
|-------|-------------|
| `variante` | `sequential`, `parallel` o `islands` |
| `instancia` | Nombre de la carpeta (`small`, `medium`, `large`) |
| `hilos` | Número de hilos OpenMP utilizados |
| `semilla` | Semilla del RNG para reproducibilidad |
| `tiempo_ms` | Tiempo de ejecución del AG en milisegundos (sin I/O) |
| `mejor_valor` | Suma de valores de ítems seleccionados |
| `mejor_fitness` | Valor de la función de aptitud del mejor individuo |
| `es_factible` | `true` si cumple todas las restricciones duras |
| `generaciones_usadas` | Generaciones ejecutadas hasta criterio de parada |

---

## Descripción de las Variantes

### Variante 1 — Secuencial (`sequential`)

AG clásico con los siguientes operadores:

1. **Inicialización**: cromosomas binarios aleatorios con `std::mt19937`
2. **Evaluación**: función de penalización con 5 términos
3. **Selección**: torneo de tamaño K=5
4. **Cruzamiento**: un punto de corte, probabilidad 0.85
5. **Mutación**: bit flip por posición, probabilidad 0.02
6. **Elitismo**: pool = padres + hijos (400), se conservan los 200 mejores

### Variante 2 — Paralelo con OpenMP (`parallel`)

Misma lógica que la variante 1, con tres zonas paralelizadas:

#### A) Evaluación de fitness
```cpp
#pragma omp parallel for schedule(dynamic)
for (int i = 0; i < pool_size; ++i) {
    calcularFitness(pool[i], inst);
}
```
- **Por qué es paralelizable**: `fitness(X_i)` no depende de `X_j`; cada individuo es independiente.
- **Variables privadas**: índice `i`, cálculos intermedios internos.
- **Variables compartidas (solo lectura)**: `pool[]`, `inst`.
- **Sin race condition**: cada hilo escribe en `pool[i]` exclusivo.
- **`schedule(dynamic)`**: equilibra carga si los cromosomas tienen distintos costos de evaluación.

#### B) Generación de hijos (cruzamiento + mutación)
```cpp
#pragma omp parallel for schedule(static)
for (int p = 0; p < n_parejas; ++p) {
    int tid = omp_get_thread_num();
    std::mt19937& rng_local = rngs[tid]; // RNG propio por hilo
    // ... cruzamiento y mutación ...
    hijos[2*p]   = h1;  // escritura en índice exclusivo
    hijos[2*p+1] = h2;
}
```
- **Por qué es paralelizable**: cada pareja produce hijos sin compartir estado.
- **Race condition en RNG evitada**: cada hilo tiene su propio `mt19937` con semilla `= seed + thread_id`.
- **Sin race condition en escritura**: cada hilo escribe en `hijos[2*p]` y `hijos[2*p+1]`, índices exclusivos.

#### C) Selección por torneo
- Ejecutada dentro de la zona paralela B.
- `poblacion[]` es **solo lectura** durante la selección → sin race condition.
- Cada hilo usa su `rng_local` privado.

### Variante 3 — Modelo de Islas (`islands`)

```
N_ISLAS = 4  (configurable vía --threads)
POP_POR_ISLA = 50
FRECUENCIA_MIGRACIÓN = 25 generaciones
N_MIGRANTES = 2
TOPOLOGÍA = anillo
```

- Cada isla evoluciona de forma **completamente independiente** entre migraciones.
- Las islas se ejecutan en paralelo con `#pragma omp parallel for`.
- La migración usa `#pragma omp critical` para proteger el acceso cruzado entre islas.
- Cada isla tiene su propio `mt19937` con semilla `= base_seed + isla_id`.

```
Anillo de migración:
  Isla 0 → Isla 1 → Isla 2 → Isla 3 → Isla 0
  (los 2 mejores de isla i reemplazan a los 2 peores de isla i+1)
```

---

## Parámetros del Algoritmo y Justificación

### Parámetros genéticos

| Parámetro | Valor | Justificación |
|-----------|-------|---------------|
| `POP_SIZE` | 200 | Balance diversidad/convergencia para instancias de hasta 10K ítems |
| `MAX_GEN` | 500 | Suficiente para convergencia observada experimentalmente |
| `PROB_CRUZAMIENTO` | 0.85 | Valor clásico de la literatura (Holland, 1975); favorece exploración |
| `PROB_MUTACION` | 0.02 | 1/n típico para n=50; previene convergencia prematura |
| `TORNEO_K` | 5 | Presión selectiva moderada; evita pérdida de diversidad |
| `SIN_MEJORA_MAX` | 100 | Criterio de parada por estancamiento |

### Penalizaciones de la función de aptitud

La función de aptitud es:

```
fitness(X) = valor_total(X)
           - α × exceso_peso(X)
           - β × exceso_volumen(X)
           - γ × violaciones_categoria(X)
           - δ × incompatibilidades_violadas(X)
           - ε × dependencias_incumplidas(X)
```

| Parámetro | Valor | Justificación |
|-----------|-------|---------------|
| `α` (peso) | 10.0 | Un kg de exceso "vale" 10 unidades de valor → cualquier ítem con valor < 10 no compensa exceder el peso |
| `β` (volumen) | 10.0 | Misma lógica que α; peso y volumen tienen igual importancia |
| `γ` (categoría) | 500.0 | Penalización moderada; restricción blanda → el AG la mejora gradualmente sin bloquear la búsqueda |
| `δ` (incompatibilidad) | 1000.0 | Restricción dura → penalización alta garantiza que casi nunca sea óptimo violarla |
| `ε` (dependencia) | 1000.0 | Igual que δ; dependencia es restricción dura |

> **Nota**: Los valores α=β=10 están calibrados para que el costo de exceder
> capacidad siempre supere el beneficio esperado de un ítem marginal (valor
> promedio ≈ 505; exceso típico ≥ 50 unidades → penalización = 500 >> valor).

---

## Medición de Tiempos

El tiempo se mide **exclusivamente** sobre el algoritmo genético, sin incluir la
lectura de archivos CSV:

```cpp
double inicio = omp_get_wtime();
// ... AG ...
double fin = omp_get_wtime();
double tiempo_ms = (fin - inicio) * 1000.0;
```

`omp_get_wtime()` retorna tiempo de pared (*wall clock*) de alta resolución,
independiente del número de hilos.

---

## Reproducibilidad

Cada ejecución acepta una semilla que inicializa `std::mt19937`:
- **Variante 1 y 2**: semilla única para el hilo maestro; hilos adicionales usan `seed + thread_id`.
- **Variante 3**: cada isla usa `seed + isla_id`.

Esto garantiza que los resultados sean **reproducibles** con la misma semilla y
número de hilos.

---

## Autores

Actividad 2 — Computación Paralela  
Universidad / Institución
