/* vec3.h - Vectores 3D, rotaciones e interseccion rayo-esfera.
 * Todo 'static inline': se llama millones de veces por frame y asi el
 * compilador lo funde en el bucle. En float, salvo el angulo aureo. */
#ifndef VEC3_H
#define VEC3_H

#include <math.h>

typedef struct { float x, y, z; } Vec3;

/* ---------------------------------------------------------- construccion -- */
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

/* a + b*s: el patron del raycasting (origen + direccion*t), y emite FMA. */
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

/* Preferirla siempre que solo haya que comparar: sqrt cuesta ~15 ciclos. */
static inline float v3_len2(Vec3 a) { return v3_dot(a, a); }

static inline float v3_len(Vec3 a)  { return sqrtf(v3_dot(a, a)); }

static inline Vec3 v3_norm(Vec3 a)
{
    float l2 = v3_dot(a, a);
    if (l2 <= 1e-20f) return v3_zero();   /* vector nulo: no se puede normalizar */
    return v3_scale(a, 1.0f / sqrtf(l2));
}

/* Componente de 'a' perpendicular a 'n': deja la fuerza sobre el plano
 * tangente para que la semilla no despegue ni se hunda (docs/01 sec 5.2). */
static inline Vec3 v3_reject(Vec3 a, Vec3 n)
{
    return v3_sub(a, v3_scale(n, v3_dot(a, n)));
}

/* Reciben seno y coseno YA calculados: al rotar N semillas con el mismo
 * angulo, sinf/cosf salen una vez fuera del bucle y no N veces adentro. */
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

/* Interseccion rayo-esfera cerrada (Ericson): m = o-c, b = m.d, k = |m|^2-R^2,
 * y con |d| = 1 sale t = -b - sqrt(b^2 - k). Este 'if' es la fuente del
 * desbalance de carga: fallar cuesta ~10 ciclos, pegar dispara el O(N). */
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

/* ------------------------------------------------------------ utilidades -- */
static inline float f_clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Hermite entre e0 y e1: el antialiasing analitico de las celdas de Voronoi. */
static inline float f_smoothstep(float e0, float e1, float x)
{
    float t = f_clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

#endif /* VEC3_H */
