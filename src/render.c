/* ===========================================================================
 *  render.c - Framebuffer, camara y el raycasting con Voronoi esferico.
 *
 *  Esta version reemplaza el dibujo de puntos por el kernel real del
 *  proyecto: por cada pixel se lanza un rayo, se intersecta contra la esfera
 *  unitaria y se busca a que semilla le toca esa celda del Voronoi esferico.
 *  Es O(P*N) -- P pixeles de la silueta por N semillas -- y es exactamente el
 *  cuello de botella que se va a paralelizar despues.
 *
 *  Deliberadamente NO lleva ningun '#pragma omp', NO usa acos() en el bucle
 *  caliente y NO usa rejilla espacial: este es el baseline secuencial
 *  honesto contra el que se mide el speedup (docs/PLAN-03-DIEGUITO.md).
 *
 *  El render de puntos original (discos con z-buffer) se conserva como
 *  render_points(): sigue siendo el plan B si --voronoi 0 o si algun equipo
 *  necesita un fallback mas barato.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#include "render.h"

#include <math.h>
#include <stdlib.h>

#include "color.h"

/* Fondo casi negro con un toque frio: hace resaltar los colores vivos de las
 * semillas sin ser un negro plano que se ve barato. */
#define RENDER_BG  0xFF0A0A12u

/* Ancho de la banda de antialiasing entre celdas de Voronoi, en unidades de
 * producto punto. Chico a proposito: sobre la esfera unitaria best1-best2 se
 * mueve poco incluso lejos del borde, asi que una banda angosta ya alcanza
 * para suavizar sin comerse celdas enteras cuando N es grande. */
#define EDGE_W  0.02f

/* ==========================================================================
 *  Framebuffer
 * ========================================================================== */
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

/* ==========================================================================
 *  Camara
 * ========================================================================== */
Camera camera_make(const Config *cfg)
{
    Camera cam;

    /* Campo de vision vertical fijo de 50 grados: encuadre comodo, sin la
     * distorsion de gran angular. La distancia al origen se DERIVA de el para
     * que la esfera unitaria ocupe la fraccion pedida de la altura. */
    const float fov_v      = 50.0f * (float)SS_PI / 180.0f;
    cam.tan_half_fov       = tanf(fov_v * 0.5f);

    float frac = (cfg != NULL) ? (float)cfg->sphere_frac : (float)SS_DEF_FILL;
    if (frac <= 0.0f) frac = (float)SS_DEF_FILL;

    /* Un punto en el borde superior de la esfera (offset vertical 1 respecto
     * al centro) debe proyectarse a 'frac' de la media altura. En proyeccion
     * perspectiva eso es (1/dist)/tan_half_fov = frac  ->  dist = ... */
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
    /* Centro del pixel a coordenadas normalizadas [-1, 1], con Y hacia arriba. */
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

/* ==========================================================================
 *  render_points - el dibujo de discos con z-buffer (baseline de Nico).
 *  Se conserva completo para --voronoi 0: mas barato, sirve de plan B si el
 *  raycasting se cae de FPS en alguna maquina.
 * ========================================================================== */
static void draw_disc(Framebuffer *fb, float *zbuf,
                      int cx, int cy, int radius, float depth, uint32_t color)
{
    const int w = fb->w, h = fb->h;

    int x0 = cx - radius, x1 = cx + radius;
    int y0 = cy - radius, y1 = cy + radius;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= w) x1 = w - 1;
    if (y1 >= h) y1 = h - 1;

    const int r2 = radius * radius;
    for (int y = y0; y <= y1; ++y) {
        int dy = y - cy;
        for (int x = x0; x <= x1; ++x) {
            int dx = x - cx;
            if (dx * dx + dy * dy > r2) continue;      /* fuera del disco */

            int k = y * w + x;
            if (depth >= zbuf[k]) continue;            /* algo mas cerca ya esta */
            zbuf[k]   = depth;
            fb->px[k] = color;
        }
    }
}

