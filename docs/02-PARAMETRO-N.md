# El parámetro N — modelo de costo, punto de saturación y protocolo de demostración

**Proyecto 1 — Computación Paralela y Distribuida (UVG, Sección 20)**
Equipo: Esteban Cárcamo · Nico · Dieguito

---

## 0. La tesis del proyecto en una frase

> Existe un **N crítico** donde la versión secuencial cae por debajo de 30 FPS y el
> screensaver se ve visiblemente trabado. Con **el mismo N**, la versión con OpenMP
> vuelve a correr fluida. El speedup no es un número en una tabla: **se ve en pantalla**.

Todo este documento existe para (a) predecir dónde está ese N con un modelo, y (b) fijar
el protocolo con el que lo vamos a medir.

---

## 1. Qué es N en nuestro screensaver

El enunciado exige *"por lo menos un parámetro (N), el cual indica la cantidad de
elementos a renderizar"*.

En nuestra escena, **N = número de semillas sobre la esfera de Fibonacci**.

Es la elección correcta y no una interpretación forzada, porque `N` controla
simultáneamente las tres cosas que importan:

| N controla… | Efecto |
|---|---|
| **Lo visual** | Cuántas celdas de Voronoi tiene la esfera. `N=50` es un balón de fútbol; `N=2000` es un grano de polen |
| **El costo del render** | El bucle interno recorre las `N` semillas por cada píxel de la silueta → `O(P·N)` |
| **El costo de la física** | Repulsión todos-contra-todos → `O(N²)` |

No hay ningún otro parámetro que haga que el programa se trabe. `N` es el único eje real
de dificultad, y por eso es el parámetro del enunciado.

---

## 2. Modelo de costo por frame

Sea:

| Símbolo | Significado | Valor por defecto |
|---|---|---|
| `W × H` | Resolución del canvas | 1280 × 720 = 921,600 px |
| `P` | Píxeles **dentro de la silueta** de la esfera | ≈ 287,000 (31 % de la pantalla) |
| `N` | Semillas | parámetro |
| `T_d` | Evaluaciones de producto punto por segundo, 1 hilo | ≈ 3 × 10⁹ /s *(a medir)* |
| `T_p` | Evaluaciones de par de repulsión por segundo, 1 hilo | ≈ 1 × 10⁹ /s *(a medir)* |
| `a` | Costo fijo del frame (present + textura + overlay + fondo) | ≈ 1.5 ms *(a medir)* |

El tiempo de un frame se descompone en tres términos:

```
                   P · N          N²
    T(N)  =  a  +  ─────   +   ──────
                    T_d          T_p
             ↑       ↑             ↑
           fijo   RENDER        FÍSICA
                 (lineal)     (cuadrática)
```

Es decir, **`T(N) = a + b·N + c·N²`**, con:

```
    b = P / T_d  ≈  287,000 / 3×10⁹  =  9.6 × 10⁻⁵ s   =  0.096 ms por semilla
    c = 1  / T_p ≈  1 / 1×10⁹        =  1.0 × 10⁻⁹ s   =  1 ns por par
```

### 2.1 ¿Cuál de los dos kernels manda? — un resultado no obvio

El término cuadrático solo domina cuando `c·N² > b·N`, o sea:

```
    N  >  b/c  =  (P/T_d)·T_p  =  287,000 / 3  ≈  96,000
```

> **Para todo N por debajo de ~10⁵, el cuello de botella es el RENDER, no la física.**

Esto es contraintuitivo — uno esperaría que un `O(N²)` aplaste a un `O(N)` — y la razón
es que la constante del render (`P ≈ 287,000` píxeles) es **enorme** comparada con la de
la física. Un `O(N)` con constante 287,000 le gana a un `O(N²)` con constante 1 hasta
que `N` llega a 96,000.

**Consecuencia práctica para el proyecto:** todo el esfuerzo de optimización y
paralelización va al bucle de Voronoi por píxel. La física se paraleliza igual (es
gratis, y da un segundo kernel con un perfil distinto para comparar en el informe), pero
no es donde se gana.

