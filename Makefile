# ============================================================================
#  Proyecto 1 - Computacion Paralela y Distribuida (UVG, Seccion 20)
#  Screensaver: Animated Fibonacci Sphere
#
#  Un solo arbol de codigo produce DOS binarios:
#     bin/screensaver_seq  ->  SIN -fopenmp  (los #pragma omp quedan inertes)
#     bin/screensaver_omp  ->  CON -fopenmp
#
#  Esa es la razon de no duplicar el codigo: las directivas de OpenMP son
#  ignoradas por el compilador si no se le pasa la bandera, asi que el
#  baseline secuencial y la version paralela salen del MISMO fuente.
#
#  Equipo: Esteban Carcamo (Linux) - Nico (macOS) - Dieguito (Windows/MSYS2)
# ============================================================================

# ---------------------------------------------------------------- plataforma
ifeq ($(OS),Windows_NT)
    PLATFORM := windows
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        PLATFORM := macos
    else
        PLATFORM := linux
    endif
endif

CC      ?= cc
OPT     ?= -O2
STD     := -std=c11
WARN    := -Wall -Wextra -Wshadow -Wno-unused-parameter
INCLUDE := -Iinclude
CFLAGS  := $(STD) $(OPT) $(WARN) $(INCLUDE)
LDLIBS  := -lm

# ---------------------------------------------------------------------- SDL2
# sdl2-config existe en las tres plataformas (Homebrew en macOS, MSYS2 en
# Windows, el paquete de distro en Linux). pkg-config es el plan B.
SDL2_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL2_LIBS   := $(shell sdl2-config --libs   2>/dev/null)
ifeq ($(strip $(SDL2_LIBS)),)
    SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
    SDL2_LIBS   := $(shell pkg-config --libs   sdl2 2>/dev/null)
endif
ifeq ($(strip $(SDL2_LIBS)),)
    SDL2_LIBS := -lSDL2
endif

# -------------------------------------------------------------------- OpenMP
# Linux y Windows/MSYS2 usan GCC, que trae OpenMP de fabrica.
# macOS es el caso especial: Apple Clang NO incluye OpenMP, hay que instalar
# libomp con Homebrew y pasarle el preprocesador a mano.
#     brew install libomp sdl2
ifeq ($(PLATFORM),macos)
    BREW_PREFIX := $(shell brew --prefix 2>/dev/null)
    ifeq ($(strip $(BREW_PREFIX)),)
        BREW_PREFIX := /opt/homebrew
    endif
    LIBOMP := $(BREW_PREFIX)/opt/libomp
    OMP_CFLAGS := -Xpreprocessor -fopenmp -I$(LIBOMP)/include
    OMP_LDLIBS := -L$(LIBOMP)/lib -lomp
else
    OMP_CFLAGS := -fopenmp
    OMP_LDLIBS := -fopenmp
endif

# --------------------------------------------------------------------- rutas
SRC_DIR  := src
INC_DIR  := include
TEST_DIR := tests
BIN_DIR  := bin
OBJ_SEQ  := build/seq
OBJ_OMP  := build/omp

