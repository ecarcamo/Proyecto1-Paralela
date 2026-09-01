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
| `T_p` | Evaluaciones de par de repulsión por segundo, 1 hilo | **6.2 × 10⁸ /s** ✅ *medido, §3.6C* |
| `a` | Costo fijo del frame (present + textura + overlay + fondo) | **2.8 ms** ✅ *medido, §3.6D* |

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
| Hilos | 32 (i9-13980HX, híbrido P+E) | 12 (Apple M4 Pro, 8P + 4E) ✅ | por medir |
| `T_d` medido | **1.175 × 10⁹ /s** ✅ | **2.095 × 10⁹ /s** ✅ | por medir |
| `N_crit^seq` @1280×720 | **130** ✅ | **87** ✅ *(medido, §3.5)* | por medir |
| `N_crit^omp` esperado | ~4,000 | ~7,000 (por medir) | ∝ núcleos |

`N_crit^seq` debería ser **parecido en las tres** (depende del IPC de un solo núcleo);
`N_crit^omp` debería escalar con el número de núcleos. **Que las tres máquinas den un
`N_crit^seq` parecido pero un `N_crit^omp` muy distinto es, por sí solo, evidencia de que
el speedup viene del paralelismo y no de otra cosa.** Las tres tablas van al informe.

### 3.5 Barrido medido en el MacBook (Apple M4 Pro) — el N de la demo

Barrido real del baseline secuencial, **10 repeticiones por punto**, headless
(`--bench K --no-render --csv`), kernel por defecto, 1280×720. Datos crudos en
[`datos/nico_m4pro_seq_barrido.csv`](../datos/nico_m4pro_seq_barrido.csv).

| N | `T` mediana | FPS | sd | Lectura |
|---:|---:|---:|---:|---|
| 32 | 19.9 ms | 49.8 | 0.28 | fluido |
| 64 | 27.6 ms | 36.0 | 0.28 | fluido |
| **87** | **33.3 ms** | **30.0** | — | ⚠️ **`N_crit^seq`** *(interpolado; medido en N=88 → 29.6 FPS)* |
| 128 | 44.6 ms | 22.3 | 0.43 | ya no cumple el enunciado |
| 192 | 61.6 ms | 16.2 | 0.51 | tirones evidentes |
| 352 | 99.1 ms | 10.1 | — | 10 FPS |
| 512 | 134.9 ms | 7.4 | 0.61 | |
| **848** | **209.8 ms** | **4.8** | — | pase de diapositivas |
| **1000** | **244.1 ms** | **4.10** | 1.4 | 🎯 **N elegido para la demo de OpenMP** |
| 1536 | 348.9 ms | 2.9 | 1.15 | |
| 2048 | 453.9 ms | 2.2 | 1.78 | |
| 3000 | 645.1 ms | 1.6 | 3.23 | |
| 4000 | 849.7 ms | 1.2 | 7.64 | congelado |
| ~4740 | ~1000 ms | 1.0 | — | un segundo por frame *(extrapolado)* |

**El costo es lineal, como predice §2.1.** La pendiente local `b` baja de 0.25 ms/semilla
en N≈100 a 0.205 en N≈3000 (la esfera ya no cabe en L2 pero el acceso se vuelve más
secuencial); el término cuadrático de la física nunca aparece porque `--physics 0` es el
defecto y, aun encendido, §2.1 lo ubica en N≈244,500.

**Nota sobre `T_d`.** El microbenchmark de `make test` da 2.095×10⁹ productos punto/s →
0.137 ms/semilla, pero la pendiente **medida en el render real es 0.205–0.25 ms/semilla**,
un 50–80 % más. La diferencia es lo que el microbenchmark no ve: escritura del
framebuffer, setup por píxel y tráfico de memoria. **Para el informe vale la pendiente
medida, no la del microbenchmark.**

#### Por qué N = 1000 y no otro

El N de la demo tiene que cumplir **dos** condiciones a la vez, no una:

1. **Secuencial visiblemente trabado.** A 4.10 FPS nadie discute que está trabado: son
   244 ms por frame, más de siete veces el presupuesto de 33.3 ms.
2. **Que OpenMP lo pueda rescatar.** Para volver a ≥30 FPS hace falta un speedup de
   `244.1 / 33.3 = 7.32×`. Con 8 P-cores + 4 E-cores y un bucle por píxel que es
   *embarrassingly parallel*, 7–9× es el rango esperable.

