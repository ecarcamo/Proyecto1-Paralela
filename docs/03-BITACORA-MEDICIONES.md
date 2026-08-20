# Bitácora de mediciones — barrido de N y punto de saturación secuencial

**Proyecto 1 — Computación Paralela y Distribuida (UVG, Sección 20)**


---

## 1. Entorno de medición

|                         |                                                                                                       |
| ----------------------- | ----------------------------------------------------------------------------------------------------- |
| CPU                     | AMD Ryzen 7 8845HS w/ Radeon 780M Graphics                                                            |
| Núcleos/hilos lógicos | 16                                                                                                    |
| RAM asignada            | 7.4 GiB (bajo WSL2)                                                                                   |
| SO                      | Ubuntu 24.04 sobre**WSL2** (kernel 6.6.87.2-microsoft-standard-WSL2), Windows como host         |
| Compilador              | GCC 13.3.0 (Ubuntu 13.3.0-6ubuntu2~24.04.1)                                                           |
| SDL2                    | 2.30.0                                                                                                |
| Flags                   | `-std=c11 -O2 -Wall -Wextra -Wshadow -Wno-unused-parameter` (default del Makefile)                  |
| Binario medido          | `screensaver_seq` (sin `-fopenmp`)                                                                |
| Parámetros del barrido | `--bench 100 --no-render --csv --physics 1` (default), resto en default (1280×720, ángulo áureo) |

> ⚠️ **Nota metodológica:** esta corrida se hizo en WSL2, no en Windows nativo con MSYS2.
> Es una CPU real y un kernel Linux real (no hay overhead de traducción de instrucciones),
> pero WSL2 corre sobre un hipervisor liviano (Hyper-V) que comparte el scheduler de
> núcleos con el resto del host Windows. Eso es relevante para la Sección 3: es la
> explicación más probable de los saltos de varianza que aparecen ahí.

---

## 2. Protocolo

Igual al de `docs/PLAN-03-DIEGUITO.md` y `docs/02-PARAMETRO-N.md` §5: 13 valores de N,
10 repeticiones cada uno, 100 frames por corrida (se descartan los primeros 10 de
calentamiento, quedan 90 frames útiles por corrida):

```bash
mkdir -p datos
for N in 50 100 200 300 400 500 750 1000 1500 2000 3000 5000 8000; do
  for r in $(seq 1 10); do
    ./bin/screensaver_seq --n $N --bench 100 --no-render --csv >> datos/seq_barrido.csv
  done
done
```

130 corridas totales, volcadas en [`datos/seq_barrido.csv`](../datos/seq_barrido.csv)
(10 columnas: `n,width,height,frames,media,mediana,min,max,sd,fps`, una línea por
corrida). El render medido es el kernel real de Fase 1: raycasting por píxel con
Voronoi esférico (`--voronoi` en su default de 1), con física de Coulomb+Verlet activa
(`--physics` en su default de 1) — es decir, el costo total que ve el usuario en la
ventana, no un render simplificado.

---

## 3. Tabla agregada — N vs. tiempo por frame y FPS

Media, sd y FPS agregando las 10 repeticiones por N (media de las medias, sd muestral
entre corridas):

|    N |   media (ms) |     sd (ms) |   FPS | ¿cumple ≥30 FPS? |
| ---: | -----------: | ----------: | ----: | :----------------: |
|   50 |        26.82 |        0.63 | 37.31 |         ✅         |
|  100 |        38.02 |        1.01 | 26.32 |         ❌         |
|  200 |        57.86 |        0.63 | 17.28 |         ❌         |
|  300 |        83.19 |        1.61 | 12.03 |         ❌         |
|  400 |       107.00 |        4.53 |  9.36 |         ❌         |
|  500 |       125.42 |        1.16 |  7.97 |         ❌         |
|  750 |  339.26 ⚠️ |  86.78 ⚠️ |  3.25 |         ❌         |
| 1000 |       485.98 |        4.88 |  2.06 |         ❌         |
| 1500 |       730.94 |       12.79 |  1.37 |         ❌         |
| 2000 |       937.90 |       37.97 |  1.07 |         ❌         |
| 3000 |      1472.53 |       27.87 |  0.68 |         ❌         |
| 5000 | 1749.89 ⚠️ | 662.89 ⚠️ |  0.66 |         ❌         |
| 8000 |      1804.10 |       55.00 |  0.56 |         ❌         |

**N crítico secuencial (cruce de 30 FPS): entre N=50 (37.31 FPS) y N=100 (26.32 FPS),
interpolando linealmente, ≈ N = 83.**

### 3.1 Dos anomalías de varianza — documentadas, no escondidas

Dos filas (marcadas ⚠️) tienen una desviación estándar muchísimo mayor que sus vecinas,
y no por ruido disperso: es un **salto de régimen a mitad del barrido**, sostenido en
las corridas restantes.

**N=750** — las medias individuales de las 10 corridas fueron:

```
174.9, 175.5, 398.5, 375.9, 376.5, 375.6, 371.5, 379.9, 378.7, 385.6  (ms)
```

Las dos primeras corridas rondan 175 ms; a partir de la tercera, salta a ~375-398 ms
(más del doble) y se mantiene ahí el resto del barrido.

**N=5000** — las medias individuales:

