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
| `P` | Píxeles **dentro de la silueta** de la esfera | **287,285** (31.2 % de la pantalla) |
| `N` | Semillas | parámetro |
| `T_d` | Evaluaciones de producto punto por segundo, 1 hilo | **1.175 × 10⁹ /s** ✅ *medido* |
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
    b = P / T_d  =  287,285 / 1.175×10⁹  =  2.445 × 10⁻⁴ s  =  0.2445 ms por semilla   ✅ medido
    c = 1  / T_p ≈  1 / 1×10⁹            =  1.0   × 10⁻⁹ s  =  1 ns por par           (aún estimado)
```

### 2.1 ¿Cuál de los dos kernels manda? — un resultado no obvio

El término cuadrático solo domina cuando `c·N² > b·N`, o sea:

```
    N  >  b/c  =  (P/T_d)·T_p  =  2.445×10⁻⁴ / 1×10⁻⁹  ≈  244,500
```

> **Para todo N por debajo de ~2.4 × 10⁵, el cuello de botella es el RENDER, no la física.**

Esto es contraintuitivo — uno esperaría que un `O(N²)` aplaste a un `O(N)` — y la razón
es que la constante del render (`P = 287,285` píxeles) es **enorme** comparada con la de
la física. Un `O(N)` con constante 287,285 le gana a un `O(N²)` con constante 1 hasta
que `N` llega a ~244,500 — que está muy por encima de cualquier `N` que vayamos a usar.

**Consecuencia práctica para el proyecto:** todo el esfuerzo de optimización y
paralelización va al bucle de Voronoi por píxel. La física se paraleliza igual (es
gratis, y da un segundo kernel con un perfil distinto para comparar en el informe), pero
no es donde se gana.

Es un hallazgo medible, defendible y de los que casi nadie reporta. Va al informe.

---

## 3. El N crítico — modelo calibrado con mediciones reales

El umbral que fija el enunciado es **30 FPS**, o sea `T ≤ 33.3 ms`.

Ignorando el término cuadrático (§2.1) y despejando:

```
    N_crit^seq  =  ( 1/F_obj  −  a ) / b
                =  ( 33.3 ms  −  1.5 ms ) / 0.2445 ms
                ≈  130 semillas
```

> ### 📌 El modelo estaba mal por 2.5× y la medición lo corrigió
>
> La primera versión de este documento estimaba `T_d ≈ 3×10⁹` productos punto por
> segundo y predecía `N_crit ≈ 330`. Al medirlo con `make test`, el valor real en la
> máquina de Esteban resultó ser **`1.175×10⁹` — 2.5 veces menor** — y el `N_crit` real
> es **130**, no 330.
>
> **Por qué falló la estimación.** Se había supuesto que el bucle interno se
> vectorizaría. No lo hace, y no puede: la actualización de los dos máximos
> (`if (d > b1) { b2 = b1; b1 = d; }`) es una **dependencia serial entre iteraciones**
> — cada iteración necesita el `b1` de la anterior. Eso impide que el compilador use
> AVX2, y encima mete una rama dependiente de los datos. El bucle corre escalar, a
> ~1.4 evaluaciones por ciclo.
>
> Ese análisis es más valioso para el informe que haber acertado el número, y ya apunta
> a una optimización concreta para la fase 2: llevar máximos parciales por carril SIMD y
> reducirlos al final.

> **Varianza entre corridas.** Dos ejecuciones consecutivas de `make test` en la misma
> máquina dieron `T_d = 1.175×10⁹` y `1.053×10⁹` — un **10 % de diferencia**, que mueve
> el `N_crit` de 130 a 117. Las causas son el *turbo boost* (la frecuencia baja cuando el
> núcleo se calienta) y que otros procesos comparten la caché L3.
>
> Esto no es ruido a ignorar: **es la razón por la que el enunciado exige 10 mediciones
> por prueba** (Anexo 3). Una sola corrida no es un dato. Todos los números que vayan al
> informe se reportan como media ± desviación sobre 10 repeticiones.

### 3.1 Proyección secuencial (constantes medidas, `a` aún estimado)

| N | `T(N)` | FPS | ¿Cumple ≥30 FPS? |
|---:|---:|---:|:---:|
| 50 | 13.7 ms | 73.0 | ✅ |
| 100 | 26.0 ms | 38.5 | ✅ |
| **130** | **33.3 ms** | **30.0** | ⚠️ **el filo** |
| 200 | 50.4 ms | 19.8 | ❌ |
| 300 | 74.9 ms | 13.4 | ❌ |
| 500 | 123.8 ms | 8.1 | ❌ |
| 1,000 | 246.0 ms | 4.1 | ❌ visiblemente trabado |
| 3,000 | 735.0 ms | 1.4 | ❌ pasa a ser un pase de diapositivas |
| 8,000 | 1,957 ms | 0.5 | ❌ dos segundos por frame |

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
| 130 | 0.0451 | 13.4× | 2.5 ms | ✅ tope de vsync |
| 500 | 0.0121 | 23.3× | 5.3 ms | ✅ tope de vsync |
| 1,000 | 0.0061 | 26.9× | 9.2 ms | ✅ 109 |
| 3,000 | 0.0020 | 30.1× | 24.4 ms | ✅ 41 |
| **4,000** | 0.0015 | **30.6×** | **32.1 ms** | ⚠️ **31 — el nuevo filo** |

```
    N_crit^seq  ≈    130          N_crit^omp  ≈  4,000
    ──────────────────────────────────────────────────
    Ganancia:  ~31× más elementos al mismo FPS objetivo
