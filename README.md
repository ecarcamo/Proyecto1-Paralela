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
./bin/screensaver_seq --n 128          # ~30 FPS: el filo del secuencial
./bin/screensaver_seq --n 3000         # ~1.4 FPS: se traba a proposito
./bin/screensaver_omp --n 3000         # ~41 FPS: mismo N, fluido otra vez
```

> El valor por defecto de `--n` es **128** porque es el N crítico medido a 1280×720:
> con ese valor el binario **secuencial** corre justo en los 30 FPS que exige el
> enunciado. Subirlo es lo que traba el programa, y eso es exactamente la demostración
> del proyecto — ver [docs/02-PARAMETRO-N.md](docs/02-PARAMETRO-N.md).

| Opción | Descripción | Por defecto |
|---|---|---|
| `--n <int>` | **Semillas sobre la esfera** (el parámetro N del enunciado) | 128 |
| `--width <int>` | Ancho del canvas (mínimo 640) | 1280 |
| `--height <int>` | Alto del canvas (mínimo 480) | 720 |
| `--angle <grados>` | Ángulo de divergencia | 137.50776 |
| `--seed <uint>` | Semilla del PRNG (determinista) | 12345 |
| `--color-speed <v>` | Vueltas del círculo de tono por segundo (0 = color fijo) | 0.06 |
| `--color-spread <f>` | Dispersión de ese ritmo entre semillas, 0..1 | 0.65 |
| `--physics <0\|1>` | Repulsión tipo Douady–Couder | 1 |
| `--voronoi <0\|1>` | Celdas de Voronoi vs. puntos | 1 |
| `--threads <int>` | Hilos de OpenMP | máximo del sistema |
| `--bench <K>` | Corre K frames sin ventana y reporta tiempos | 0 |
| `--no-render` | Modo headless | off |
| `--csv` | Salida en CSV para graficar | off |
| `--help` | Ayuda | |

### Deriva de color

Las semillas que ya están sobre la esfera no se quedan con el color con el que
nacieron: su **tono recorre el círculo HSV** con el tiempo, cada una a su propio
ritmo (`--color-spread`), y la saturación late hacia arriba — nunca hacia el gris —
así la esfera se ve **más saturada** y se va poblando de tonos distintos en vez de
cambiar en bloque como un filtro encima.

```bash
./bin/screensaver_seq                       # deriva suave (una vuelta cada ~17 s)
./bin/screensaver_seq --color-speed 0.4     # notoria, buena para la demo
./bin/screensaver_seq --color-speed 0       # el comportamiento anterior: color fijo
./bin/screensaver_seq --color-spread 0      # todas al mismo ritmo: la paleta gira entera
```

El color **no se guarda ni se acumula**: se recalcula cada frame como una función pura
`color(i, seed, t)`, igual que `color_for_seed()`. Eso es deliberado — mantiene el
render sin estado compartido entre frames, así que el `parallel for` de la fase OpenMP
sigue produciendo el mismo framebuffer bit a bit que el secuencial en el mismo instante
`t`. El costo es O(N) por frame contra el O(P·N) del kernel de Voronoi: medido, queda
dentro del ruido.

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