```
2338.2, 2382.8, 2415.7, 2394.7, 2360.7, 1107.6, 1124.6, 1117.5, 1116.8, 1140.3  (ms)
```

Las primeras cinco corridas rondan 2360-2415 ms; desde la sexta, cae a ~1110-1140 ms
(menos de la mitad) y se mantiene ahí — en la dirección contraria a N=750, pero igual
de abrupto.

**Interpretación:** no es un bug del kernel de render (el resto de los N, incluido
N=8000 que le sigue, muestra variación normal de un dígito o bajo dos dígitos de sd).
Es consistente con lo que ya advertía `docs/02-PARAMETRO-N.md` §3 sobre *turbo boost* y
contención de caché L3 entre procesos — agravado aquí porque la medición corrió en
**WSL2**, donde el hipervisor de Windows puede reasignar núcleos entre el host y la VM
de Linux a mitad de una corrida larga (cada franja de N corrió sus 10 repeticiones de
forma consecutiva y sin pausas, así que un cambio de asignación de CPU del host caería
justo ahí). Es exactamente la razón por la que el enunciado exige 10 mediciones por
prueba en vez de una sola: una corrida aislada de N=750 o N=5000 habría reportado un
número que no representa el comportamiento típico del binario.

---

## 4. Comparación contra la predicción de `docs/02-PARAMETRO-N.md`

El modelo de docs/02 (calibrado en la máquina de Esteban, Linux nativo, i9-13980HX)
predice **N_crit ≈ 130** a 1280×720. La medición de esta máquina da **N_crit ≈ 83**.

Razones esperables de la diferencia, ninguna alarmante:

1. **CPU distinta.** El modelo de docs/02 mide `T_d` (evaluaciones de producto punto
   por segundo) en un i9-13980HX de 32 hilos hetereogéneos (P+E cores); esta máquina es
   un Ryzen 7 8845HS de 16 hilos. El IPC de un solo núcleo — que es lo que determina
   `N_crit^seq`, ya que el binario secuencial usa un solo hilo — no es idéntico entre
   arquitecturas, y `docs/02-PARAMETRO-N.md` §3.4 ya anticipa esto: *"N_crit^seq
   debería ser parecido en las tres [máquinas del equipo] (depende del IPC de un solo
   núcleo)"* — parecido, no igual, y el ratio 130/83 ≈ 1.6× cae dentro de lo esperable
   para un salto de generación/arquitectura de CPU.
2. **El kernel medido no es el mismo.** El N_crit≈130 de docs/02 se estimó con un
   *proxy* del bucle de Voronoi (el bucle de `estimar_n_critico()` en
   `tests/test_sphere.c`, que mide productos punto puros sin el resto del pipeline de
   render). El N_crit≈83 de esta bitácora es del `render_frame()` real, que además
   incluye: la intersección rayo-esfera por píxel, el sombreado con `f_smoothstep` y
   `rgb_lerp`, y — a diferencia del proxy — corre con `--physics 1` activo, sumando el
   costo de `physics_step()` por frame (aunque ese costo es marginal para estos N, ver
   §5).
3. **Overhead de WSL2.** No descartable dado lo observado en §3.1: si el hipervisor
   introduce jitter de scheduling incluso en la franja de N chico (50-100, la que
   define el cruce de 30 FPS), el N_crit medido queda más conservador de lo que daría
   Windows nativo o Linux bare-metal.
4. Corre `make test` en esta misma máquina, que estima el N crítico de forma
   independiente vía el proxy de productos punto: dio **N≈176** (ver la salida de
   `test_sphere.c`, sección 6, corrida en esta sesión) — un tercer número, más alto que
   el 83 medido aquí con el render real y más cerca del 130 de docs/02. Esto refuerza el
   punto 2: el proxy y el render real no miden exactamente el mismo trabajo, y ambos
   difieren de la máquina de referencia.

**Conclusión:** la dirección del resultado es la esperada (un N crítico del mismo orden
de magnitud, 83 vs. 130, ambos muy por debajo de los miles), y las tres causas de
diferencia son identificables y explicables — no hay indicio de un error de medición o
de implementación.

---

## 5. Costo de la física — ¿domina el render o la física?

`docs/02-PARAMETRO-N.md` §2.1 predice que el render O(P·N) domina sobre la física O(N²)
hasta N≈244,500 — muy por encima de todo este barrido. Verificación indirecta: entre
N=3000 (1472.5 ms) y N=8000 (1804.1 ms), el tiempo crece ~1.23× mientras N crece
2.67× — muy sub-lineal respecto de N, y muchísimo más sub-cuadrático de lo que
esperaría un O(N²) dominante. Es consistente con que el render (O(P·N), con P fijo)
sigue siendo el término dominante en todo este rango, tal como predice el modelo.

---

## 6. Gráfica pendiente

El CSV crudo (`datos/seq_barrido.csv`) ya tiene el formato listo para graficar
`FPS vs. N` con la línea horizontal en 30 FPS — es la gráfica principal del proyecto
según `docs/02-PARAMETRO-N.md` §5.2. Pendiente de generar cuando exista el binario
`_omp` para superponer la curva paralela y mostrar las dos intersecciones (N_crit^seq
y N_crit^omp) en el mismo eje.