```

### 3.3 El N crítico depende de la resolución

Como `N_crit ∝ 1/P` y `P` crece con el cuadrado de la resolución, la resolución es una
palanca directa sobre dónde se traba el programa. Con las mismas constantes medidas:

| Resolución | `P` (píxeles de silueta) | `b` (ms/semilla) | `N_crit^seq` |
|---|---:|---:|---:|
| 640 × 480 *(mínimo del enunciado)* | 127,676 | 0.1087 | **293** |
| **1280 × 720** *(por defecto)* | 287,285 | 0.2445 | **130** |
| 1920 × 1080 | 646,392 | 0.5501 | **58** |

**Consecuencia para la demo:** hay que reportar siempre `N_crit` **junto con la
resolución**, porque solo no significa nada. Y si en la presentación hiciera falta un
`N_crit` más alto para que la esfera se vea más rica, se baja la resolución en vez de
tocar el algoritmo.

### 3.4 Nota sobre las tres máquinas del equipo

`N_crit` **depende de la máquina** y cada quien mide la suya con `make test`:

| | Esteban | Nico | Dieguito |
|---|---|---|---|
| SO | Linux | macOS | Windows |
| Hilos | 32 (i9-13980HX, híbrido P+E) | por medir | por medir |
| `T_d` medido | **1.175 × 10⁹ /s** ✅ | por medir | por medir |
| `N_crit^seq` @1280×720 | **130** ✅ | por medir | por medir |
| `N_crit^omp` esperado | ~4,000 | ∝ núcleos | ∝ núcleos |

`N_crit^seq` debería ser **parecido en las tres** (depende del IPC de un solo núcleo);
`N_crit^omp` debería escalar con el número de núcleos. **Que las tres máquinas den un
`N_crit^seq` parecido pero un `N_crit^omp` muy distinto es, por sí solo, evidencia de que
el speedup viene del paralelismo y no de otra cosa.** Las tres tablas van al informe.

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
# 1 — Secuencial, justo debajo del filo: se ve fluido y bonito.
./bin/screensaver_seq --n 130
#    → ~30 FPS. "Así se ve nuestro screensaver."

# 2 — Secuencial, MISMO programa, N veintitrés veces mayor: se traba.
./bin/screensaver_seq --n 3000
#    → ~1.4 FPS. La esfera avanza a tirones. El overlay marca los FPS en ROJO.

# 3 — Paralelo, MISMO N, MISMA máquina, MISMO código fuente.
./bin/screensaver_omp --n 3000 --threads 32
#    → ~41 FPS. Fluido otra vez.
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
| ¿Por qué es costoso? | Voronoi esférico por píxel: `O(P·N)`, `P = 287,285` |
| ¿Cuál es el kernel dominante? | El **render**, para todo `N < 2.4×10⁵` (§2.1) |
| ¿Dónde se traba el secuencial? | **`N ≈ 130`** @1280×720 ✅ *medido* |
| ¿Hasta dónde llega el paralelo? | `N ≈ 4,000` con 32 hilos *(predicción sobre constantes medidas)* |
| ¿Cuál es la demo? | `seq --n 3000` (trabado) vs `omp --n 3000` (fluido) |
| ¿Cuántas mediciones? | 10 repeticiones × 14 valores de N × 2 binarios |