# Todos los .c de src/ salvo main.c forman la biblioteca comun, para que los
# tests puedan enlazarla sin arrastrar SDL ni el bucle principal.
ALL_SRCS  := $(wildcard $(SRC_DIR)/*.c)
MAIN_SRC  := $(SRC_DIR)/main.c
LIB_SRCS  := $(filter-out $(MAIN_SRC),$(ALL_SRCS))

LIB_OBJS_SEQ := $(patsubst $(SRC_DIR)/%.c,$(OBJ_SEQ)/%.o,$(LIB_SRCS))
LIB_OBJS_OMP := $(patsubst $(SRC_DIR)/%.c,$(OBJ_OMP)/%.o,$(LIB_SRCS))
MAIN_OBJ_SEQ := $(patsubst $(SRC_DIR)/%.c,$(OBJ_SEQ)/%.o,$(wildcard $(MAIN_SRC)))
MAIN_OBJ_OMP := $(patsubst $(SRC_DIR)/%.c,$(OBJ_OMP)/%.o,$(wildcard $(MAIN_SRC)))

TEST_SRCS := $(wildcard $(TEST_DIR)/*.c)
TEST_BINS := $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/%,$(TEST_SRCS))

SEQ_BIN := $(BIN_DIR)/screensaver_seq
OMP_BIN := $(BIN_DIR)/screensaver_omp

# ------------------------------------------------------------------- targets
.PHONY: all seq omp tests test clean distclean print-config help

## all: binario secuencial y tests (por defecto)
all: seq tests

## seq: binario secuencial, sin OpenMP  <- el baseline honesto que se mide
seq: $(SEQ_BIN)

## omp: binario paralelo, con OpenMP
omp: $(OMP_BIN)

## tests: arneses de verificacion (no necesitan SDL)
tests: $(TEST_BINS)

$(SEQ_BIN): $(LIB_OBJS_SEQ) $(MAIN_OBJ_SEQ) | $(BIN_DIR)
	@if [ -z "$(MAIN_OBJ_SEQ)" ]; then \
	   echo ">> src/main.c todavia no existe: se omite $(SEQ_BIN)"; \
	 else \
	   echo "  LD  $@ [secuencial]"; \
	   $(CC) $(CFLAGS) $(SDL2_CFLAGS) $^ -o $@ $(SDL2_LIBS) $(LDLIBS); \
	 fi

$(OMP_BIN): $(LIB_OBJS_OMP) $(MAIN_OBJ_OMP) | $(BIN_DIR)
	@if [ -z "$(MAIN_OBJ_OMP)" ]; then \
	   echo ">> src/main.c todavia no existe: se omite $(OMP_BIN)"; \
	 else \
	   echo "  LD  $@ [OpenMP]"; \
	   $(CC) $(CFLAGS) $(OMP_CFLAGS) $(SDL2_CFLAGS) $^ -o $@ $(SDL2_LIBS) $(OMP_LDLIBS) $(LDLIBS); \
	 fi

$(OBJ_SEQ)/%.o: $(SRC_DIR)/%.c | $(OBJ_SEQ)
	@echo "  CC  $< [secuencial]"
	@$(CC) $(CFLAGS) $(SDL2_CFLAGS) -MMD -MP -c $< -o $@

$(OBJ_OMP)/%.o: $(SRC_DIR)/%.c | $(OBJ_OMP)
	@echo "  CC  $< [OpenMP]"
	@$(CC) $(CFLAGS) $(OMP_CFLAGS) $(SDL2_CFLAGS) -MMD -MP -c $< -o $@

# Los tests enlazan la biblioteca secuencial: verifican matematica, no velocidad.
$(BIN_DIR)/%: $(TEST_DIR)/%.c $(LIB_OBJS_SEQ) | $(BIN_DIR)
	@echo "  LD  $@ [test]"
	@$(CC) $(CFLAGS) $< $(LIB_OBJS_SEQ) -o $@ $(LDLIBS)

$(BIN_DIR) $(OBJ_SEQ) $(OBJ_OMP):
	@mkdir -p $@

## test: compila y ejecuta el arnes de verificacion matematica
test: tests
	@for t in $(TEST_BINS); do echo ""; echo "=== $$t ==="; ./$$t || exit 1; done

## clean: borra objetos y binarios
clean:
	@rm -rf build $(BIN_DIR)
	@echo "limpio."

distclean: clean
	@rm -f datos/tmp_*

## print-config: muestra las banderas detectadas (para depurar el entorno)
print-config:
	@echo "PLATAFORMA  : $(PLATFORM)"
	@echo "CC          : $(CC)"
	@echo "CFLAGS      : $(CFLAGS)"
	@echo "SDL2_CFLAGS : $(SDL2_CFLAGS)"
	@echo "SDL2_LIBS   : $(SDL2_LIBS)"
	@echo "OMP_CFLAGS  : $(OMP_CFLAGS)"
	@echo "OMP_LDLIBS  : $(OMP_LDLIBS)"
	@echo "LIB_SRCS    : $(LIB_SRCS)"
	@echo "TEST_SRCS   : $(TEST_SRCS)"

## help: lista los targets disponibles
help:
	@grep -E '^## ' $(MAKEFILE_LIST) | sed 's/^## /  /'

-include $(LIB_OBJS_SEQ:.o=.d) $(LIB_OBJS_OMP:.o=.d)