static void render_points(Framebuffer *fb, const SeedSet *s, const Config *cfg, double t)
{
    const int w = fb->w, h = fb->h;
    fb_clear(fb, RENDER_BG);

    /* z-buffer del frame: profundidad por pixel, +inf = vacio. Se pide y se
     * libera aqui mismo para no dejar estado global ni fugas entre frames. */
    float *zbuf = (float *)malloc((size_t)w * (size_t)h * sizeof(float));
    if (zbuf == NULL) return;                 /* sin z-buffer no dibujamos, no crash */
    const size_t npx = (size_t)w * (size_t)h;
    for (size_t i = 0; i < npx; ++i) zbuf[i] = INFINITY;

    Camera cam = camera_make(cfg);
    const float aspect = (float)w / (float)h;

    /* Giro del frame: rotacion alrededor de Y por el tiempo, mas una
     * inclinacion fija en X para que la esfera no se vea plana de frente. */
    const float ang  = (float)(t * ((cfg != NULL) ? cfg->rot_speed : SS_DEF_ROT_SPEED));
    const float sy   = sinf(ang),  cyv = cosf(ang);
    const float tilt = 0.42f;                 /* ~24 grados de encuadre, fijo */
    const float stx  = sinf(tilt), ctx = cosf(tilt);

    /* Luz difusa fija en el espacio del mundo: da volumen a la esfera y hace
     * que el lado opuesto se vea mas oscuro. */
    const Vec3 light = v3_norm(v3(-0.4f, 0.6f, 0.7f));

    /* Radio del disco en pixeles: parte del radio proyectado de la esfera y se
     * reparte entre las N semillas, con topes para que ni desaparezca ni se
     * empaste. Con N grande los discos se achican y aparece el grano fino. */
    const float sphere_px = (cfg != NULL ? (float)cfg->sphere_frac : (float)SS_DEF_FILL)
                            * (float)h * 0.5f;
    int nseeds = s->n;
    if (nseeds < 1) nseeds = 1;
    int radius = (int)(sphere_px / sqrtf((float)nseeds) + 0.5f);
    if (radius < 1)  radius = 1;
    if (radius > 14) radius = 14;

    for (int idx = 0; idx < s->n; ++idx) {
        /* Posicion rotada de la semilla (sigue sobre la esfera unitaria). */
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
        int   sx = (int)((ndc_x * 0.5f + 0.5f) * (float)w + 0.5f);
        int   sy_px = (int)((1.0f - (ndc_y * 0.5f + 0.5f)) * (float)h + 0.5f);

        /* Sombreado difuso: la normal en la esfera unitaria ES la posicion. */
        float diff = v3_dot(p, light);
        if (diff < 0.0f) diff = 0.0f;
        float k = 0.28f + 0.72f * diff;               /* piso ambiente + difusa */

        draw_disc(fb, zbuf, sx, sy_px, radius, cz, rgb_mul(s->color[idx], k));
    }

    free(zbuf);
}

/* ==========================================================================
 *  rotate_seeds - rota las N semillas UNA vez por frame a un buffer aparte.
 *
 *  Con N << W*H (el caso normal, N por defecto es 128) sale mucho mas barato
 *  rotar las N semillas una sola vez que rotar el rayo de cada uno de los
 *  W*H pixeles (docs/01-FUNDAMENTO-MATEMATICO.md, seccion 3.1).
 *
 *  Importante: este buffer es SOLO para el render de este frame. No toca
 *  s->ax/ay/az, que son el estado persistente de la fisica (physics.c) --
 *  pisarlos aca romperia la integracion de Verlet entre frames.
 * ========================================================================== */
static void rotate_seeds(const SeedSet *s, float sy, float cyv, float stx, float ctx,
                         float *rx, float *ry, float *rz)
{
    for (int i = 0; i < s->n; ++i) {
        Vec3 p = seed_pos(s, i);
        p = v3_rot_y(p, sy, cyv);
        p = v3_rot_x(p, stx, ctx);
        rx[i] = p.x;
        ry[i] = p.y;
        rz[i] = p.z;
    }
}

/* ==========================================================================
 *  render_raycast - el kernel dominante: raycasting por pixel con Voronoi
 *  esferico. Ver docs/01-FUNDAMENTO-MATEMATICO.md seccion 4.
 * ========================================================================== */
