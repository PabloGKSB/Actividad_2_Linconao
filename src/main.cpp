// =============================================================================
// main.cpp
// Punto de entrada principal del programa mochila_ga.
// Maneja argumentos de línea de comandos, carga la instancia, ejecuta la
// variante del algoritmo genético solicitada y registra los resultados.
// =============================================================================
#include "instance_loader.hpp"
#include "fitness.hpp"
#include "genetic_algorithm.hpp"
#include "island_model.hpp"

#include <iostream>
#include <fstream>
#include <string>
#include <stdexcept>
#include <filesystem>
#include <omp.h>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Verifica si un archivo existe
// ---------------------------------------------------------------------------
static bool fileExists(const std::string& path) {
    return fs::exists(path);
}

// ---------------------------------------------------------------------------
// Estructura con los argumentos parseados de CLI
// ---------------------------------------------------------------------------
struct CLIArgs {
    std::string instance_folder = "data/small";
    std::string variant         = "sequential";  // sequential | parallel | islands
    int         threads         = 1;
    unsigned int seed           = 42;
    int         generations     = 500;
    int         population      = 200;
};

// ---------------------------------------------------------------------------
// Imprime el uso del programa
// ---------------------------------------------------------------------------
static void printUsage(const std::string& prog) {
    std::cout << "Uso: " << prog << " [opciones]\n\n"
              << "Opciones:\n"
              << "  --instance  <carpeta>    Carpeta con los CSV de la instancia  (default: data/small)\n"
              << "  --variant   <variante>   sequential | parallel | islands       (default: sequential)\n"
              << "  --threads   <N>          Número de hilos OpenMP                (default: 1)\n"
              << "  --seed      <N>          Semilla del generador aleatorio       (default: 42)\n"
              << "  --generations <N>        Máximo de generaciones                (default: 500)\n"
              << "  --population  <N>        Tamaño de la población                (default: 200)\n"
              << "  --help                   Muestra esta ayuda\n\n"
              << "Ejemplo:\n"
              << "  " << prog
              << " --instance data/small --variant parallel --threads 4 --seed 7\n";
}

// ---------------------------------------------------------------------------
// Parseo de argumentos de línea de comandos
// ---------------------------------------------------------------------------
static CLIArgs parseArgs(int argc, char* argv[]) {
    CLIArgs args;
    for (int i = 1; i < argc; ++i) {
        std::string flag(argv[i]);

        if (flag == "--help" || flag == "-h") {
            printUsage(argv[0]);
            std::exit(0);
        }
        auto nextArg = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("Falta valor para el argumento: " + flag);
            }
            return std::string(argv[++i]);
        };

        if      (flag == "--instance")   args.instance_folder = nextArg();
        else if (flag == "--variant")    args.variant         = nextArg();
        else if (flag == "--threads")    args.threads         = std::stoi(nextArg());
        else if (flag == "--seed")       args.seed            = static_cast<unsigned>(std::stoi(nextArg()));
        else if (flag == "--generations") args.generations    = std::stoi(nextArg());
        else if (flag == "--population") args.population      = std::stoi(nextArg());
        else {
            std::cerr << "[WARNING] Argumento desconocido ignorado: " << flag << "\n";
        }
    }
    return args;
}

// ---------------------------------------------------------------------------
// Escribe una fila de resultados en results/resultados.csv
// Si el archivo no existe, escribe el encabezado primero.
// ---------------------------------------------------------------------------
static void writeResult(const std::string& variante,
                         const std::string& instancia,
                         int                hilos,
                         unsigned int       semilla,
                         double             tiempo_ms,
                         int                mejor_valor,
                         double             mejor_fitness,
                         bool               es_factible,
                         int                generaciones_usadas)
{
    fs::create_directories("results");

    std::string csv_path = "results/resultados.csv";
    bool existe = fileExists(csv_path);

    std::ofstream file(csv_path, std::ios::app);
    if (!file.is_open()) {
        std::cerr << "[ERROR] No se pudo abrir results/resultados.csv para escritura.\n";
        return;
    }

    // Escribir cabecera si el archivo es nuevo
    if (!existe) {
        file << "variante,instancia,hilos,semilla,tiempo_ms,"
             << "mejor_valor,mejor_fitness,es_factible,generaciones_usadas\n";
    }

    file << variante      << ","
         << instancia     << ","
         << hilos         << ","
         << semilla        << ","
         << tiempo_ms     << ","
         << mejor_valor   << ","
         << mejor_fitness  << ","
         << (es_factible ? "true" : "false") << ","
         << generaciones_usadas << "\n";
}

