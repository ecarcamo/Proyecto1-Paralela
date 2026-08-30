# ============================================================================
#  Proyecto 1 - Computacion Paralela y Distribuida (UVG, Seccion 20)
#  Screensaver: Animated Fibonacci Sphere
#
#  En esta etapa del proyecto hay UN solo binario:
#     bin/screensaver_seq  ->  el programa secuencial, sin OpenMP
#
#  No hay paralelismo todavia: ni #pragma omp en el codigo ni -fopenmp en la
#  compilacion. Este es el baseline honesto que despues se va a medir contra
#  la version paralela; el andamiaje de OpenMP entra cuando entre el primer
#  pragma, no antes.
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

# --------------------------------------------------------------------- rutas
SRC_DIR  := src
INC_DIR  := include
TEST_DIR := tests
BIN_DIR  := bin
OBJ_SEQ  := build/seq

# Todos los .c de src/ salvo main.c forman la biblioteca comun, para que los
# tests puedan enlazarla sin arrastrar SDL ni el bucle principal.
ALL_SRCS  := $(wildcard $(SRC_DIR)/*.c)
MAIN_SRC  := $(SRC_DIR)/main.c
LIB_SRCS  := $(filter-out $(MAIN_SRC),$(ALL_SRCS))

LIB_OBJS_SEQ := $(patsubst $(SRC_DIR)/%.c,$(OBJ_SEQ)/%.o,$(LIB_SRCS))
MAIN_OBJ_SEQ := $(patsubst $(SRC_DIR)/%.c,$(OBJ_SEQ)/%.o,$(wildcard $(MAIN_SRC)))

TEST_SRCS := $(wildcard $(TEST_DIR)/*.c)
TEST_BINS := $(patsubst $(TEST_DIR)/%.c,$(BIN_DIR)/%,$(TEST_SRCS))

SEQ_BIN := $(BIN_DIR)/screensaver_seq

# ------------------------------------------------------------------- targets
.PHONY: all seq tests test clean distclean print-config help

## all: binario secuencial y tests (por defecto)
all: seq tests

## seq: el binario secuencial  <- el unico que existe en esta etapa
seq: $(SEQ_BIN)

## tests: arneses de verificacion (no necesitan SDL)
tests: $(TEST_BINS)

$(SEQ_BIN): $(LIB_OBJS_SEQ) $(MAIN_OBJ_SEQ) | $(BIN_DIR)
	@if [ -z "$(MAIN_OBJ_SEQ)" ]; then \
	   echo ">> src/main.c todavia no existe: se omite $(SEQ_BIN)"; \
	 else \
	   echo "  LD  $@ [secuencial]"; \
	   $(CC) $(CFLAGS) $(SDL2_CFLAGS) $^ -o $@ $(SDL2_LIBS) $(LDLIBS); \
	 fi

$(OBJ_SEQ)/%.o: $(SRC_DIR)/%.c | $(OBJ_SEQ)
	@echo "  CC  $< [secuencial]"
	@$(CC) $(CFLAGS) $(SDL2_CFLAGS) -MMD -MP -c $< -o $@

# Los tests enlazan la biblioteca comun: verifican matematica, no velocidad.
$(BIN_DIR)/%: $(TEST_DIR)/%.c $(LIB_OBJS_SEQ) | $(BIN_DIR)
	@echo "  LD  $@ [test]"
	@$(CC) $(CFLAGS) $< $(LIB_OBJS_SEQ) -o $@ $(LDLIBS)

$(BIN_DIR) $(OBJ_SEQ):
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
	@echo "LIB_SRCS    : $(LIB_SRCS)"
	@echo "TEST_SRCS   : $(TEST_SRCS)"

## help: lista los targets disponibles
help:
	@grep -E '^## ' $(MAKEFILE_LIST) | sed 's/^## /  /'

# Los .d de -MMD: sin esto, tocar un .h NO recompila lo que lo incluye.
# MAIN_OBJ_SEQ tiene que estar: main.c incluye config.h, y cuando faltaba,
# agregar un campo a Config dejaba main.o compilado contra la estructura VIEJA
# -- misma direccion, offsets corridos -- y el programa leia basura en los
# campos de mas abajo sin que nada fallara al compilar ni al enlazar.
-include $(LIB_OBJS_SEQ:.o=.d) $(MAIN_OBJ_SEQ:.o=.d)