Es un hallazgo medible, defendible y de los que casi nadie reporta. Va al informe.

---

## 3. Predicción del N crítico secuencial

El umbral que fija el enunciado es **30 FPS**, o sea `T ≤ 33.3 ms`.

Ignorando el término cuadrático (§2.1) y despejando:

```
    N_crit^seq  =  ( 1/F_obj  −  a ) / b
                =  ( 33.3 ms  −  1.5 ms ) / 0.096 ms
                ≈  331 semillas
```

### 3.1 Proyección secuencial (estimada — se reemplaza con mediciones reales)

| N | `T(N)` estimado | FPS estimados | ¿Cumple ≥30 FPS? |
|---:|---:|---:|:---:|
| 100 | 11.1 ms | 90 | ✅ |
| 200 | 20.7 ms | 48 | ✅ |
| **330** | **33.1 ms** | **30.2** | ⚠️ **el filo** |
| 500 | 49.4 ms | 20 | ❌ |
| 1,000 | 97.3 ms | 10 | ❌ |
| 3,000 | 289 ms | 3.5 | ❌ visiblemente trabado |
| 8,000 | 767 ms | 1.3 | ❌ presentación de diapositivas |

### 3.2 Proyección paralela

Con `p` hilos y fracción serial de Amdahl `f = a / T(N)`:

```
                    1
    S(N, p)  =  ─────────────
                f + (1−f)/p
```

Fijate en algo importante: **`f` disminuye cuando `N` crece**, porque el costo fijo `a` se
diluye. Por eso el speedup **mejora** con N — el problema se vuelve *más* paralelo
mientras más grande es. Es Gustafson en acción, y es exactamente lo que queremos mostrar.

Con `p = 32` hilos (i9-13980HX de Esteban):

| N | `f = a/T` | Speedup teórico | `T` paralelo | FPS paralelo |
|---:|---:|---:|---:|---:|
| 330 | 0.045 | 13.5× | 2.5 ms | ✅ 60 (tope de vsync) |
| 1,000 | 0.015 | 21.6× | 4.5 ms | ✅ 60 (tope de vsync) |
| 3,000 | 0.005 | 27.5× | 10.5 ms | ✅ 95 |
| 8,000 | 0.002 | 30.2× | 25.4 ms | ✅ 39 |
| **10,000** | 0.0016 | **30.6×** | **31.4 ms** | ⚠️ **32 — el nuevo filo** |

```
    N_crit^seq ≈    330          N_crit^omp ≈  10,000
    ──────────────────────────────────────────────────
    Ganancia:  ~30× más elementos al mismo FPS objetivo
```

> ⚠️ Estos números son **predicciones del modelo**, con `T_d`, `T_p` y `a` estimados.
> Los valores reales los produce `--bench` (§5). Si la medición no coincide con el
> modelo, **gana la medición** y el modelo se recalibra: eso también va al informe, y
> explicar *por qué* difiere (ancho de banda de memoria, vectorización, E-cores) vale
> más que haber acertado.

### 3.3 Nota sobre las tres máquinas del equipo

`N_crit` **depende de la máquina** y cada quien mide la suya:

| | Esteban | Nico | Dieguito |
|---|---|---|---|
| SO | Linux | macOS | Windows |
| Hilos | 32 (i9-13980HX, híbrido P+E) | por medir | por medir |
| `N_crit^seq` esperado | ~330 | similar (depende de IPC) | similar |
| `N_crit^omp` esperado | ~10,000 | ∝ núcleos | ∝ núcleos |

`N_crit^seq` debería ser **parecido en las tres** (depende del IPC de un solo núcleo);
`N_crit^omp` debería escalar con el número de núcleos. **Que las tres máquinas den el
mismo `N_crit^seq` pero distinto `N_crit^omp` es, por sí solo, evidencia de que el
speedup viene del paralelismo y no de otra cosa.** Las tres tablas van al informe.

---

## 4. Rangos válidos y programación defensiva

El enunciado califica explícitamente la *"programación defensiva en caso de errores en el
ingreso de datos"*. Reglas para `--n`:

