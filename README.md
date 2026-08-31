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
./bin/screensaver_seq --n 128          # ~11 FPS con el kernel por defecto
./bin/screensaver_seq --n 128 --raster 1   # ~122 FPS: el plan B que no escala
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
| `--voronoi <0\|1>` | Celdas de Voronoi vs. bolitas | 0 |
| `--raster <0\|1>` | Bolitas rasterizadas (plan B barato, **no escala con N**) | 0 |
| `--cannon <0\|1>` | Modo cañón: la esfera se construye a cañonazos | 0 |
| `--cannons <K>` | Cañones simultáneos, 1..N | 1 |
| `--cannon-layout <m>` | Reparto de índices: `roundrobin` o `blocks` | roundrobin |
| `--fire-rate <R>` | Disparos por segundo, **por cañón** | 60 |
| `--muzzle-speed <V>` | Radios por segundo; el vuelo dura `1/V` | 1.5 |
| `--muzzle-radius <r0>` | Radio de la esfera chica donde están las bocas, [0, 0.95] | 0.12 |
| `--trail <L>` | Fantasmas de estela por bolita en vuelo | 6 |
| `--threads <int>` | Hilos de OpenMP | máximo del sistema |
| `--bench <K>` | Corre K frames sin ventana y reporta tiempos | 0 |
| `--no-render` | Modo headless | off |
| `--csv` | Salida en CSV para graficar | off |
| `--help` | Ayuda | |

### Los tres kernels y su costo

| Modo | Qué hace | Costo | N=128 | N=3000 |
|---|---|---|---|---|
| por defecto | bolitas por **raycasting** | O(P·N) | 90 ms | 1241 ms |
| `--voronoi 1` | celdas de Voronoi por raycasting | O(P·N) | 51 ms | 1189 ms |
| `--raster 1` | bolitas **rasterizadas** | ~O(1) en N | 8 ms | 10 ms |

*(medido a 1280×720 en el i9-13980HX, un hilo)*

Las bolitas se resuelven **por píxel**, no rasterizando discos. El rasterizado sigue
disponible con `--raster 1`, pero no es el baseline y no debe usarse para medir, por dos
razones que se ven en la tabla:

1. **Su costo es casi constante en N.** El radio va como `1/√N`, o sea el área de cada
   bolita va como `1/N`, y hay N bolitas: **el área total pintada no depende de N**.
   Medido, multiplicar N por 1562 (de 128 a 200 000) solo multiplica el costo por 4.8, y
   ese poco que crece es el bucle O(N) de proyectar, no los píxeles. Con ese kernel N no
   es una perilla de carga.
2. **No se paraleliza por semillas.** Dos bolitas que se solapan hacen read-modify-write
   del mismo z-buffer: es una carrera de datos.

Invertir el bucle arregla las dos cosas: pasa a ser O(P·N) —el mismo modelo de costo que
el Voronoi— y cada píxel es independiente, así que el `parallel for` entra sin carreras,
sin atómicos y sin z-buffer. La imagen además mejora, porque la oclusión entre bolitas
pasa a ser exacta por píxel.

> El test rayo-esfera cuesta la mitad de lo normal aprovechando que la cámara está sobre
> el eje z. Queda en el mismo producto punto que ya hacía el Voronoi más tres operaciones,
> y el `sqrt` pasa a ser uno por píxel en vez de uno por semilla. El `|C|²` de cada semilla
> se lee del arreglo `rr[]` que llena `rotate_seeds()`: **no** se asume que todas estén
> sobre la esfera unitaria, porque en modo cañón una bolita en pleno vuelo tiene `|C| < 1`.
> Ver el comentario de `render_balls_raycast()`.

### Modo cañón

Con `--cannon 1` la esfera no aparece hecha: arranca vacía y **K cañones la construyen
delante del público**, disparando una bolita por ronda hacia su posición del patrón áureo.
El disparo *i* aterriza exactamente en la posición *i* de Fibonacci, así que todo lo que se
dispara se ve y se queda, y la esfera se densifica de verdad mientras el contador de FPS
cae en vivo.