static void render_raycast(Framebuffer *fb, const SeedSet *s, const Config *cfg, double t)
{
    const int w = fb->w, h = fb->h;

    /* Buffer de semillas rotadas para este frame. Un malloc por frame, igual
     * que el z-buffer de render_points: es codigo secuencial deliberadamente
     * simple, la version paralela decide si vale la pena reciclarlo. */
    float *rx = (float *)malloc((size_t)s->n * sizeof(float));
    float *ry = (float *)malloc((size_t)s->n * sizeof(float));
    float *rz = (float *)malloc((size_t)s->n * sizeof(float));
    if (rx == NULL || ry == NULL || rz == NULL) {
        free(rx); free(ry); free(rz);
        return;                                        /* sin memoria, no crash */
    }

    const float ang  = (float)(t * ((cfg != NULL) ? cfg->rot_speed : SS_DEF_ROT_SPEED));
    const float sy   = sinf(ang),  cyv = cosf(ang);
    const float tilt = 0.42f;                 /* mismo encuadre que render_points */
    const float stx  = sinf(tilt), ctx = cosf(tilt);
    rotate_seeds(s, sy, cyv, stx, ctx, rx, ry, rz);

    Camera cam = camera_make(cfg);
    const Vec3 center = v3(0.0f, 0.0f, 0.0f);
    const float radius = 1.0f;                          /* esfera unitaria */

    /* Misma luz fija en el mundo que usaba render_points, para que el look
     * no cambie al alternar --voronoi. */
    const Vec3 light = v3_norm(v3(-0.4f, 0.6f, 0.7f));

    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            Vec3 d = camera_ray(&cam, i, j, w, h);

            float thit;
            if (!ray_sphere_hit(cam.origin, d, center, radius, &thit)) {
                fb->px[j * w + i] = RENDER_BG;
                continue;                               /* rayo de fondo, ~10 ciclos */
            }

            /* Punto de impacto y su normal. Sobre la esfera unitaria
             * centrada en el origen, la normal ES el punto -- no hace falta
             * normalizar de nuevo. */
            Vec3 q   = v3_madd(cam.origin, d, thit);
            Vec3 nrm = q;

            /* --- Voronoi esferico: el bucle interno, O(N) ------------------
             * Se maximiza el producto punto en vez de minimizar la distancia
             * geodesica (que pediria acos): son equivalentes porque acos es
             * monotona decreciente, y evitamos un acos por semilla por pixel. */
            float best1 = -2.0f, best2 = -2.0f;
            int   winner = 0;
            for (int k = 0; k < s->n; ++k) {
                float dot = nrm.x * rx[k] + nrm.y * ry[k] + nrm.z * rz[k];
                if      (dot > best1) { best2 = best1; best1 = dot; winner = k; }
                else if (dot > best2) { best2 = dot; }
            }

            /* Sombreado: Lambert difuso + piso ambiente, mismas constantes
             * que render_points para que el brillo no cambie entre modos. */
            float lambert = v3_dot(nrm, light);
            if (lambert < 0.0f) lambert = 0.0f;
            float k_shade = 0.28f + 0.72f * lambert;
            uint32_t base = rgb_mul(s->color[winner], k_shade);

            /* Borde de celda: best1-best2 es chico exactamente en la
             * frontera entre dos celdas, asi que un smoothstep sobre esa
             * diferencia da antialiasing analitico sin detectar aristas. */
            float edge = f_smoothstep(0.0f, EDGE_W, best1 - best2);
            fb->px[j * w + i] = rgb_lerp(RENDER_BG, base, edge);
        }
    }

    free(rx); free(ry); free(rz);
}

/* ==========================================================================
 *  render_frame - despacha entre Voronoi (por defecto) y puntos (--voronoi 0).
 *  La firma es el contrato con main.c y no cambia (ver render.h).
 * ========================================================================== */
void render_frame(Framebuffer *fb, const SeedSet *s, const Config *cfg, double t)
{
    if (fb == NULL || fb->px == NULL || s == NULL) return;

    if (cfg != NULL && cfg->voronoi) render_raycast(fb, s, cfg, t);
    else                             render_points(fb, s, cfg, t);
}