| Condición | Acción |
|---|---|
| No es un número entero (`--n abc`, `--n 3.5`) | Error, uso, `exit(EXIT_FAILURE)` |
| `N < 1` | Error explícito: *"N debe ser ≥ 1"* |
| `N > N_MAX` (5,000,000) | Error: excede la memoria razonable |
| `N > 50,000` | **Advertencia**, no error: *"con N=… se esperan <1 FPS en secuencial"* — se ejecuta igual, es decisión del usuario |
| Desbordamiento de `strtol` | Detectado vía `errno == ERANGE` |
| Falla `malloc` | Mensaje claro y salida limpia, sin *leaks* |

**Cota de memoria.** La estructura de semillas es SoA (*Structure of Arrays*) para
favorecer la vectorización:

```c
typedef struct {
    int    n, capacity;
    float *x,  *y,  *z;      /* posición sobre S²           12 B */
    float *vx, *vy, *vz;     /* velocidad tangencial        12 B */
    float *ax, *ay, *az;     /* aceleración                 12 B */
    uint32_t *color;         /* color pseudoaleatorio        4 B */
} SeedSet;                   /*                    total:  40 B por semilla */
```

`N = 5,000,000` → 200 MB. Es el techo defendible; de ahí sale `N_MAX`.

Los mismos criterios aplican a `--width`/`--height` (mínimo **640×480** por enunciado) y a
`--threads` (entre 1 y `omp_get_max_threads()`).

---

## 5. Protocolo de medición

El enunciado exige, en el Anexo 3, *"un mínimo de 10 mediciones por prueba"*. Por eso el
programa incorpora un **modo benchmark headless**:

```bash
./bin/screensaver_seq --n 1000 --bench 300 --no-render
./bin/screensaver_omp --n 1000 --bench 300 --threads 32
```

`--bench K` ejecuta `K` frames sin ventana, descarta los primeros 10 (calentamiento de
caché y de la piscina de hilos) y reporta:

```
N=1000  frames=300  media=97.31 ms  mediana=97.10  min=95.88  max=104.2  sd=1.21  FPS=10.28
```

### 5.1 Barrido para encontrar el N crítico

```bash
for N in 50 100 200 300 400 500 750 1000 1500 2000 3000 5000 8000 10000; do
    for rep in $(seq 1 10); do
        ./bin/screensaver_seq --n $N --bench 100 --no-render --csv >> datos/seq.csv
    done
done
```

10 repeticiones × 14 valores de N = 140 corridas por binario. Salida en CSV para graficar
directo.

### 5.2 Gráficas que salen de ahí (van al informe)

1. **`FPS` vs `N`**, secuencial y paralela en el mismo eje, con la línea horizontal en 30
   FPS. Las dos intersecciones son `N_crit^seq` y `N_crit^omp`. **Es la gráfica principal
   del proyecto.**
2. **`T(N)` vs `N`** con el ajuste `a + bN + cN²` superpuesto → valida el modelo de §2.
3. **Speedup vs número de hilos** a `N` fijo, con la curva de Amdahl superpuesta.
4. **Eficiencia `S/p` vs `p`** → dónde empieza a caer y por qué.

---

## 6. Protocolo de la demostración en vivo

Esta es la secuencia exacta para la presentación. Tres comandos, dos minutos.

```bash
# 1 — Secuencial, por debajo del filo: se ve fluido y bonito.
./bin/screensaver_seq --n 300
#    → ~33 FPS. "Así se ve nuestro screensaver."

# 2 — Secuencial, MISMO programa, N diez veces mayor: se traba.
./bin/screensaver_seq --n 3000
#    → ~3 FPS. La esfera avanza a tirones. El overlay marca los FPS en ROJO.

# 3 — Paralelo, MISMO N, MISMA máquina, MISMO código fuente.
./bin/screensaver_omp --n 3000 --threads 32
#    → ~95 FPS. Fluido otra vez.
```

