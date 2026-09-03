/* render.c - Framebuffer, camara y los tres kernels de dibujado. */
#include "render.h"

#include <math.h>
#include <stdlib.h>

#include "color.h"

#define RENDER_BG  0xFF0A0A12u   /* negro con un toque frio */

/* Banda de antialiasing entre celdas: se deriva de N porque best1-best2 ~ 8/N. */
#define EDGE_FRAC  0.15f
#define EDGE_MAX   0.05f

/* Debajo de este N los bucles O(N) del preambulo no justifican el fork/join. */
#define RENDER_PAR_MIN 256

static inline float edge_width_for(int n)
{
    if (n < 1) n = 1;
    float w = EDGE_FRAC * 8.0f / (float)n;
    return (w > EDGE_MAX) ? EDGE_MAX : w;
}

/* --------------------------------------------------------- framebuffer --- */
int fb_alloc(Framebuffer *fb, int w, int h)
{
    if (fb == NULL || w <= 0 || h <= 0) return -1;

    fb->px = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (fb->px == NULL) {
        fb->w = fb->h = 0;
        return -2;
    }
    fb->w = w;
    fb->h = h;
    return 0;
}

void fb_free(Framebuffer *fb)
{
    if (fb == NULL) return;
    free(fb->px);
    fb->px = NULL;
    fb->w = fb->h = 0;
}

void fb_clear(Framebuffer *fb, uint32_t argb)
{
    if (fb == NULL || fb->px == NULL) return;
    const size_t count = (size_t)fb->w * (size_t)fb->h;
    for (size_t i = 0; i < count; ++i) fb->px[i] = argb;
}

/* -------------------------------------------------------------- camara --- */
Camera camera_make(const Config *cfg)
{
    Camera cam;

    /* FOV vertical fijo; la distancia se deriva de el y de sphere_frac. */
    const float fov_v      = 50.0f * (float)SS_PI / 180.0f;
    cam.tan_half_fov       = tanf(fov_v * 0.5f);

    float frac = (cfg != NULL) ? (float)cfg->sphere_frac : (float)SS_DEF_FILL;
    if (frac <= 0.0f) frac = (float)SS_DEF_FILL;

    float dist = 1.0f / (frac * cam.tan_half_fov);
    if (dist < 1.5f) dist = 1.5f;   /* nunca meter la camara dentro de la esfera */

    cam.origin  = v3(0.0f, 0.0f, dist);
    cam.forward = v3(0.0f, 0.0f, -1.0f);   /* mira hacia el origen */
    cam.right   = v3(1.0f, 0.0f, 0.0f);
    cam.up      = v3(0.0f, 1.0f, 0.0f);
    return cam;
}

Vec3 camera_ray(const Camera *cam, int i, int j, int w, int h)
{
    /* Centro del pixel a coordenadas normalizadas [-1,1], con Y hacia arriba. */
    float aspect = (float)w / (float)h;
    float ndc_x  = ((i + 0.5f) / (float)w) * 2.0f - 1.0f;
    float ndc_y  = 1.0f - ((j + 0.5f) / (float)h) * 2.0f;

    float px = ndc_x * cam->tan_half_fov * aspect;
    float py = ndc_y * cam->tan_half_fov;

    Vec3 dir = cam->forward;
    dir = v3_madd(dir, cam->right, px);
    dir = v3_madd(dir, cam->up,    py);
    return v3_norm(dir);
}

/* ------------------------------------------------------------ bolitas ---- */
/* Luz en el espacio del disco: x derecha, y abajo, z hacia el ojo. */
#define BALL_LX  (-0.45f)
#define BALL_LY  (-0.55f)
#define BALL_LZ   (0.70f)
#define BALL_SPEC_POW  28.0f   /* dureza del brillo especular */
#define BALL_SPEC_K    0.55f   /* cuanto blanco mete el brillo, 0..1 */
#define BALL_FILL      1.60f   /* algo menos que el 1.9/sqrt(N) que las hace tocarse */

