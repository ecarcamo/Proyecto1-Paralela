/* ===========================================================================
 *  render.c - Framebuffer, camara y el dibujo de puntos (baseline, sin Voronoi).
 *
 *  Este cuerpo existe para que la esfera se PUEDA VER hoy: proyecta cada
 *  semilla a pantalla y pinta un disco sombreado con z-buffer. Es O(N), rapido,
 *  y suficiente para comprobar que la geometria y el giro estan bien.
 *
 *  Deliberadamente NO lleva ningun '#pragma omp': estamos construyendo el
 *  baseline secuencial honesto contra el que se va a medir el speedup. La
 *  paralelizacion entra despues, sobre el raycasting con Voronoi de Dieguito.
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
 *  Dibujo de un disco sombreado con z-buffer.
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

/* ==========================================================================
 *  render_frame - LA FIRMA ES UN CONTRATO (ver render.h). Dieguito reemplaza
 *  SOLO este cuerpo por el raycasting con Voronoi.
 * ========================================================================== */
void render_frame(Framebuffer *fb, const SeedSet *s, const Config *cfg, double t)
{
    if (fb == NULL || fb->px == NULL || s == NULL) return;

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
