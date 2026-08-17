# Fundamento matemático — *Animated Fibonacci Sphere*

**Proyecto 1 — Computación Paralela y Distribuida (UVG, Sección 20)**
Equipo: Esteban Cárcamo · Nico · Dieguito

**Referencia visual base:** [Let's Code #1 — Animated Fibonacci Sphere](https://youtu.be/HzzMFqpD4FY)

---

## 0. Qué se dibuja y por qué esta escena

El screensaver dibuja una **esfera de Fibonacci**: `N` semillas distribuidas sobre la
superficie de una esfera de manera casi perfectamente uniforme, usando el **ángulo
áureo** `ψ = 137.50776…°`. La esfera rota, las semillas nacen desde un polo y se
reacomodan por repulsión mutua, y cada semilla se pinta como una **celda de Voronoi
esférica** con color pseudoaleatorio e iluminación difusa.

El resultado visual es un objeto que existe en la naturaleza: la cabezuela de un cardo,
un grano de polen, un *Dipsacus*. Las espirales de Fibonacci aparecen solas sobre la
superficie curva sin que las dibujemos.

### 0.1 La regla técnica que sostiene todo el proyecto

> **Si el trabajo pesado lo hace la GPU o la librería de dibujo, no queda nada que
> paralelizar en CPU y el speedup sale plano (~1.0×).**

Por eso, aunque la escena sea 3D, **no usamos OpenGL**. El programa es un
**software renderer**: la CPU calcula el framebuffer completo (`uint32_t[W*H]`) y SDL2
únicamente hace `SDL_UpdateTexture` + `SDL_RenderCopy` + `SDL_RenderPresent`.

Esta decisión es la que convierte el 3D en una **ventaja** en vez de un problema: el
raycasting por píxel es aritmética pura, perfectamente paralelizable, y con una
intensidad computacional mucho mayor que dibujar círculos 2D.

---

## 1. El número áureo y el ángulo áureo

El número áureo es la solución positiva de `φ² = φ + 1`:

```
φ = (1 + √5) / 2 = 1.6180339887498948…
```

El **ángulo áureo** es el que divide una vuelta completa en razón áurea:

```
ψ = 2π / φ²  =  2π · (2 − φ)  =  2.39996322972865332…  rad
             =  137.50776405003785…°
```

En el código lo definimos derivándolo de `φ`, **nunca hard-codeado** como constante
mágica (el enunciado penaliza el hard-coding):

```c
static const double PHI        = 1.6180339887498948482;
static const double GOLDEN_ANG = 2.0 * M_PI / (PHI * PHI);   /* ≈ 2.39996 rad */
```

### 1.1 Por qué un ángulo racional es un desastre

Si el ángulo de divergencia fuera `ψ = (p/q)·2π` con `p/q` racional, entonces:

```
θ_{n+q} = θ_n + p·2π  ≡  θ_n   (mod 2π)
```

La semilla `n+q` cae **exactamente sobre el mismo meridiano** que la semilla `n`. Todas
las semillas se apilan en apenas `q` meridianos y entre ellos queda superficie muerta.
Sobre la esfera esto se ve como una **pelota rayada** con huecos gigantes.

| Ángulo | Fracción de vuelta | Qué se ve |
|---|---|---|
| 120° | 1/3 | 3 meridianos |
| 135° | 3/8 | 8 meridianos |
| 138.46° | 5/13 | 13 meridianos |
| **137.50776°** | **irracional** | **cobertura uniforme, sin huecos** |

> 3/8 y 5/13 son cocientes de Fibonacci: se acercan a `ψ` y por eso "casi" funcionan.
> Esa es justamente la trampa que hace falta explicar.

Esto se demuestra **en vivo** con las teclas `[` y `]`, que barren el ángulo: el patrón
colapsa a rayas y vuelve a explotar en cobertura perfecta al pasar por 137.50776°.

### 1.2 Por qué φ y no cualquier otro irracional

Cualquier irracional evita el apilamiento *exacto*. Pero un irracional que sea *casi*
racional produce apilamiento *casi* exacto, y eso ya se ve feo. Hace falta el número
**más difícil de aproximar por racionales**, y ese es `φ`.

La razón está en su fracción continua:

```
φ = [1; 1, 1, 1, 1, …]      ← todos los términos son 1, el valor mínimo posible
```

En aproximación diofántica, **términos grandes en la fracción continua ⟹ muy buenas
aproximaciones racionales**. Por ejemplo `π = [3; 7, 15, 1, 292, …]`: ese 292 gigante es
lo que hace que `355/113` aproxime a `π` con error de `3×10⁻⁷`. Como `φ` tiene puros
unos, sus aproximaciones racionales son **las peores posibles**.

Sus convergentes son exactamente los cocientes de Fibonacci:

```
1/1, 2/1, 3/2, 5/3, 8/5, 13/8, 21/13, 34/21, 55/34, …  →  φ
```

### 1.3 El teorema de las tres distancias — el resultado que lo cierra

> **Teorema (Steinhaus; probado por Sós, Świerczkowski, Surányi).** Si se colocan `n`
> puntos en la circunferencia en los ángulos `{α, 2α, …, nα}` (mod 1) con `α` irracional,
> los huecos entre puntos consecutivos toman **a lo más 3 longitudes distintas**.

Este es el resultado que hace riguroso todo lo anterior: **el espaciado angular nunca
degenera, para ningún `n`**. Y con `α = 1/φ²` las tres longitudes son lo más parecidas
entre sí que es posible, lo que da el reparto más uniforme que existe.

**Lo verificamos numéricamente en el programa** (`tests/test_sphere.c`): se miden los
huecos angulares para `n = 100, 1000, 5000` y se comprueba que solo hay 3 valores
distintos. Es una verificación experimental de un teorema, hecha por nosotros, y va como
anexo del informe.

---

## 2. De la espiral plana a la esfera

### 2.1 El caso plano (por qué `√n`), como punto de partida

El modelo clásico de Vogel (1979) para un girasol es:

```
θ_n = n·ψ            r_n = c·√n
```

La raíz cuadrada no es un truco: es **la única forma de que la densidad sea constante**.
Si el disco de radio `R` contiene `n` semillas y cada una ocupa área fija `a`:

```
π·R² = n·a    ⟹    R = √(n·a/π) = c·√n
```

### 2.2 El caso esférico — el teorema de Arquímedes es la clave

En 3D queremos lo mismo: **áreas iguales por semilla**. Aquí entra un resultado de
Arquímedes que hace toda la magia, y es lo que hay que saber explicar en la
presentación.

> **Teorema de Arquímedes (el "hat-box").** El área de la franja de una esfera de radio
> `R` comprendida entre dos planos paralelos es igual al área de la franja
> correspondiente del **cilindro circunscrito**. Es decir, la proyección lateral
> esfera → cilindro **preserva el área**.

Consecuencia inmediata, con la esfera unitaria parametrizada por la altura `z ∈ [−1, 1]`:

```
dA = 2π·R·dz          ← el área depende SOLO de dz, no de la latitud
```

**El área es lineal en `z`.** Por lo tanto, para repartir `N` semillas en áreas iguales
basta repartir la coordenada `z` **uniformemente**:

```
z_n = 1 − 2·(n + ½) / N          n = 0, 1, …, N−1
```

El `+½` centra cada semilla en su franja de área `4π/N` (regla del punto medio), en vez
de pegarlas al borde. Es un detalle chico que mejora notablemente la uniformidad en los
polos.

El radio del paralelo a esa altura sale de Pitágoras sobre la esfera unitaria:

```
ρ_n = √(1 − z_n²)
```

Y el ángulo azimutal es el ángulo áureo acumulado:

```
θ_n = n·ψ           (mod 2π)
```

### 2.3 La fórmula completa

```
    z_n = 1 − 2(n + ½)/N
    ρ_n = √(1 − z_n²)
    θ_n = n · ψ                    ψ = 2π/φ²

    p_n = ( ρ_n·cos θ_n ,  ρ_n·sin θ_n ,  z_n )   ∈ S²
```

**Tres líneas.** Eso es la esfera de Fibonacci completa. Y `|p_n| = 1` exactamente, por
construcción:

```
ρ²cos²θ + ρ²sin²θ + z² = ρ²(cos²θ + sin²θ) + z² = ρ² + z² = (1 − z²) + z² = 1   ✔
```

Esa identidad es un **test unitario** en el código: se verifica que `|p_n| = 1` con
tolerancia `1e-6` para toda semilla.

### 2.4 Por qué el `z` uniforme es el análogo exacto del `√n` plano

Vale la pena ver que es el **mismo argumento** en las dos geometrías: en ambos casos se
invierte la función de área acumulada.

| | Disco (2D) | Esfera (3D) |
|---|---|---|
| Elemento de área | `dA = 2π·r·dr` | `dA = 2π·dz` |
| Área acumulada | `A(r) = π·r²` | `A(z) = 2π·(1 − z)` |
| Repartir en `N` partes iguales | `π·r² = n·(a)` | `2π(1−z) = n·(4π/N)` |
| **Se despeja** | **`r_n = c·√n`** | **`z_n = 1 − 2n/N`** |

La raíz cuadrada aparece en 2D porque `A(r)` es cuadrática; **desaparece** en 3D porque
el teorema de Arquímedes vuelve `A(z)` lineal. Es el mismo principio dando dos fórmulas
distintas, y es exactamente el tipo de conexión que hace bueno un informe.

---

## 3. Movimiento — la trigonometría del enunciado

El requisito de *movimiento* y de *elemento de trigonometría* se cumple con la rotación
rígida de la esfera y con el nacimiento progresivo de las semillas.

### 3.1 Rotación en dos ejes

Se compone una rotación en `Y` (giro principal) con una en `X` (cabeceo lento), ambas
función del tiempo:

```
R_y(α) = ⎡ cos α   0   sin α ⎤        R_x(β) = ⎡ 1     0        0    ⎤
         ⎢   0     1     0   ⎥                 ⎢ 0   cos β  −sin β  ⎥
         ⎣ −sin α  0   cos α ⎦                 ⎣ 0   sin β   cos β  ⎦

    p'(t) = R_x(β(t)) · R_y(α(t)) · p          α(t) = ω_y·t,  β(t) = A·sin(ω_x·t)
```

**Optimización que importa:** en vez de rotar los `N` puntos, es más barato **rotar el
rayo de la cámara con la matriz inversa** (que para una rotación es su transpuesta,
`R⁻¹ = Rᵀ`, porque `R` es ortogonal). Así se hacen 9 multiplicaciones por *píxel* en vez
de por *semilla*… salvo que `N ≪ W·H`, en cuyo caso conviene al revés. Medimos las dos.

### 3.2 Crecimiento

El número de semillas visibles crece con el tiempo hasta llegar a `N`:

```
n_visible(t) = min( N , ⌊ t / Δt_nacimiento ⌋ )
```

Como `z_n` depende de `N` (no de `n_visible`), la esfera se "rellena" desde el polo
norte hacia el sur, que es exactamente el efecto del video de referencia.

---

## 4. El renderizado — raycasting por píxel

Este es el **kernel dominante** del programa y la razón de ser del proyecto paralelo.

### 4.1 Cámara y rayo primario

Para cada píxel `(i, j)` se construye un rayo con origen en la cámara y dirección:

```
u = (2·(i + ½)/W − 1) · aspect · tan(fov/2)
v = (1 − 2·(j + ½)/H)          · tan(fov/2)

d⃗ = normalizar( u·right⃗ + v·up⃗ + forward⃗ )
```

### 4.2 Intersección rayo–esfera (forma cerrada, sin raymarching)

La esfera es una cuádrica, así que la intersección es una ecuación de segundo grado
resuelta de forma **exacta y en tiempo constante** — nada de marchar pasos:

```
|o⃗ + t·d⃗ − c⃗|² = R²

    con  m⃗ = o⃗ − c⃗  y  |d⃗| = 1:

    t² + 2(m⃗·d⃗)·t + (|m⃗|² − R²) = 0

    b = m⃗·d⃗ ,   c = |m⃗|² − R²
    Δ = b² − c

    Δ < 0  →  el rayo falla la esfera        (píxel de fondo, ~10 ciclos)
    Δ ≥ 0  →  t = −b − √Δ                    (raíz más cercana)
```

**Este `if` es la fuente del desbalance de carga** que hace interesante la parte
paralela: los píxeles de fondo cuestan ~10 ciclos y los de la silueta cuestan
`O(N)`. Ver `docs/02-PARAMETRO-N.md`.

El punto de impacto y su normal son:

```
q⃗ = o⃗ + t·d⃗            n̂ = (q⃗ − c⃗)/R
```

> Detalle bonito: **sobre la esfera unitaria centrada en el origen, la normal ES el
> punto.** No hay que calcular nada.

### 4.3 Voronoi esférico — el bucle interno

Un cardo o un grano de polen no son puntitos: son **celdas que se tocan**. Matemáticamente
son el **diagrama de Voronoi esférico** del conjunto de semillas, donde la distancia es
la **geodésica** (arco de círculo máximo):

```
d_geo(q̂, ŝ) = R · arccos( q̂ · ŝ )
```

Y aquí está el truco que hace el kernel barato:

> `arccos` es **monótona decreciente** en `[−1, 1]`. Por lo tanto
> **minimizar la distancia geodésica ≡ maximizar el producto punto.**

El bucle interno queda reducido a un producto punto y dos comparaciones — **sin
`arccos`, sin `sqrt`, sin divisiones**:

```c
float best1 = -2.0f, best2 = -2.0f;   /* los dos mayores productos punto */
int   winner = -1;
for (int i = 0; i < n; i++) {
    float dot = qx*sx[i] + qy*sy[i] + qz*sz[i];   /* 3 mul + 2 add */
    if (dot > best1)      { best2 = best1; best1 = dot; winner = i; }
    else if (dot > best2) { best2 = dot; }
}
```

**Guardar los dos mejores** es lo elegante: `best1 − best2` es pequeño exactamente sobre
las fronteras entre celdas, así que da los **bordes con antialiasing analítico, gratis**,
sin ningún detector de aristas:

```
borde = suavizar( (best1 − best2) / w )      /* smoothstep */
```

### 4.4 Iluminación

Modelo de Lambert difuso + especular de Blinn–Phong + un término ambiente:

```
I = k_a  +  k_d·max(0, n̂·l̂)  +  k_s·max(0, n̂·ĥ)^s        ĥ = normalizar(l̂ + v̂)
```

Es aritmética adicional por píxel de silueta: **sube la intensidad computacional**, que
es justo lo que le conviene al proyecto.

### 4.5 Costo del kernel

Sea `P` = número de píxeles que caen dentro de la silueta de la esfera.

```
Costo_render = O(P · N)
```

Con 1280×720 y una esfera que ocupa el 31 % de la pantalla, `P ≈ 2.9×10⁵`. Con `N = 1000`
son **2.9×10⁸ productos punto por frame**. Trabajo de verdad, y **paralelo perfecto**:
cada píxel tiene un único dueño, lee las semillas en modo *read-only*, y no hace falta un
solo `lock`.

---

## 5. La física — el ángulo áureo emerge solo

Todo lo anterior **impone** 137.5°. Pero en la naturaleza nadie le dijo ese número a la
planta: **la planta lo descubre por física**.

En 1992, Douady y Couder dejaron caer gotas de ferrofluido magnetizado en un plato con
aceite y campo magnético. Las gotas se repelen y migran hacia afuera. **Sin programar
nada, se acomodaron espontáneamente en el ángulo áureo.**

Lo reproducimos sobre la esfera. Esto cumple el requisito de **"elemento de física"** del
enunciado, y sobre una superficie esférica el sistema es exactamente el **problema de
Thomson** (cargas puntuales en una esfera minimizando energía), que es un problema
clásico y citable.

### 5.1 Repulsión tipo Coulomb con *softening*

```
F⃗_i = Σ_{j≠i}  k · (p⃗_i − p⃗_j) / ( |p⃗_i − p⃗_j|² + ε² )^{3/2}
```

El `ε` (*softening*) evita la singularidad cuando dos semillas casi coinciden. Costo:
**`O(N²)`**.

> Nota de implementación: **no** usamos la simetría `F_ji = −F_ij` para hacer `N²/2`
> pares. Aunque ahorraría la mitad del trabajo, obliga a escribir en `F_j` desde el hilo
> que procesa `i`, lo que crea una **condición de carrera**. Hacemos el `N²` completo:
> cada hilo escribe solo en *su* `F_i`, y el kernel queda libre de sincronía. Es una
> decisión consciente de *más trabajo a cambio de cero locks*, y va documentada en el
> informe.

### 5.2 Restricción a la superficie

La fuerza se proyecta al **plano tangente** para que la semilla no despegue de la esfera:

```
F⃗_i ← F⃗_i − (F⃗_i · n̂_i)·n̂_i          n̂_i = p⃗_i   (esfera unitaria)
```

### 5.3 Integración de Velocity-Verlet

Integrador de 2º orden, simpléctico, que conserva energía mucho mejor que Euler:

```
p⃗(t+Δt) = p⃗(t) + v⃗(t)·Δt + ½·a⃗(t)·Δt²
a⃗(t+Δt) = F⃗(p⃗(t+Δt))/m − γ·v⃗(t)
v⃗(t+Δt) = v⃗(t) + ½·(a⃗(t) + a⃗(t+Δt))·Δt

p⃗(t+Δt) ← p⃗(t+Δt) / |p⃗(t+Δt)|          ← re-proyectar a la esfera
```

El `γ` es fricción: sin ella el sistema oscila para siempre y nunca se ordena.

### 5.4 Qué se mide en pantalla

Se muestra en vivo el **ángulo de divergencia medio** entre semillas consecutivas y se lo
ve converger a 137.5° solo. Un número que baja `139.2 → 137.9 → 137.6 → 137.51` mientras
el patrón se ordena.

**No programamos el ángulo áureo: programamos que las semillas se empujen, y φ apareció.**

---

## 6. Colores pseudoaleatorios

El enunciado pide **varios colores, idealmente pseudoaleatorios**. Se genera un tono por
semilla mezclando su índice con la semilla global del RNG mediante un *hash* entero
(*bit-mixer* tipo SplitMix64), y se convierte de HSV a RGB:

```c
uint32_t h = hash_u32(i ^ seed);
float hue = (h & 0xFFFF) / 65535.0f;              /* [0,1) */
float sat = 0.55f + 0.35f * (((h >> 16) & 0xFF) / 255.0f);
float val = 0.70f + 0.30f * (((h >> 24) & 0xFF) / 255.0f);
```

Dos propiedades que importan:

1. **Es determinista**: misma `--seed`, mismos colores. Indispensable para poder comparar
   la salida secuencial contra la paralela **bit a bit** (validación por *checksum*).
2. **Es puro** (no tiene estado): se puede llamar desde cualquier hilo sin sincronía. Un
   `rand()` global sería un desastre en paralelo — tiene estado compartido y ni siquiera
   es *thread-safe*.

Y hay un regalo visual: como el color depende del índice `n` y las semillas vecinas en el
espacio están separadas por números de Fibonacci en el índice, **las espirales se pintan
solas de colores contrastantes**. Se vuelven visibles sin dibujarlas.

---

## 7. Verificaciones numéricas (van al informe como anexo)

Todas implementadas en `tests/test_sphere.c` y ejecutables con `make test`:

| # | Qué se verifica | Criterio de aceptación |
|---|---|---|
| 1 | `\|p_n\| = 1` para toda semilla | error máx `< 1e-6` |
| 2 | Uniformidad: distancia al vecino más cercano | coef. de variación `< 0.10` |
| 3 | Cobertura: área de celda de Voronoi | coef. de variación `< 0.15` |
| 4 | **Teorema de las tres distancias** sobre `θ_n` | exactamente 3 huecos distintos |
| 5 | Degeneración con ángulo racional | `ψ = 2π·3/8` ⟹ 8 meridianos detectados |
| 6 | Comparación contra red lat-lon | Fibonacci gana en coef. de variación |

El test #4 es el más valioso: es la **verificación experimental de un teorema** hecha por
nosotros.

---

## 8. Cumplimiento del enunciado, punto por punto

| Requisito del PDF | Cómo se cumple |
|---|---|
| Parámetro **N** | `--n <semillas>`; ver `docs/02-PARAMETRO-N.md` |
| Colores **pseudoaleatorios** | Hash SplitMix64 por índice → HSV → RGB (§6) |
| Canvas **≥ 640×480** | `--width/--height`, por defecto **1280×720**; validado en `args.c` |
| **Movimiento** | Rotación en 2 ejes + nacimiento progresivo de semillas (§3) |
| **Física o trigonometría** | **Ambas**: repulsión de Coulomb + Verlet (§5) *y* esférico→cartesiano, rotaciones, ray–esfera (§2–§4) |
| **FPS en pantalla** | Overlay dibujado sobre el framebuffer + título de ventana |
| Sin hard-coding | Todo por CLI: `n`, ángulo, resolución, seed, física on/off, hilos, scheduler |
| Versión secuencial y paralela | Dos binarios del mismo árbol: `screensaver_seq` / `screensaver_omp` |

---

## 9. Referencias

1. **Vogel, H. (1979).** "A better way to construct the sunflower head". *Mathematical
   Biosciences*, 44(3–4), 179–189. — El modelo `r = c√n`, `θ = nψ`.
2. **Douady, S. & Couder, Y. (1992).** "Phyllotaxis as a physical self-organized growth
   process". *Physical Review Letters*, 68(13), 2098–2101. — El experimento donde φ
   emerge de la física.
3. **Świerczkowski, S. (1959).** "On successive settings of an arc on the circumference
   of a circle". *Fundamenta Mathematicae*, 46, 187–189. — Teorema de las tres distancias.
4. **González, Á. (2010).** "Measurement of areas on a sphere using Fibonacci and
   latitude–longitude lattices". *Mathematical Geosciences*, 42(1), 49–64. — La red de
   Fibonacci esférica y su superioridad medida frente a lat-lon.
5. **Marques, R., Bouville, C., et al. (2013).** "Spherical Fibonacci Point Sets for
   Illumination Integrals". *Computer Graphics Forum*, 32(8), 134–143.
6. **Thomson, J. J. (1904).** "On the structure of the atom…". *Philosophical Magazine*,
   7(39), 237–265. — El problema de Thomson.
7. **Hardy, G. H. & Wright, E. M. (1979).** *An Introduction to the Theory of Numbers*,
   6ª ed. Oxford. — Fracciones continuas y aproximación diofántica (§1.2).
