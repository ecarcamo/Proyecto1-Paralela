# PLAN 03 — DIEGUITO  ·  Voronoi, física y la medición del N crítico

> ⚠️ Este archivo **no se versiona**. Es coordinación interna del equipo.

**Turno:** martes 18 en la madrugada / miércoles 19 temprano — **cerrás antes de la
clase.**
**Máquina:** Windows.
**Estado al terminar:** 🎯🎯 **screensaver secuencial COMPLETO + la tabla de N vs FPS.**
**Commits sugeridos:** 4.

---

## Antes de escribir una línea

### ⚠️ Windows: usá MSYS2, no Visual Studio

MSVC no soporta bien OpenMP moderno ni los `#pragma` que vamos a usar en la fase 2, y el
`Makefile` está escrito para GCC/Clang. Instalá MSYS2 y desde la terminal **MINGW64**:

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 make
```

Después:

```bash
git pull
make && ./bin/screensaver_seq --n 500     # tenes que ver la esfera de Nico girando
```

Si no compila, corré `make print-config` y mandá la salida al grupo **antes** de ponerte
a pelear solo. No perdás tu turno en configuración.

---

## Tu turno es el que cierra el checkpoint

Sos el que deja el proyecto en estado presentable. Cuando termines, tenemos:

1. Un screensaver secuencial que cumple **todos** los requisitos del enunciado
2. La medición que **justifica** que haya que paralelizarlo

Eso es exactamente el punto donde el enunciado dice que hay que estar:

> *"Comenzarán diseñando y programando la versión secuencial. **Una vez su versión
> secuencial esté lista (funcional y corriendo), procederán a buscar acelerarla** y
> mejorarla utilizando OpenMP."*

O sea: llegar con el secuencial terminado y el cuello de botella medido **no es ir
atrasado, es ir exactamente en el paso que el PDF prescribe.**

---

## Lo que se entrega

```
src/render.c        ← reemplazás render_frame() por raycasting + Voronoi
include/physics.h  src/physics.c    ← repulsión O(N²) + Verlet
include/bench.h    src/bench.c      ← modo headless + CSV
datos/seq_barrido.csv               ← la medición
docs/03-BITACORA-MEDICIONES.md      ← la tabla y el análisis
```

---

## Los 4 commits

| # | Mensaje | Contenido |
|:-:|---|---|
| 1 | `[render] raycasting por pixel con Voronoi esferico e iluminacion` | El kernel dominante |
| 2 | `[physics] repulsion tipo Coulomb sobre la esfera con Verlet` | Douady–Couder |
| 3 | `[bench] modo headless con estadisticas y salida CSV` | El instrumento de medida |
| 4 | `[docs] bitacora: barrido de N y punto de saturacion secuencial` | 🎯 el resultado |

---

## Detalle de cada pieza

### 1. `render_frame()` — el raycasting con Voronoi

Todo está deducido en `docs/01-FUNDAMENTO-MATEMATICO.md` §4. Resumen operativo:

```c
for (int j = 0; j < fb->h; j++)
for (int i = 0; i < fb->w; i++) {
    Vec3 d = camera_ray(&cam, i, j, fb->w, fb->h);

    float t;
    if (!ray_sphere_hit(cam.origin, d, center, R, &t)) {   /* ya existe en vec3.h */
        fb->px[j*fb->w + i] = BACKGROUND;
        continue;                                          /* ~10 ciclos */
    }

    Vec3 q = v3_add(cam.origin, v3_scale(d, t));
    Vec3 nrm = v3_norm(v3_sub(q, center));      /* sobre la esfera unitaria, la normal ES el punto */

    /* --- Voronoi esferico: el bucle interno, O(N) --- */
    float best1 = -2.0f, best2 = -2.0f;
    int   winner = -1;
    for (int k = 0; k < s->n; k++) {
        float dot = nrm.x*s->x[k] + nrm.y*s->y[k] + nrm.z*s->z[k];
        if      (dot > best1) { best2 = best1; best1 = dot; winner = k; }
        else if (dot > best2) { best2 = dot; }
    }

    float edge  = smoothstep(0.0f, EDGE_W, best1 - best2);  /* antialias analitico gratis */
    float lambert = fmaxf(0.0f, v3_dot(nrm, light_dir));
    fb->px[j*fb->w + i] = shade(s->color[winner], lambert, edge);
}
```

Tres cosas que **no** hay que hacer:

- ❌ **No uses `acos()`.** Es monótona decreciente, así que maximizar el producto punto ya
  te da el mínimo de la distancia geodésica. Un `acos` por semilla por píxel te mataría
  el rendimiento por nada.
- ❌ **No metas `#pragma omp`.** Todavía no. Necesitamos el baseline secuencial honesto,
  y si lo paralelizás ahora perdemos la medición que justifica todo el proyecto.