/* Radio de la bolita en unidades de mundo; unico para los tres kernels. */
static float ball_world_radius(int n)
{
    if (n < 1) n = 1;
    float r = BALL_FILL / sqrtf((float)n);
    return (r > 0.5f) ? 0.5f : r;      /* con N chico, que no se coma la esfera */
}

/* Color de las N semillas en el instante t; NULL = usar los fijos del SeedSet. */
static uint32_t *frame_colors(const SeedSet *s, const Config *cfg, double t)
{
    if (cfg == NULL || cfg->color_speed == 0.0 || s->n < 1) return NULL;

    uint32_t *c = (uint32_t *)malloc((size_t)s->n * sizeof(uint32_t));
    if (c == NULL) return NULL;

    ColorAnim anim = { (float)cfg->color_speed, (float)cfg->color_spread };

    /* color_for_seed_at() es pura: se recalcula cada frame en vez de acumularse. */
    #pragma omp parallel for schedule(static) if(s->n >= RENDER_PAR_MIN)
    for (int i = 0; i < s->n; ++i)
        c[i] = color_for_seed_at((uint32_t)i, cfg->seed, &anim, t);

    return c;
}

/* Rasteriza una semilla como esferita: normal nz = sqrt(1-u^2-v^2), z-buffer. */
static void draw_ball(Framebuffer *fb, float *zbuf, float cxf, float cyf, float rf,
                      float depth, float r_world, uint32_t color, float k_world)
{
    const int w = fb->w, h = fb->h;

    /* Caja de la bolita, con medio pixel de margen para la banda de cobertura. */
    int x0 = (int)floorf(cxf - rf - 0.5f), x1 = (int)ceilf(cxf + rf + 0.5f);
    int y0 = (int)floorf(cyf - rf - 0.5f), y1 = (int)ceilf(cyf + rf + 0.5f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= w) x1 = w - 1;
    if (y1 >= h) y1 = h - 1;

    const float inv_rf = (rf > 1e-6f) ? (1.0f / rf) : 0.0f;
    const float rlim  = rf + 0.5f;          /* borde exterior de la banda */
    const float rlim2 = rlim * rlim;
    const float z_min = depth - r_world;    /* lo mas cerca que puede estar */

    /* Vector medio de Blinn-Phong (L + V, V = (0,0,1)): igual para todas. */
    const float hx = BALL_LX, hy = BALL_LY, hz = BALL_LZ + 1.0f;
    const float hinv = 1.0f / sqrtf(hx * hx + hy * hy + hz * hz);
    const float Hx = hx * hinv, Hy = hy * hinv, Hz = hz * hinv;

    for (int y = y0; y <= y1; ++y) {
        float dy = (float)y - cyf;
        for (int x = x0; x <= x1; ++x) {
            float dx = (float)x - cxf;

            /* Dos descartes baratos antes del sqrt. */
            float d2 = dx * dx + dy * dy;
            if (d2 >= rlim2) continue;

            int k = y * w + x;
            if (z_min >= zbuf[k]) continue;

            /* Cobertura: rampa lineal de un pixel, permite radio fraccionario. */
            float d = sqrtf(d2);
            float cov = rf - d + 0.5f;
            if (cov > 1.0f) cov = 1.0f;

            /* Normal de la esferita; en la banda de cobertura se toma el borde. */
            float un = dx * inv_rf, vn = dy * inv_rf;
            float rr = un * un + vn * vn;
            float nz = (rr < 1.0f) ? sqrtf(1.0f - rr) : 0.0f;

            /* Profundidad propia: el polo esta r_world mas cerca que el centro. */
            float z = depth - nz * r_world;

            if (z >= zbuf[k]) continue;                /* algo mas cerca ya esta */

            /* Difusa local modulada por k_world (lado de la esfera grande). */
            float nd = un * BALL_LX + vn * BALL_LY + nz * BALL_LZ;
            if (nd < 0.0f) nd = 0.0f;
            float shade = k_world * (0.25f + 0.75f * nd);

            float nh = un * Hx + vn * Hy + nz * Hz;
            float spec = 0.0f;
            if (nh > 0.0f) spec = powf(nh, BALL_SPEC_POW) * BALL_SPEC_K * k_world;

            uint32_t px = rgb_mul(color, shade);
            if (spec > 0.004f) px = rgb_lerp(px, 0xFFFFFFFFu, spec);

            /* Solo tapa (escribe z) si cubre mas de medio pixel. */
            if (cov < 1.0f)  px = rgb_lerp(fb->px[k], px, cov);
            if (cov >= 0.5f) zbuf[k] = z;

            fb->px[k] = px;
        }
    }
}

