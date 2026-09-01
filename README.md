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
| `--angle <grados>` | Ángulo de divergencia | 137.50776 |
| `--physics <0\|1>` | Repulsión tipo Douady–Couder (incompatible con `--cannons`) | 0 |
| `--voronoi <0\|1>` | Celdas de Voronoi vs. bolitas | 0 |
| `--raster <0\|1>` | Bolitas rasterizadas (plan B barato, **no escala con N**) | 0 |
| `--cannons <K>` | **Enciende el modo cañón** con K cañones, 1..N. Sin la bandera (o con `0`) la esfera aparece ya hecha | 0 |
| `--cannon-layout <m>` | Reparto de índices: `roundrobin` o `blocks` | roundrobin |
| `--fire-rate <R>` | Disparos por segundo, **por cañón**. Fija cuánto dura el llenado: `techo(N/K)/R` | 60 |
| `--muzzle-speed <V>` | Radios por segundo; el vuelo de cada bolita dura `1/V` | 1.5 |
| `--muzzle-radius <r0>` | Radio de la esfera chica donde están las bocas, [0, 0.95]. En `0` salen del centro exacto | 0.12 |
| `--recirculate <0\|1>` | `0`: aterrizan y se quedan (la esfera se completa). `1`: se redisparan | 0 |
| `--trail <L>` | Fantasmas de estela por bolita en vuelo | 6 |
| `--bench <K>` | Corre K frames sin ventana y reporta tiempos | 0 |
| `--vsync <0\|1>` | Sincronía vertical. **Poner en 0 para medir** | 1 |
| `--no-render` | Modo headless | off |
| `--csv` | Salida en CSV para graficar | off |
| `--help` / `-h` | Ayuda | |

Son **17 banderas**, y la lista de arriba es exactamente la que acepta el parser
(`src/args.c`) — ni una de más ni una de menos.

**Lo que NO es configurable, a propósito.** El canvas (1280×720), la velocidad de giro, el
encuadre, la semilla del PRNG y la deriva de color son fijos: son la identidad visual del
screensaver, no perillas. Dejarlos quietos además hace que toda medición salga a la misma
resolución sin tener que acordarse de pasarla, que es justo lo que el informe necesita.

> `--threads` **no existe todavía** — va a entrar junto con `bin/screensaver_omp` y
> `make omp`, que tampoco existen. Ver la sección **Estado** al final.

### Por qué no hay un `--cannon 0|1`

El modo cañón se enciende con `--cannons` y nada más. Tener las dos banderas obligaba a
escribir `--cannon 1 --cannons 1` para decir una sola cosa, y dejaba pasar combinaciones sin
sentido como `--cannon 0 --cannons 8`. Ahora el número *es* el interruptor:

```bash
./bin/screensaver_seq --n 400                 # sin cañones: la esfera aparece hecha
./bin/screensaver_seq --n 400 --cannons 1     # un cañón
./bin/screensaver_seq --n 400 --cannons 8     # ocho
./bin/screensaver_seq --n 400 --cannons 0     # explícitamente apagado
```

`--cannons` negativo se rechaza: `0` significa apagar, pero un negativo es un error de
tipeo y no se interpreta en silencio.

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

Con `--cannons K` la esfera no aparece hecha: arranca vacía y **K cañones la construyen
delante del público**, disparando una bolita por ronda hacia su posición del patrón áureo.
El disparo *i* aterriza exactamente en la posición *i* de Fibonacci, así que todo lo que se
dispara se ve y se queda, y la esfera se densifica de verdad mientras el contador de FPS
cae en vivo.

```bash
./bin/screensaver_seq --n 400 --cannons 8 --trail 6              # ocho chorros
./bin/screensaver_seq --n 400 --cannons 4 --cannon-layout blocks
```

**N es el tope de la esfera, no un punto de partida.** Los cañones reparten los `N` índices
del patrón, cada uno se dispara una vez y se queda donde aterrizó: a los `techo(N/K)/R`
segundos la esfera está **completa** y se queda completa. No es una preferencia estética,
es la única semántica que deja medir — `N` es el parámetro de carga del enunciado, y además
el patrón de Fibonacci depende de `N` en *todos* sus puntos (`z = 1 - 2(i+½)/N`), así que
subir `N` en vivo no agregaría una semilla al final: reubicaría a las que ya están.