```bash
./bin/screensaver_seq --n 400 --cannon 1 --cannons 8 --trail 6   # ocho chorros
./bin/screensaver_seq --n 400 --cannon 1 --cannons 4 --cannon-layout blocks
```

**La animación no se termina nunca.** En vez de que el slot *i* aterrice y se quede quieto
para siempre, se vuelve a disparar cada `T_ciclo` segundos. Todo sigue siendo una función
cerrada de `t` —sin estado que integrar, sin historial de partículas—; a la fórmula solo se
le agregó un `fmod`:

```
T_ciclo      = techo(N/K) / R
t_disparo(i) = ronda(i) / R
fase(i, t)   = fmod(t - t_disparo(i), T_ciclo)
radio(i, t)  = clamp(V * fase(i, t), 0, 1)
pos(i, t)    = lerp(boca(cañón(i)), dir_fib(i), radio(i, t))
```

Que sea función pura de `(i, t)` es lo que hace que la pausa congele la animación sin una
línea de código extra, que el benchmark pueda arrancar en cualquier instante, y que la
versión paralela produzca el mismo framebuffer bit a bit.

Las K bocas se reparten sobre una esfera chica de radio `r0` usando **la misma construcción
de Fibonacci con K puntos**: cero matemática nueva. Con `r0 = 0` todas colapsan al origen y
se recupera el cañón único.

#### K es una perilla de carga ortogonal a N

Es lo que hace útil este modo para el informe. Sin recirculación, K solo aceleraba la rampa
y el estado final era siempre N. Con recirculación la carga queda **constante en régimen
permanente**, así que K sube el costo por frame sin tocar la geometría de la esfera:

```
dibujadas = N + (K·R/V) · (L/2)
            ^      ^        ^
      aterrizadas  |     fantasmas promedio por bolita en vuelo
              en vuelo por segundo
```

Dos detalles que no son obvios y que están verificados contra el código en
`tests/test_sphere.c` (sección 8):

- Las bolitas en vuelo **no** se suman aparte de N: con recirculación cada índice está o
  aterrizado o volando, nunca las dos cosas. Las reales son siempre N.
- Los fantasmas promedian `L/2`, no `L`. Una bolita recién salida todavía no desplegó la
  cola —los fantasmas nunca cruzan hacia atrás de su propio disparo— y como
  `delta = vuelo/(L+1)`, la cantidad crece lineal con la fase.

Barrido de K con **N fijo en 400**, `R=60 V=6 L=6` a 1280×720, un hilo:

| K | dibujadas | ms/frame | sd | FPS |
|---|---|---|---|---|
| 1 | 430 | 118.3 | 3.0 | 8.5 |
| 2 | 460 | 119.8 | 1.1 | 8.4 |
| 4 | 520 | 135.4 | 1.7 | 7.4 |
| 8 | 640 | 163.1 | 1.8 | 6.1 |
| 16 | 880 | 205.2 | 10.2 | 4.9 |

El ajuste da `costo_ms = 0.199 · dibujadas + 31.8` con **R² = 0.994**: la recta confirma el
modelo O(P·N). Y las desviaciones estándar de 1–3 ms son la prueba de que el régimen
permanente existe — sin recirculación el costo por frame crecería muestra a muestra durante
todo el benchmark y la media dejaría de describir nada.

> **Lo que está adentro se paga pero no se ve.** Una bolita en vuelo por dentro de una
> esfera ya llena queda tapada por completo por el shell exterior, pero el raycaster igual
> la evalúa en cada píxel. Es correcto —así funciona el raycasting sin estructuras de
> aceleración— pero conviene tenerlo dicho antes de que lo pregunten en la defensa.

`--cannon` es incompatible con `--physics` (la repulsión asume semillas ya sobre la
superficie) y `config_validate()` lo rechaza de entrada. Con `--voronoi 1` el particionado
usa solo las aterrizadas: el modo no se rompe, pero la animación no se ve, porque la celda
cubre superficie. `--raster 1` funciona sin tocar nada.

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