/* --raster 1: bolitas rasterizadas. Costo ~O(1) en N y con carrera en el
 * z-buffer si se paralelizara por semillas: es plan B, no el kernel a medir. */
static void render_points(Framebuffer *fb, const SeedSet *s, const Config *cfg, double t)
{
    const int w = fb->w, h = fb->h;
    fb_clear(fb, RENDER_BG);

    /* z-buffer del frame: se pide y se libera aca, sin estado global. */
    float *zbuf = (float *)malloc((size_t)w * (size_t)h * sizeof(float));
    if (zbuf == NULL) return;                 /* sin z-buffer no dibujamos, no crash */
    const size_t npx = (size_t)w * (size_t)h;
    for (size_t i = 0; i < npx; ++i) zbuf[i] = INFINITY;

    uint32_t       *anim_col = frame_colors(s, cfg, t);
    const uint32_t *col      = (anim_col != NULL) ? anim_col : s->color;

    Camera cam = camera_make(cfg);
    const float aspect = (float)w / (float)h;
    const float cam_dist = v3_len(cam.origin);   /* la camara mira al origen */

    /* Giro por tiempo en Y, mas inclinacion fija en X para el encuadre. */
    const float ang  = (float)(t * ((cfg != NULL) ? cfg->rot_speed : SS_DEF_ROT_SPEED));
    const float sy   = sinf(ang),  cyv = cosf(ang);
    const float tilt = 0.42f;
    const float stx  = sinf(tilt), ctx = cosf(tilt);

    const Vec3 light = v3_norm(v3(-0.4f, 0.6f, 0.7f));   /* luz fija en el mundo */

    const float sphere_px = (cfg != NULL ? (float)cfg->sphere_frac : (float)SS_DEF_FILL)
                            * (float)h * 0.5f;
    float radius = ball_world_radius(s->n) * sphere_px;
    if (radius < 0.5f) radius = 0.5f;      /* mas chico que esto ya no se ve */

    for (int idx = 0; idx < s->n; ++idx) {
        Vec3 p = seed_pos(s, idx);
        p = v3_rot_y(p, sy, cyv);
        p = v3_rot_x(p, stx, ctx);

        /* A coordenadas de camara. */
        Vec3 rel = v3_sub(p, cam.origin);
        float cz = v3_dot(rel, cam.forward);          /* profundidad, >0 al frente */
        if (cz <= 1e-3f) continue;                    /* detras del ojo */
        float cxp = v3_dot(rel, cam.right);
        float cyp = v3_dot(rel, cam.up);

        /* Proyeccion perspectiva a pixeles. */
        float ndc_x = (cxp / cz) / (cam.tan_half_fov * aspect);
        float ndc_y = (cyp / cz) / cam.tan_half_fov;
        float sx    = (ndc_x * 0.5f + 0.5f) * (float)w;
        float sy_px = (1.0f - (ndc_y * 0.5f + 0.5f)) * (float)h;

        /* Sobre la esfera unitaria la normal ES la posicion. */
        float diff = v3_dot(p, light);
        if (diff < 0.0f) diff = 0.0f;
        float k = 0.28f + 0.72f * diff;               /* piso ambiente + difusa */

        /* Radio con perspectiva, en float: redondearlo dibujaba un anillo. */
        float rf = radius * (cam_dist / cz);
        float r_world = radius / sphere_px;

        draw_ball(fb, zbuf, sx, sy_px, rf, cz, r_world, col[idx], k);
    }

    free(anim_col);
    free(zbuf);
}

