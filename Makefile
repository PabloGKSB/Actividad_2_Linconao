# =============================================================================
# Makefile para el proyecto Mochila GA
# Requiere: GCC 8+ con soporte OpenMP
#
# En Windows con MSYS2 (ucrt64), si g++ en PATH apunta a MinGW antiguo:
#   make CXX=C:/msys64/ucrt64/bin/g++
# O agregar C:\msys64\ucrt64\bin al inicio del PATH del sistema.
# =============================================================================

CXX      = g++
CXXFLAGS = -O2 -std=c++17 -Wall -Wextra
OPENMP   = -fopenmp

# Archivos fuente del algoritmo genético (excluye generate_instance)
SRCS_GA = src/instance_loader.cpp \
           src/fitness.cpp \
           src/genetic_algorithm.cpp \
           src/island_model.cpp \
           src/main.cpp

SRC_GEN = src/generate_instance.cpp

TARGET_GA  = mochila_ga
TARGET_GEN = generate_instance

# ---- Objetivo principal ----
all: $(TARGET_GA) $(TARGET_GEN)

$(TARGET_GA): $(SRCS_GA)
	$(CXX) $(CXXFLAGS) $(OPENMP) $^ -o $@
	@echo "[OK] $(TARGET_GA) compilado."

$(TARGET_GEN): $(SRC_GEN)
	$(CXX) $(CXXFLAGS) $^ -o $@
	@echo "[OK] $(TARGET_GEN) compilado."

clean:
	rm -f $(TARGET_GA) $(TARGET_GEN) *.exe *.o

.PHONY: all clean