Que la esfera se complete no significa que la animación muera: siguen el giro (`--rot`) y
la deriva de color, que son fijos.

Todo es función cerrada de `(i, t)` — sin estado que integrar, sin historial de partículas:

```
t_disparo(i) = ronda(i) / R
fase(i, t)   = t - t_disparo(i)                 (0 si todavía no se disparó)
radio(i, t)  = clamp(V * fase(i, t), 0, 1)      <- satura en 1: aterriza y se queda
pos(i, t)    = lerp(boca(cañón(i)), dir_fib(i), radio(i, t))
```

#### `--recirculate 1`: carga plana para medir

Con el flag encendido, el slot *i* se vuelve a disparar cada `T_ciclo = techo(N/K)/R`
segundos — a la fórmula de arriba solo se le agrega un `fmod`:

```
fase(i, t) = fmod(t - t_disparo(i), T_ciclo)
```

Sirve para el informe, porque deja la carga dibujada constante en régimen permanente. Pero
**tiene un costo visual que hay que saber**: si un slot pasa `1/V` segundos volando de cada
`T_ciclo`, la fracción de la esfera efectivamente puesta en cualquier instante es

```
aterrizadas / N = 1 - K·R/(V·N)
```

Con `N=400 K=8 R=60 V=1.5` eso da `1 - 480/600 = 0.20`: la esfera se queda **en el 20% para
siempre** y lo que se ve es un chorro permanente, nunca una esfera. Por eso el default es
`0`, y por eso el programa avisa por `stderr` cuando la fracción cae por debajo del 75%.

Que sea función pura de `(i, t)` es lo que hace que la pausa congele la animación sin una
línea de código extra, que el benchmark pueda arrancar en cualquier instante, y que la
versión paralela produzca el mismo framebuffer bit a bit.

Las K bocas se reparten sobre una esfera chica de radio `r0` usando **la misma construcción
de Fibonacci con K puntos**: cero matemática nueva. Con `r0 = 0` todas colapsan al origen y
se recupera el cañón único.

#### K es una perilla de carga ortogonal a N

Es lo que hace útil este modo para el informe, y **requiere `--recirculate 1`**. Con el
default, K solo acelera la rampa y el estado final es siempre N — que es justamente lo que
se quiere ver, pero no da una perilla de carga. Con recirculación la carga queda
**constante en régimen permanente**, así que K sube el costo por frame sin tocar la
geometría de la esfera:

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

Barrido de K con **N fijo en 400**, `--recirculate 1 R=60 V=6 L=6` a 1280×720, un hilo:

| K | dibujadas | ms/frame | sd | FPS |
|---|---|---|---|---|
| 1 | 430 | 118.3 | 3.0 | 8.5 |
| 2 | 460 | 119.8 | 1.1 | 8.4 |
| 4 | 520 | 135.4 | 1.7 | 7.4 |
| 8 | 640 | 163.1 | 1.8 | 6.1 |
| 16 | 880 | 205.2 | 10.2 | 4.9 |

El ajuste da `costo_ms = 0.199 · dibujadas + 31.8` con **R² = 0.994**: la recta confirma el
modelo O(P·N). Y las desviaciones estándar de 1–3 ms son la prueba de que el régimen
permanente existe: la media describe algo real y no el promedio de una rampa.

> Con el default (`--recirculate 0`) el régimen permanente también existe y es más simple
> —la esfera completa, `dibujadas = N`, cero en vuelo— solo que se llega a él recién
> después del llenado. `--bench` ya arranca en `t0 = T_ciclo + 1/V`, el primer instante en
> que todos los índices existen, así que en los dos modos mide régimen permanente y no la
> rampa.

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
ritmo, y la saturación late hacia arriba — nunca hacia el gris —
así la esfera se ve **más saturada** y se va poblando de tonos distintos en vez de
cambiar en bloque como un filtro encima.

Los dos valores son **fijos**: 0.06 vueltas de tono por segundo (una vuelta completa cada
~17 s, lento como para leerse como transición y no como parpadeo) y 0.65 de dispersión
entre semillas. Se ajustan en `SS_DEF_COLOR_SPEED` y `SS_DEF_COLOR_SPREAD`
([include/config.h](include/config.h)), no por línea de comandos.

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