/* Rota las N semillas una vez por frame; rr[i] = |p|^2 (opcional, modo canon). */
static void rotate_seeds(const SeedSet *s, float sy, float cyv, float stx, float ctx,
                         float *rx, float *ry, float *rz, float *rr)
{
    /* Sin dependencias y carga uniforme: static exacto. */
    #pragma omp parallel for schedule(static) if(s->n >= RENDER_PAR_MIN)
    for (int i = 0; i < s->n; ++i) {
        Vec3 p = seed_pos(s, i);
        p = v3_rot_y(p, sy, cyv);
        p = v3_rot_x(p, stx, ctx);
        rx[i] = p.x;
        ry[i] = p.y;
        rz[i] = p.z;
        if (rr != NULL) rr[i] = v3_len2(p);
    }
}

/* --voronoi 1: celdas de Voronoi esferico por pixel, O(P*N). Ver docs/01 sec 4. */
static void render_raycast(Framebuffer *fb, const SeedSet *s, const Config *cfg, double t)
{
    const int w = fb->w, h = fb->h;

    /* Semillas rotadas de este frame. */
    float *rx = (float *)malloc((size_t)s->n * sizeof(float));
    float *ry = (float *)malloc((size_t)s->n * sizeof(float));
    float *rz = (float *)malloc((size_t)s->n * sizeof(float));
    float *rr = (float *)malloc((size_t)s->n * sizeof(float));
    if (rx == NULL || ry == NULL || rz == NULL || rr == NULL) {
        free(rx); free(ry); free(rz); free(rr);
        return;                                        /* sin memoria, no crash */
    }

    /* Una vez por frame, no por pixel: el bucle caliente solo indexa col[]. */
    uint32_t       *anim_col = frame_colors(s, cfg, t);
    const uint32_t *col      = (anim_col != NULL) ? anim_col : s->color;

    const float ang  = (float)(t * ((cfg != NULL) ? cfg->rot_speed : SS_DEF_ROT_SPEED));
    const float sy   = sinf(ang),  cyv = cosf(ang);
    const float tilt = 0.42f;                 /* mismo encuadre que los otros modos */
    const float stx  = sinf(tilt), ctx = cosf(tilt);
    rotate_seeds(s, sy, cyv, stx, ctx, rx, ry, rz, rr);

    Camera cam = camera_make(cfg);
    const Vec3 center = v3(0.0f, 0.0f, 0.0f);
    const float radius = 1.0f;                          /* esfera unitaria */

    const Vec3 light = v3_norm(v3(-0.4f, 0.6f, 0.7f));  /* la luz de siempre */
    const float edge_w = edge_width_for(s->n);

    /* Mismo schedule que render_balls_raycast (ver la tabla medida ahi). */
    #pragma omp parallel for schedule(dynamic, 1)
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            Vec3 d = camera_ray(&cam, i, j, w, h);

            float thit;
            if (!ray_sphere_hit(cam.origin, d, center, radius, &thit)) {
                fb->px[j * w + i] = RENDER_BG;
                continue;                               /* rayo de fondo, ~10 ciclos */
            }

            /* Sobre la esfera unitaria centrada en el origen, la normal ES q. */
            Vec3 q   = v3_madd(cam.origin, d, thit);
            Vec3 nrm = q;

            /* Maximizar el producto punto == minimizar la geodesica, sin acos.
             * rr[k] < 1 = bolita en vuelo (modo canon): no tiene celda. */
            float best1 = -2.0f, best2 = -2.0f;
            int   winner = 0;
            int   hay_ganador = 0;
            for (int k = 0; k < s->n; ++k) {
                if (rr[k] < 0.999f) continue;          /* no aterrizada, sin celda */
                float dot = nrm.x * rx[k] + nrm.y * ry[k] + nrm.z * rz[k];
                if      (dot > best1) { best2 = best1; best1 = dot; winner = k; hay_ganador = 1; }
                else if (dot > best2) { best2 = dot; }
            }
            if (!hay_ganador) {                         /* nadie aterrizo todavia */
                fb->px[j * w + i] = RENDER_BG;
                continue;
            }

            /* Lambert + piso ambiente, mismas constantes que los otros kernels. */
            float lambert = v3_dot(nrm, light);
            if (lambert < 0.0f) lambert = 0.0f;
            float k_shade = 0.28f + 0.72f * lambert;
            uint32_t base = rgb_mul(col[winner], k_shade);

            /* best1-best2 es chico en la frontera: smoothstep = antialiasing. */
            float edge = f_smoothstep(0.0f, edge_w, best1 - best2);
            fb->px[j * w + i] = rgb_lerp(RENDER_BG, base, edge);
        }
    }

    free(anim_col);
    free(rx); free(ry); free(rz); free(rr);
}

