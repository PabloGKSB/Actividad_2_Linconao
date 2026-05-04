// =============================================================================
// omp_stub.hpp
// Stub mínimo de OpenMP para compilar sin soporte real de OpenMP.
// Se activa definiendo NO_OPENMP en la línea de compilación.
//
// Uso:
//   g++ -DNO_OPENMP ... (compila sin -fopenmp, funciona en single-thread)
// =============================================================================
#pragma once

#ifdef NO_OPENMP
#include <ctime>

// Funciones de tiempo y control de hilos (single-thread equivalents)
inline double omp_get_wtime() {
    return static_cast<double>(clock()) / CLOCKS_PER_SEC;
}
inline int  omp_get_max_threads()    { return 1; }
inline int  omp_get_thread_num()     { return 0; }
inline int  omp_get_num_threads()    { return 1; }
inline void omp_set_num_threads(int) {}

// Anular las directivas pragma omp en compilación sin OpenMP
// Los pragma omp parallel for / critical / etc. se ignoran
#define omp_stub_noop

#endif // NO_OPENMP

