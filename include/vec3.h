/* ===========================================================================
 *  vec3.h - Aritmetica vectorial 3D, rotaciones e interseccion rayo-esfera.
 *
 *  TODO va como 'static inline' en el header, a proposito: estas funciones se
 *  llaman millones de veces por frame desde el bucle interno del renderizado.
 *  Si estuvieran en un .c aparte, cada llamada seria una llamada real y el
 *  compilador no podria mantener las componentes en registros. Inline permite
 *  que gcc/clang las funda en el bucle y, con suerte, las vectorice.
 *
 *  Se usa 'float' y no 'double' deliberadamente: la mitad de ancho de banda de
 *  memoria, el doble de carriles SIMD, y para coordenadas en la esfera unitaria
 *  la precision sobra (7 digitos significativos sobre valores en [-1, 1]).
 *  Las EXCEPCIONES son los acumuladores del angulo aureo, que si van en double:
 *  n*psi con n grande pierde precision rapido en float y el patron se degrada.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#ifndef VEC3_H
#define VEC3_H

#include <math.h>

typedef struct { float x, y, z; } Vec3;

/* -------------------------------------------------------- construccion ---- */
static inline Vec3 v3(float x, float y, float z)
{
    Vec3 v; v.x = x; v.y = y; v.z = z; return v;
}

static inline Vec3 v3_zero(void) { return v3(0.0f, 0.0f, 0.0f); }