Subir a N=2048 haría la demo más dramática pero exigiría **13.6×**, que estas 12 CPUs no
dan: el screensaver seguiría trabado en las dos versiones y se pierde el punto.

> **Plan B.** Si el speedup medido queda por debajo de 7.4×, la demo se corre a **N = 800**
> (210 ms, 4.8 FPS, exige solo 6.3×). Sigue siendo un pase de diapositivas en secuencial.


### 3.6 Matriz de configuraciones y escalado extremo (M4 Pro)

Todo el barrido de §3.5 corrió con los **defaults**: `--cannons 0`, `--physics 0`,
`--voronoi 0`, `--raster 0` — o sea el kernel de **bolitas por raycasting**, sin llenado
ni física. Estos dos experimentos exploran los otros ejes.
Datos: [`datos/nico_m4pro_configs.csv`](../datos/nico_m4pro_configs.csv) y
[`datos/nico_m4pro_escalado_grande.csv`](../datos/nico_m4pro_escalado_grande.csv).

#### A. Costo por configuración, N = 1000, 10 repeticiones

| Configuración | mediana | FPS | × default |
|---|---:|---:|---:|
| `--raster 1` | 4.6 ms | 219.2 | 0.02× |
| `--voronoi 1` | 196.6 ms | 5.09 | 0.83× |
| `--voronoi 1 --physics 1` | 197.9 ms | 5.05 | 0.84× |
| **default (bolitas raycast)** | **236.7 ms** | **4.22** | **1.00×** |
| `--physics 1` | 241.0 ms | 4.15 | 1.02× |
| `--cannons 8 --recirculate 1` | 432.9 ms | 2.31 | 1.83× |
| `--cannons 8 --recirculate 1 --trail 16` | 762.9 ms | 1.31 | 3.22× |
| `--cannons 8 --recirculate 1 --trail 32` | 1276.9 ms | 0.78 | 5.39× |
| `--cannons 32 --recirculate 1 --trail 32` | 2779.7 ms | 0.36 | 11.74× |

Tres resultados que no eran obvios:

1. **`--voronoi 1` es 17 % más BARATO que el default.** El plan A documentado como
   "el kernel caro" no lo es: las bolitas por raycasting cuestan más. Los dos son
   `O(P·N)`, pero el de bolitas paga bounding box y borde suave por esfera.
2. **`--physics 1` es gratis a este N**: +1.8 %. Coherente con §2.1.
3. **El modo cañón es un eje de dificultad independiente de N**, y el más potente que
   tiene el programa: ×11.7 sin tocar `--n`. Son los fantasmas de estela, que se suman
   a las esferas a raycastear. La carga es `N + (K·R/V)·(L/2)` — con K=32, R=60, V=1.5,
   L=32 son **20,480 fantasmas** contra 1,000 semillas.

> ⚠️ **Ojo con el cañón como palanca:** el término `(K·R/V)·(L/2)` es **aditivo y no
> depende de N**. A N=1000 multiplica por 11.7; a N=100,000 multiplica por 1.2. Sirve
> para hundir el programa con N chico, no para empeorar un N ya grande.

#### B. Escalado hasta ~0 FPS, kernel default, 3 repeticiones

| N | mediana | FPS | por frame |
|---:|---:|---:|---:|
| 6,000 | 1,235 ms | 0.810 | 1.2 s |
| 12,000 | 2,414 ms | 0.414 | 2.4 s |
| 16,000 | 3,189 ms | 0.314 | 3.2 s |
| 32,000 | 6,290 ms | 0.159 | 6.3 s |
| 64,000 | 12,763 ms | 0.078 | 12.8 s |
| **100,000** | **19,864 ms** | **0.050** | **19.9 s** |

**El costo es lineal hasta N=100,000 sin una sola desviación**: `b = 0.19818 ms/semilla`
sobre dos órdenes de magnitud. No hay cliff de caché ni rodilla; el kernel es `O(P·N)`
puro y se comporta como tal.

#### C. Corrección a §2.1: el cruce O(N²) está en N ≈ 124,000, no 244,500

Midiendo `--physics 1` contra el mismo N sin física:

| N | sin física | con física | costo física | pares | `c` |
|---:|---:|---:|---:|---:|---:|
| 16,000 | 3,189 ms | 3,615 ms | 426 ms | 2.56×10⁸ | 1.66 ns/par |
| 32,000 | 6,290 ms | 7,877 ms | 1,587 ms | 1.024×10⁹ | 1.55 ns/par |