/* Sombreado de un impacto: lado de la esfera grande (kw) por relieve propio. */
static uint32_t shade_ball(Vec3 center, Vec3 nrm, Vec3 dir, Vec3 light, uint32_t color)
{
    float kw = v3_dot(center, light);         /* la normal de la esfera grande */
    if (kw < 0.0f) kw = 0.0f;
    kw = 0.28f + 0.72f * kw;                  /* piso ambiente + difusa */

    float nd = v3_dot(nrm, light);
    if (nd < 0.0f) nd = 0.0f;

    uint32_t px = rgb_mul(color, kw * (0.25f + 0.75f * nd));

    /* Vector medio de Blinn-Phong: L + V, con V = -dir. */
    Vec3  hv = v3_norm(v3_sub(light, dir));
    float nh = v3_dot(nrm, hv);
    if (nh > 0.0f) {
        float spec = powf(nh, BALL_SPEC_POW) * BALL_SPEC_K * kw;
        if (spec > 0.004f) px = rgb_lerp(px, 0xFFFFFFFFu, spec);
    }
    return px;
}

/* Kernel por defecto: las bolitas por raycasting, O(P*N) y sin z-buffer.
 * Test barato con O = (0,0,dist): perp^2 = |C-O|^2 - b^2, un sqrt por pixel. */