/* ------------------------------------------------------------ aritmetica -- */
static inline Vec3 v3_add(Vec3 a, Vec3 b)
{
    return v3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static inline Vec3 v3_sub(Vec3 a, Vec3 b)
{
    return v3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static inline Vec3 v3_scale(Vec3 a, float s)
{
    return v3(a.x * s, a.y * s, a.z * s);
}

/* a + b*s en una sola operacion: el patron mas comun del raycasting
 * (origen + direccion * t). Ayuda a que el compilador emita FMA. */
static inline Vec3 v3_madd(Vec3 a, Vec3 b, float s)
{
    return v3(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s);
}

static inline float v3_dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline Vec3 v3_cross(Vec3 a, Vec3 b)
{
    return v3(a.y * b.z - a.z * b.y,
              a.z * b.x - a.x * b.z,
              a.x * b.y - a.y * b.x);
}

/* Longitud al cuadrado. Preferirla SIEMPRE que solo haga falta comparar:
 * la raiz cuadrada es monotona, asi que |a| < |b|  <=>  |a|^2 < |b|^2, y
 * sqrt cuesta ~15 ciclos. */
static inline float v3_len2(Vec3 a) { return v3_dot(a, a); }

static inline float v3_len(Vec3 a)  { return sqrtf(v3_dot(a, a)); }

static inline Vec3 v3_norm(Vec3 a)
{
    float l2 = v3_dot(a, a);
    if (l2 <= 1e-20f) return v3_zero();   /* vector nulo: no se puede normalizar */
    return v3_scale(a, 1.0f / sqrtf(l2));
}

/* --------------------------------------------------------------- tangente -- */
/*  Componente de 'a' PERPENDICULAR a 'n' (se asume |n| = 1).
 *
 *      reject(a, n) = a - (a . n) n
 *
 *  Es la operacion clave de la fisica sobre la esfera: la fuerza de repulsion
 *  apunta en cualquier direccion, pero la semilla solo puede moverse sobre la
 *  superficie. Proyectando la fuerza al plano tangente se elimina la
 *  componente radial y la semilla no despega ni se hunde.
 *  (docs/01-FUNDAMENTO-MATEMATICO.md, seccion 5.2)                            */
static inline Vec3 v3_reject(Vec3 a, Vec3 n)
{
    return v3_sub(a, v3_scale(n, v3_dot(a, n)));
}

/* ------------------------------------------------------------- rotaciones -- */
/*  Se reciben el seno y el coseno YA calculados en vez del angulo. Motivo: al
 *  rotar N semillas con el mismo angulo, sinf/cosf se calculan UNA vez fuera
 *  del bucle en vez de N veces adentro. Es la optimizacion de "sacar
 *  invariantes del bucle" (hoisting), y hacerla explicita en la firma impide
 *  que alguien la deshaga por descuido.                                       */
static inline Vec3 v3_rot_x(Vec3 p, float s, float c)
{
    return v3(p.x,
              p.y * c - p.z * s,
              p.y * s + p.z * c);
}

static inline Vec3 v3_rot_y(Vec3 p, float s, float c)
{
    return v3(p.x * c + p.z * s,
              p.y,
             -p.x * s + p.z * c);
}

static inline Vec3 v3_rot_z(Vec3 p, float s, float c)
{
    return v3(p.x * c - p.y * s,
              p.x * s + p.y * c,
              p.z);
}

/* ============================================================================
 *  Interseccion rayo-esfera, en forma cerrada.
 *
 *  La esfera es una cuadrica, asi que NO hace falta raymarching: la
 *  interseccion sale de una ecuacion de segundo grado, exacta y en tiempo
 *  constante.
 *
 *      |o + t*d - c|^2 = R^2       con |d| = 1  y  m = o - c
 *
 *      t^2 + 2(m.d) t + (|m|^2 - R^2) = 0
 *
 *      b = m.d ,  k = |m|^2 - R^2 ,  disc = b^2 - k
 *
 *      disc <  0  ->  el rayo falla la esfera
 *      disc >= 0  ->  t = -b - sqrt(disc)   (la raiz mas cercana)
 *
 *  Notar que al asumir |d| = 1 el coeficiente cuadratico vale 1 y desaparecen
 *  el 'a' y el '2a' de la formula general. Es la version de Christer Ericson
 *  (Real-Time Collision Detection), y ahorra dos divisiones por pixel.
 *
 *  Los dos primeros descartes (k < 0 y b > 0) son casi gratis y eliminan la
 *  mayoria de los rayos antes de tocar el sqrt.
 *
 *  IMPORTANTE PARA LA FASE PARALELA: este 'if' es la fuente del desbalance de
 *  carga. Un rayo que falla cuesta ~10 ciclos; uno que pega cuesta O(N) porque
 *  dispara el bucle de Voronoi. Como la silueta es un circulo en medio de la
 *  pantalla, con schedule(static) los hilos de las filas superiores e
 *  inferiores terminan de inmediato y esperan en la barrera.
 *  (docs/02-PARAMETRO-N.md, seccion 7.1)
 *
 *  Devuelve 1 si hay impacto y escribe la distancia en *t_out; 0 si no.
 * ========================================================================== */
static inline int ray_sphere_hit(Vec3 origin, Vec3 dir, Vec3 center,
                                 float radius, float *t_out)
{
    Vec3  m = v3_sub(origin, center);
    float k = v3_dot(m, m) - radius * radius;
    float b = v3_dot(m, dir);

    /* El origen esta fuera de la esfera y el rayo se aleja de ella. */
    if (k > 0.0f && b > 0.0f) return 0;

    float disc = b * b - k;
    if (disc < 0.0f) return 0;            /* el rayo pasa de largo */

    float t = -b - sqrtf(disc);
    if (t < 0.0f) t = 0.0f;               /* el origen esta adentro */

    if (t_out) *t_out = t;
    return 1;
}

/* ------------------------------------------------------------- utilidades -- */
static inline float f_clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Interpolacion suave de Hermite entre los bordes e0 y e1.
 * Se usa para el antialiasing analitico de las fronteras de Voronoi:
 * (mejor1 - mejor2) es chico exactamente sobre el borde entre dos celdas,
 * asi que pasarlo por smoothstep da el borde suavizado gratis, sin ningun
 * detector de aristas. (docs/01-FUNDAMENTO-MATEMATICO.md, seccion 4.3) */
static inline float f_smoothstep(float e0, float e1, float x)
{
    float t = f_clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

#endif /* VEC3_H */