- ❌ **No optimices con rejilla espacial.** Esa es la mejora "v5" de la semana que viene, y
  vale como entregable propio (el enunciado pide *"mejoras y modificaciones iterativas…
  para obtener mejores versiones"*). Si la metés ahora, se pierde la comparación.

> Sí, te estoy pidiendo que escribas código lento a propósito. **Ese es el punto del
> proyecto:** sin un secuencial lento y medido, el speedup no significa nada.

Respetá `cfg->voronoi`: si viene en `0`, dejá el render de puntos de Nico (nos sirve como
plan B si los FPS se caen del todo en alguna máquina).

### 2. `src/physics.c` — el requisito de física

Doc 1 §5. Repulsión de Coulomb con *softening*, proyección al plano tangente,
Velocity-Verlet, re-normalización a la esfera.

```c
void physics_step(SeedSet *s, const PhysicsParams *p, double dt);
```

**Importante:** hacé el `N²` **completo**, no `N²/2`. Aprovechar la simetría
`F_ji = −F_ij` ahorraría la mitad del trabajo pero obliga a escribir en `F_j` desde la
iteración de `i`, lo que en la fase paralela es una **condición de carrera**. Con el `N²`
completo cada semilla escribe solo en su propia fuerza y el kernel queda libre de
sincronía. Es una decisión consciente y va documentada.

Sacá también el **ángulo de divergencia medio** entre semillas consecutivas y pasáselo al
overlay: verlo converger a 137.5° solo es el mejor momento de la presentación.

### 3. `src/bench.c` — el instrumento

```bash
./bin/screensaver_seq --n 1000 --bench 300 --no-render --csv
```

- Corre `K` frames sin ventana
- **Descarta los primeros 10** (calentamiento de caché)
- Reporta media, mediana, min, max, desviación estándar y FPS
- Con `--csv`, una línea por corrida lista para graficar

El enunciado exige **mínimo 10 mediciones por prueba** (Anexo 3), así que el script hace
10 repeticiones por cada valor de N.

### 4. La medición — tu entregable estrella

```bash
mkdir -p datos
for N in 50 100 200 300 400 500 750 1000 1500 2000 3000 5000 8000; do
  for r in $(seq 1 10); do
    ./bin/screensaver_seq --n $N --bench 100 --no-render --csv >> datos/seq_barrido.csv
  done
done
```

En `docs/03-BITACORA-MEDICIONES.md` poné:

1. La tabla `N | media ms | sd | FPS`
2. **El `N` donde cruza los 30 FPS** ← el número que importa
3. La comparación contra la predicción de `docs/02-PARAMETRO-N.md` §3
   (el modelo predice `N_crit ≈ 330`; si tu Windows da otro número, **está bien** — lo
   interesante es explicar por qué)
4. Specs de tu máquina (CPU, núcleos, RAM, versión de GCC)

---

## Cómo verificar que tu turno quedó bien

```bash
./bin/screensaver_seq --n 300              # fluido y bonito
./bin/screensaver_seq --n 3000             # visiblemente trabado, FPS en rojo
./bin/screensaver_seq --n 1000 --bench 100 --no-render
```

---

## 🎓 El guion para el catedrático

Cuando terminen tu turno, esto es lo que se enseña. Cinco minutos.

**1. "Este es nuestro screensaver."**
`./bin/screensaver_seq --n 300` → esfera de Fibonacci girando, colores
pseudoaleatorios, 30+ FPS en pantalla.

**2. "La matemática está verificada, no solo dibujada."**
`make test` → la uniformidad medida y el **teorema de las tres distancias comprobado
numéricamente**.

**3. "Encontramos dónde se cae."**
La gráfica de `N` vs FPS con la línea de 30 FPS. `./bin/screensaver_seq --n 3000` → se
traba en vivo y los FPS se ponen rojos.

**4. "Y ya sabemos exactamente qué paralelizar."**
El kernel de Voronoi es el 90 % del frame, es un *map* 2D sin colisiones de escritura, y
el desbalance geométrico de la silueta ya nos dice que va a ser un caso de
`schedule(dynamic)`. Esa es la semana que viene.

**Lo que demuestra ese guion:** que hicimos el análisis **antes** de paralelizar, que es
literalmente el método PCAM que el curso evalúa — y no "le tiramos `#pragma omp parallel
for` a ver si acelera", que es lo que va a hacer medio salón.

---

## Después del checkpoint (para que nadie pregunte "¿y ahora?")

| Versión | Qué se hace | Ganancia esperada |
|---|---|---|
| v1 | `#pragma omp parallel for collapse(2)` en el Voronoi | ~15–20× |
| v2 | Comparar `static` / `dynamic` / `guided` + tamaño de *chunk* | +10–20 % |
| v3 | Paralelizar la física `O(N²)` con `reduction` | marginal (§2.1 del doc 2) |
| v4 | Sacar invariantes del bucle, SoA, alineación, `simd` | +20–40 % |
| v5 | **Rejilla espacial** `O(P·N)` → `O(P·k)` | ~100× (y **cambia el mejor scheduler**) |