// ---------------------------------------------------------------------------
// Extrae el nombre base de la instancia (último componente de la ruta)
// ---------------------------------------------------------------------------
static std::string instanceName(const std::string& folder) {
    return fs::path(folder).filename().string();
}

// =============================================================================
// main
// =============================================================================
int main(int argc, char* argv[]) {
    CLIArgs args;
    try {
        args = parseArgs(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << "\n";
        printUsage(argv[0]);
        return 1;
    }

    // ------------------------------------------------------------------
    // Validar variante
    // ------------------------------------------------------------------
    if (args.variant != "sequential" &&
        args.variant != "parallel"   &&
        args.variant != "islands") {
        std::cerr << "[ERROR] Variante inválida: '" << args.variant
                  << "'. Use: sequential | parallel | islands\n";
        return 1;
    }

    // ------------------------------------------------------------------
    // Configurar número de hilos OpenMP
    // ------------------------------------------------------------------
    omp_set_num_threads(args.threads);
    std::cout << "=== Mochila GA ===\n"
              << "  Instancia:   " << args.instance_folder << "\n"
              << "  Variante:    " << args.variant << "\n"
              << "  Hilos:       " << args.threads << "\n"
              << "  Semilla:     " << args.seed << "\n"
              << "  Generaciones:" << args.generations << "\n"
              << "  Población:   " << args.population << "\n\n";

    // ------------------------------------------------------------------
    // Cargar instancia (tiempo NO incluido en la medición)
    // ------------------------------------------------------------------
    ProblemInstance inst;
    try {
        inst = loadInstance(args.instance_folder);
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Al cargar instancia: " << e.what() << "\n";
        return 1;
    }

    // ------------------------------------------------------------------
    // Ejecutar variante solicitada
    // ------------------------------------------------------------------
    GAResult resultado;

    if (args.variant == "sequential") {
        GAParams params;
        params.pop_size         = args.population;
        params.max_generaciones = args.generations;
        params.seed             = args.seed;
        // Los demás parámetros usan sus valores por defecto

        resultado = runSequential(inst, params);

    } else if (args.variant == "parallel") {
        GAParams params;
        params.pop_size         = args.population;
        params.max_generaciones = args.generations;
        params.seed             = args.seed;

        resultado = runParallel(inst, params);

    } else { // islands
        IslandParams iparams;
        // Calcular pop_por_isla repartiendo equitativamente
        iparams.n_islas         = args.threads; // Una isla por hilo
        if (iparams.n_islas < 2) iparams.n_islas = 2; // Mínimo 2 islas
        // pop_por_isla mínimo 20 para garantizar diversidad y cruzamientos seguros
        iparams.pop_por_isla    = std::max(20, args.population / iparams.n_islas);
        // Asegurar paridad (se trabaja en pares)
        if (iparams.pop_por_isla % 2 != 0) iparams.pop_por_isla++;
        iparams.max_generaciones = args.generations;
        iparams.seed            = args.seed;

        resultado = runIslandModel(inst, iparams);
    }

    // ------------------------------------------------------------------
    // Imprimir resumen en consola
    // ------------------------------------------------------------------
    std::cout << "\n=== RESULTADO ===\n"
              << "  Tiempo:          " << resultado.tiempo_ms << " ms\n"
              << "  Generaciones:    " << resultado.generaciones_usadas << "\n"
              << "  Mejor valor:     " << resultado.mejor.valor_total << "\n"
              << "  Mejor fitness:   " << resultado.mejor.fitness << "\n"
              << "  Es factible:     " << (resultado.mejor.es_factible ? "SÍ" : "NO") << "\n";

    // ------------------------------------------------------------------
    // Escribir resultado en CSV
    // ------------------------------------------------------------------
    writeResult(
        args.variant,
        instanceName(args.instance_folder),
        args.threads,
        args.seed,
        resultado.tiempo_ms,
        resultado.mejor.valor_total,
        resultado.mejor.fitness,
        resultado.mejor.es_factible,
        resultado.generaciones_usadas
    );

    std::cout << "\n[OK] Resultado guardado en results/resultados.csv\n";
    return 0;
}
