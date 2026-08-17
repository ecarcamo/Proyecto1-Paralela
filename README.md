# Screensaver — *Animated Fibonacci Sphere*

**Proyecto 1 — Computación Paralela y Distribuida · UVG, Sección 20**
Esteban Cárcamo · Nico · Dieguito

Screensaver en **C** que dibuja `N` semillas distribuidas sobre una esfera mediante el
**ángulo áureo** (137.50776°), renderizadas por *raycasting* en CPU como celdas de
**Voronoi esférico**, con repulsión física entre semillas.

El objetivo del proyecto no es el screensaver: es **medir cuánto lo acelera OpenMP**. Por
eso el renderizado es por software (la CPU calcula el framebuffer completo y SDL2 solo lo
presenta) — si el trabajo pesado lo hiciera la GPU, no quedaría nada que paralelizar.

📄 **[Fundamento matemático](docs/01-FUNDAMENTO-MATEMATICO.md)** ·
📄 **[El parámetro N y el punto de saturación](docs/02-PARAMETRO-N.md)**

---

## Compilación

Un solo árbol de código produce dos binarios. Las directivas `#pragma omp` son **inertes**
si no se compila con `-fopenmp`, así que el baseline secuencial y la versión paralela
salen del **mismo fuente**:

```
bin/screensaver_seq   <-  sin -fopenmp   (baseline)
bin/screensaver_omp   <-  con -fopenmp   (paralelo)
```

```bash
make            # ambos binarios + tests
make seq        # solo el secuencial
make omp        # solo el paralelo
make test       # verificación matemática (no necesita SDL)
make help       # lista de targets
```

### Dependencias por plataforma

| SO | Cómo instalarlas |
|---|---|
| **Linux** (Arch) | `sudo pacman -S sdl2 gcc make` |
| **Linux** (Debian/Ubuntu) | `sudo apt install libsdl2-dev build-essential` |
| **macOS** | `brew install sdl2 libomp` |
| **Windows** | MSYS2, terminal **MINGW64**: `pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 make` |

> ⚠️ **macOS:** Apple Clang **no** trae OpenMP. Por eso hace falta `brew install libomp`;
> el Makefile ya detecta Homebrew (tanto `/opt/homebrew` como `/usr/local`) y usa
> `-Xpreprocessor -fopenmp -lomp`.

> ⚠️ **Windows:** usar MSYS2/MinGW, **no** Visual Studio. El Makefile está escrito para
> GCC/Clang.

Si algo no compila, `make print-config` imprime las banderas que detectó — es más rápido
que adivinar.

---

## Uso

```bash
./bin/screensaver_seq --n 300
./bin/screensaver_omp --n 3000 --threads 8
```

| Opción | Descripción | Por defecto |
|---|---|---|
| `--n <int>` | **Semillas sobre la esfera** (el parámetro N del enunciado) | 1000 |
| `--width <int>` | Ancho del canvas (mínimo 640) | 1280 |
| `--height <int>` | Alto del canvas (mínimo 480) | 720 |
| `--angle <grados>` | Ángulo de divergencia | 137.50776 |
| `--seed <uint>` | Semilla del PRNG (determinista) | 12345 |
| `--physics <0\|1>` | Repulsión tipo Douady–Couder | 1 |
| `--voronoi <0\|1>` | Celdas de Voronoi vs. puntos | 1 |
| `--threads <int>` | Hilos de OpenMP | máximo del sistema |
| `--bench <K>` | Corre K frames sin ventana y reporta tiempos | 0 |
| `--no-render` | Modo headless | off |
| `--csv` | Salida en CSV para graficar | off |
| `--help` | Ayuda | |

### Teclas

| Tecla | Acción |
|---|---|
| `ESC` / `Q` | Salir |
| `[` `]` | Barrer el ángulo de divergencia — **el patrón colapsa y re-explota en 137.5°** |
| `P` | Pausar |
| `R` | Reiniciar |

---

## Estructura

```
include/        contratos (headers) — config.h es el que comparten los tres módulos
src/            implementación
tests/          verificación matemática, sin SDL
docs/           fundamento y mediciones
datos/          resultados de los barridos de N
```

---

## Estado

- [x] Fundamento matemático documentado
- [x] Núcleo: esfera de Fibonacci, RNG, color, métricas de uniformidad
- [ ] Ventana SDL, argumentos y overlay de FPS
- [ ] Voronoi esférico, física y benchmark
- [ ] Paralelización con OpenMP y campaña de mediciones