→ **`T_p` medido = 6.2×10⁸ pares/s**, no los 10⁹ estimados. El cruce real es
`b/c = 0.198×10⁻³ / 1.6×10⁻⁹ ≈ 124,000`.

**La conclusión de §2.1 no cambia** — el render sigue mandando en todo N usable — pero el
número sí, y por un factor de 2. `T_p` ya no es una estimación.

#### D. El costo fijo `a` medido

| N | 1 | 2 | 4 | 8 | 16 |
|---|---:|---:|---:|---:|---:|
| `T` | 3.70 ms | 5.66 ms | 9.01 ms | 12.94 ms | 16.71 ms |

Extrapolando a N=0: **`a` ≈ 2.8 ms**, no los ~1.5 ms supuestos ni los 12 que sugería el
ajuste global de §3.5. Es el valor que va en la fracción serial de Amdahl, y es una
**cota superior**: parte de esos 2.8 ms (relleno de fondo, limpieza del framebuffer)
también se paraleliza.

---

### 3.7 Por qué NO conviene subir el N de la demo

Tentación natural: si a N=1000 el secuencial da 4.22 FPS, ¿por qué no N=100,000 y 0.05 FPS,
que se ve mucho más dramático? Porque **el speedup tiene techo en el número de núcleos**:

```
    FPS_omp(N)  =  FPS_seq(N) × S       con S ≤ p ≈ 8–9.4 en esta máquina
```

Para que la versión paralela vuelva a ≥30 FPS hace falta `FPS_seq ≥ 30/S ≈ 3.2–3.8`.
**Por debajo de eso, paralelizar no rescata nada:** a 0.05 FPS, un 8× deja 0.4 FPS — las
dos versiones se ven trabadas y se pierde toda la demostración.

Proyección con `f = a/T(N)`, `a = 2.8 ms`, y dos escenarios de `p` (`p=8`: solo los
P-cores, conservador; `p=9.4`: 8P + 4E contando los E-cores al ~35 %):

| N | `T_seq` | FPS seq | `f` | `S` (p=8) | FPS omp | `S` (p=9.4) | FPS omp |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 800 | 209.8 ms | 4.77 | 0.0133 | 7.31 | **35.0** ✅ | 8.41 | **40.1** ✅ |
| **1,000** | **236.7 ms** | **4.22** | 0.0118 | 7.39 | **31.2** ✅ | 8.55 | **36.1** ✅ |
| 1,200 | ~280 ms | 3.57 | 0.0100 | 7.48 | 26.7 ❌ | 8.68 | 31.0 ⚠️ |
| 2,048 | 453.9 ms | 2.20 | 0.0062 | 7.63 | 16.8 ❌ | 8.88 | 19.6 ❌ |
| 16,000 | 3,189 ms | 0.31 | 0.0009 | 7.94 | 2.5 ❌ | 9.29 | 2.9 ❌ |
| 100,000 | 19,864 ms | 0.05 | 0.0001 | 7.99 | 0.4 ❌ | 9.36 | 0.5 ❌ |

> **La ventana de la demo se cierra en N ≈ 1,000–1,200.** N=1000 no es un punto tibio:
> es el techo de lo rescatable en esta máquina.

#### Dos N para dos objetivos distintos

| | N | Para qué |
|---|---:|---|
| **N de la demo** | **1,000** | Secuencial roto (4.22 FPS) y OpenMP fluido (31–36 FPS). Es el único régimen donde "el peor del secuencial" se vuelve *bueno* en paralelo. Va al video y a la defensa. |
| **N de la curva de escalabilidad** | **16,000** | Donde el **speedup medido es máximo**: `f = 0.0009` → el techo de Amdahl es 7.94× de 8. Las dos versiones se arrastran, pero el *factor* es el mejor y es el número para el análisis Amdahl/Gustafson del informe. |

**Por qué 16,000 y no 100,000 para la curva:** a N=16,000 la fracción serial ya es 0.0009
y el techo de Amdahl es el 99.3 % del ideal — N=100,000 aporta un 0.6 % más de speedup y
cuesta **6× más medir** (19.9 s por frame; 10 repeticiones de 6 frames son 20 minutos por
punto, solo del lado secuencial). No paga.


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