Los pasos 2 y 3 corren **el mismo N sobre la misma máquina** y se diferencian únicamente
en si se compiló con `-fopenmp`. Ese es el punto entero del proyecto y por eso el paso 2
tiene que estar en la demo: **sin el trabón, el paralelo no impresiona a nadie.**

Detalle de la presentación: el overlay pinta el número de FPS **en rojo cuando cae por
debajo de 30**, así que el trabón no hay que explicarlo — se ve.

---

## 7. Por qué N es realmente el parámetro de dificultad (y no hicimos trampa)

Una objeción legítima que puede hacer el catedrático: *"¿no están inflando N a propósito
para que se trabe?"*. La respuesta es que **no**, y hay tres razones concretas:

1. **El costo es intrínseco al algoritmo, no artificial.** El Voronoi esférico por píxel
   requiere, por definición, encontrar el mínimo sobre las `N` semillas. No hay ningún
   `sleep()` ni bucle vacío inflando el tiempo. Es `O(P·N)` porque el problema es
   `O(P·N)`.

2. **N tiene sentido visual en todo el rango.** No estamos usando `N = 10,000` semillas
   invisibles: a `N = 10,000` la esfera se ve como un grano de polen real, con celdas
   diminutas y las espirales de Fibonacci perfectamente visibles. Cada semilla contribuye
   píxeles a la pantalla.

3. **Existe la optimización algorítmica y la vamos a hacer igual.** Se puede bajar de
   `O(P·N)` a `O(P·k)` con una rejilla espacial sobre la esfera. La haremos **después**
   de medir el speedup, y compararemos las dos cosas por separado:

   | | Qué hace | Ganancia esperada |
   |---|---|---|
   | **Paralelizar** | Reparte el mismo trabajo entre `p` hilos | ~30× (limitado por Amdahl) |
   | **Optimizar** | Reduce el trabajo total | ~100× (limitado por la geometría) |

   Distinguir *paralelizar* de *optimizar* — y medir las dos por separado — es
   precisamente lo que pide el enunciado con *"realizar mejoras y modificaciones
   iterativas al programa para obtener mejores versiones"*.

   **Y hay un giro que va al informe:** la rejilla espacial **crea desbalance de carga**.
   Con el Voronoi ingenuo cada píxel evalúa exactamente `N` semillas → la carga es
   idéntica y `schedule(static)` es óptimo. Con la rejilla, unos píxeles revisan 4
   semillas y otros 40 → aparece el caso de uso de `schedule(dynamic)`. **La optimización
   algorítmica cambia cuál es el mejor scheduler.** Ese resultado no lo va a reportar casi
   nadie.

### 7.1 Un desbalance que ya existe desde el día 1

Incluso *sin* rejilla espacial hay desbalance, y es geométrico: los rayos que **fallan la
esfera** cuestan ~10 ciclos (solo el discriminante) y los que **la pegan** cuestan `O(N)`.
Como la silueta es un círculo en medio de la pantalla, con `schedule(static)` los hilos
que reciben las filas de arriba y de abajo terminan casi de inmediato y **se quedan
esperando en la barrera**, mientras los de la franja central hacen todo el trabajo.

Se puede visualizar: un mapa de calor de tiempo por fila da una curva con forma de domo.
Es la justificación experimental más limpia de `schedule(dynamic)` que va a haber en el
informe.

---

## 8. Resumen ejecutivo

| Pregunta | Respuesta |
|---|---|
| ¿Qué es N? | Semillas sobre la esfera de Fibonacci |
| ¿Por qué es costoso? | Voronoi esférico por píxel: `O(P·N)`, `P ≈ 287,000` |
| ¿Cuál es el kernel dominante? | El **render**, para todo `N < 10⁵` (§2.1) |
| ¿Dónde se traba el secuencial? | `N ≈ 330` *(predicción; a medir)* |
| ¿Hasta dónde llega el paralelo? | `N ≈ 10,000` con 32 hilos *(predicción; a medir)* |
| ¿Cuál es la demo? | `seq --n 3000` (trabado) vs `omp --n 3000` (fluido) |
| ¿Cuántas mediciones? | 10 repeticiones × 14 valores de N × 2 binarios |