static void render_balls_raycast(Framebuffer *fb, const SeedSet *s,
                                 const Config *cfg, double t)
{
    const int w = fb->w, h = fb->h;

    float *rx = (float *)malloc((size_t)s->n * sizeof(float));
    float *ry = (float *)malloc((size_t)s->n * sizeof(float));
    float *rz = (float *)malloc((size_t)s->n * sizeof(float));
    float *rr = (float *)malloc((size_t)s->n * sizeof(float));
    if (rx == NULL || ry == NULL || rz == NULL || rr == NULL) {
        free(rx); free(ry); free(rz); free(rr);
        return;                                        /* sin memoria, no crash */
    }

    const float ang  = (float)(t * ((cfg != NULL) ? cfg->rot_speed : SS_DEF_ROT_SPEED));
    const float sy   = sinf(ang),  cyv = cosf(ang);
    const float tilt = 0.42f;                 /* mismo encuadre que los otros modos */
    const float stx  = sinf(tilt), ctx = cosf(tilt);
    rotate_seeds(s, sy, cyv, stx, ctx, rx, ry, rz, rr);

    uint32_t       *anim_col = frame_colors(s, cfg, t);
    const uint32_t *col      = (anim_col != NULL) ? anim_col : s->color;

    Camera      cam   = camera_make(cfg);
    const Vec3  light = v3_norm(v3(-0.4f, 0.6f, 0.7f));   /* la luz de siempre */
    const float dist  = cam.origin.z;                     /* O = (0, 0, dist)  */

    const float r  = ball_world_radius(s->n);
    const float r2 = r * r;

    /* |C-O|^2 no depende del rayo: se precalcula aca y rr[] pasa a guardarlo. */
    const float dist2 = dist * dist;
    #pragma omp parallel for schedule(static) if(s->n >= RENDER_PAR_MIN)
    for (int k = 0; k < s->n; ++k)
        rr[k] = rr[k] + dist2 - 2.0f * dist * rz[k];

    /* Tamano de un pixel en unidades de mundo, por unidad de profundidad. */
    const float pix_k = 2.0f * cam.tan_half_fov / (float)h;

    /* Cada 'j' escribe una fila propia y todo el cuerpo es local: sin carreras.
     * dynamic,1 y no static por el desbalance geometrico (filas fuera de la
     * silueta) y por la CPU hibrida P/E-cores: a 24 hilos y N=10000, static
     * da 363 ms y dynamic,1 285 ms (guided 296, dynamic,16 328). */
    #pragma omp parallel for schedule(dynamic, 1)
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            Vec3 d = camera_ray(&cam, i, j, w, h);

            /* Rechazo del fondo contra la esfera envolvente: ahorra las N. */
            float thit;
            if (!ray_sphere_hit(cam.origin, d, v3(0.0f, 0.0f, 0.0f), 1.0f + r, &thit)) {
                fb->px[j * w + i] = RENDER_BG;
                continue;
            }

            const float od = dist * d.z;      /* O.d, con O sobre el eje z */

            /* Las DOS mas cercanas: la segunda es lo que asoma en el borde
             * suave, y mezclar contra el fondo dibujaria una costura negra. */
            float t1 = INFINITY, p1 = 0.0f;  int w1 = -1;
            float t2 = INFINITY;             int w2 = -1;

            for (int k = 0; k < s->n; ++k) {
                float b = rx[k] * d.x + ry[k] * d.y + rz[k] * d.z - od;
                if (b <= 0.0f) continue;                  /* bolita detras del ojo */

                float perp2 = rr[k] - b * b;              /* rr[k] ya es |C-O|^2 */
                if (perp2 >= r2) continue;                /* el rayo pasa de largo */

                float tt = b - sqrtf(r2 - perp2);         /* cara de adelante */
                if (tt < t1) {
                    t2 = t1; w2 = w1;
                    t1 = tt; p1 = perp2; w1 = k;
                } else if (tt < t2) {
                    t2 = tt; w2 = k;
                }
            }

            if (w1 < 0) {                                  /* pego el envolvente, */
                fb->px[j * w + i] = RENDER_BG;             /* pero ninguna bolita */
                continue;
            }

            /* Normal verdadera: del centro de la bolita al punto de impacto. */
            Vec3 c1  = v3(rx[w1], ry[w1], rz[w1]);
            Vec3 q    = v3_madd(cam.origin, d, t1);
            Vec3 nrm  = v3_scale(v3_sub(q, c1), 1.0f / r);
            uint32_t px = shade_ball(c1, nrm, d, light, col[w1]);

            /* Cobertura: que fraccion del pixel cae dentro de la bolita. */
            float cov = (r - sqrtf(p1)) / (pix_k * t1) + 0.5f;
            if (cov < 1.0f) {
                uint32_t detras = RENDER_BG;
                if (w2 >= 0) {
                    Vec3 c2 = v3(rx[w2], ry[w2], rz[w2]);
                    Vec3 q2 = v3_madd(cam.origin, d, t2);
                    Vec3 n2 = v3_scale(v3_sub(q2, c2), 1.0f / r);
                    detras = shade_ball(c2, n2, d, light, col[w2]);
                }
                px = rgb_lerp(detras, px, (cov < 0.0f) ? 0.0f : cov);
            }

            fb->px[j * w + i] = px;
        }
    }

    free(anim_col);
    free(rx); free(ry); free(rz); free(rr);
}

/* Despacho entre los tres kernels; la firma es el contrato con main.c. */
void render_frame(Framebuffer *fb, const SeedSet *s, const Config *cfg, double t)
{
    if (fb == NULL || fb->px == NULL || s == NULL) return;

    if (cfg != NULL && cfg->voronoi)     render_raycast(fb, s, cfg, t);
    else if (cfg != NULL && cfg->raster) render_points(fb, s, cfg, t);
    else                                 render_balls_raycast(fb, s, cfg, t);
}
