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

/* Ancho de la banda de antialiasing entre celdas de Voronoi.
 *
 * NO puede ser una constante fija: la medida que se compara es best1-best2,
 * una diferencia de productos punto, y esa diferencia ENCOGE con N. Cada
 * celda cubre 4*pi/N de area, o sea un radio angular de ~2/sqrt(N) rad, y en
 * el centro de la celda la diferencia vale como mucho 1-cos(2*theta) ~ 8/N.
 * Con un ancho fijo de 0.02 y N=3000 (donde 8/N = 0.0027) TODO el pixel cae
 * dentro de la banda de borde y la esfera entera se funde con el fondo: se ve
 * negra. Por eso la banda se deriva de N y se queda en una fraccion chica de
 * esa diferencia maxima, que es lo que la vuelve una linea fina a cualquier N.
 *
 * EDGE_FRAC es esa fraccion (15% del salto maximo). El clamp superior evita
 * que con N muy chico (1..4 celdas gigantes) la banda se coma media esfera. */
#define EDGE_FRAC  0.15f
#define EDGE_MAX   0.05f

static inline float edge_width_for(int n)
{
    if (n < 1) n = 1;
    float w = EDGE_FRAC * 8.0f / (float)n;
    return (w > EDGE_MAX) ? EDGE_MAX : w;
}

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
 *  render_points - cada semilla dibujada como una ESFERITA con z-buffer.
 *
 *  Antes cada semilla era un disco de color PLANO. Por eso el resultado se
 *  veia como confeti y no como la figura de referencia: una bolita real tiene
 *  relieve propio (su lado iluminado y su lado oscuro) y un brillo especular.
 *  Aca el disco se sombrea POR PIXEL reconstruyendo la normal de una esfera:
 *  dentro del disco, con (u,v) las coordenadas locales normalizadas a [-1,1],
 *
 *      nz = sqrt(1 - u^2 - v^2)      ->  normal = (u, v, nz)
 *
 *  que es la misma identidad de la esfera unitaria que usa el raycasting, solo
 *  que en el espacio local del disco. Cuesta un sqrt por pixel de bolita, y a
 *  cambio la figura pasa de puntos planos a esferas.
 *
 *  El radio y el centro son FLOTANTES a proposito. Redondearlos a pixeles
 *  enteros mete un salto de tamano discreto justo donde el redondeo cambia de
 *  valor, y como el factor de perspectiva depende de la profundidad, ese salto
 *  cae sobre un plano de profundidad constante -- o sea, sobre un CIRCULO
 *  dentro de la silueta. Con N grande (radio ~1 px) el salto es de 1 a 2 px,
 *  cuatro veces el area, y se ve como una segunda esfera dibujada por dentro.
 *  Con el borde por cobertura el tamano varia de forma continua y no hay
 *  ningun umbral donde saltar.
 * ========================================================================== */

/* Luz en el espacio del DISCO: x a la derecha, y hacia abajo, z hacia el ojo.
 * Arriba-izquierda y al frente, que es de donde viene la luz en la referencia. */
#define BALL_LX  (-0.45f)
#define BALL_LY  (-0.55f)
#define BALL_LZ   (0.70f)
#define BALL_SPEC_POW  28.0f   /* dureza del brillo: alto = punto chico y duro */
#define BALL_SPEC_K    0.55f   /* cuanto blanco mete el brillo, 0..1          */

/* Fraccion del radio "que se tocan" (1.9/sqrt(N)) que se usa de verdad.
 * Ver el calculo en render_points. */
#define BALL_FILL      1.60f

/* ---------------------------------------------------------------------------
 *  Radio de la bolita en unidades de MUNDO (la esfera grande tiene radio 1).
 *
 *  Es el mismo calculo que hacia render_points en pixeles, movido aca para que
 *  el rasterizado y el raycasting usen EXACTAMENTE el mismo tamano: si cada uno
 *  tuviera su formula, cambiar --voronoi cambiaria el tamano de las bolitas y
 *  las dos rutas dejarian de ser comparables al medir.
 *
 *  En un empaque hexagonal sobre la esfera cada semilla ocupa 4*pi/N de area,
 *  de donde la distancia al vecino mas cercano es ~3.81/sqrt(N) radianes y el
 *  radio para que se TOQUEN es la mitad. BALL_FILL se queda un poco abajo de
 *  ese 1.9 teorico porque en el borde de la silueta el escorzo las junta.
 * ------------------------------------------------------------------------- */
static float ball_world_radius(int n)
{
    if (n < 1) n = 1;
    float r = BALL_FILL / sqrtf((float)n);
    return (r > 0.5f) ? 0.5f : r;      /* con N chico, que no se coma la esfera */
}

/* ==========================================================================
 *  frame_colors - el color de las N semillas EN ESTE INSTANTE.
 *
 *  La deriva de color NO se guarda en s->color[]: se recalcula cada frame a
 *  partir de (indice, seed, t) con color_for_seed_at(). Guardarla seria mutar
 *  el SeedSet desde el render -- que recibe 's' como const y no le pertenece --
 *  y ademas volveria el color un estado ACUMULADO: dos hilos escribiendo el
 *  mismo arreglo, y el color dependiendo de cuantos frames se hayan dibujado
 *  en vez de solo del instante t. Recalcular lo mantiene puro: el frame del
 *  segundo 12.5 sale identico se haya llegado a el en 30 o en 400 frames, y
 *  la version paralela puede reproducirlo bit a bit.
 *
 *  El costo es O(N) por frame contra el O(P*N) del kernel de Voronoi, o sea
 *  perdido en el ruido; y es un bucle sin dependencias, asi que cuando se
 *  paralelice acepta un 'parallel for' directo.
 *
 *  Devuelve NULL cuando no hay nada que animar (--color-speed 0) o si falla el
 *  malloc; en ambos casos el llamador cae de vuelta en s->color[], que es
 *  exactamente el mismo color con t = 0.
 * ========================================================================== */
static uint32_t *frame_colors(const SeedSet *s, const Config *cfg, double t)
{
    if (cfg == NULL || cfg->color_speed == 0.0 || s->n < 1) return NULL;

    uint32_t *c = (uint32_t *)malloc((size_t)s->n * sizeof(uint32_t));
    if (c == NULL) return NULL;

    ColorAnim anim = { (float)cfg->color_speed, (float)cfg->color_spread };
    for (int i = 0; i < s->n; ++i)
        c[i] = color_for_seed_at((uint32_t)i, cfg->seed, &anim, t);

    return c;
}

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

    /* Vector medio de Blinn-Phong: L + V con V = (0,0,1), normalizado. Es
     * constante para todas las bolitas, asi que se calcula una vez aca. */
    const float hx = BALL_LX, hy = BALL_LY, hz = BALL_LZ + 1.0f;
    const float hinv = 1.0f / sqrtf(hx * hx + hy * hy + hz * hz);
    const float Hx = hx * hinv, Hy = hy * hinv, Hz = hz * hinv;

    for (int y = y0; y <= y1; ++y) {
        float dy = (float)y - cyf;
        for (int x = x0; x <= x1; ++x) {
            float dx = (float)x - cxf;

            /* Dos descartes baratos ANTES del sqrt: las esquinas de la caja
             * (comparando distancias al cuadrado) y los pixeles donde ni el
             * polo de la bolita alcanza a estar delante de lo ya dibujado. */
            float d2 = dx * dx + dy * dy;
            if (d2 >= rlim2) continue;

            int k = y * w + x;
            if (z_min >= zbuf[k]) continue;

            /* Cobertura: fraccion del pixel que cae dentro de la bolita. Es una
             * rampa lineal de un pixel de ancho centrada en el borde, no un
             * corte binario. Esto es lo que hace que el radio pueda ser
             * fraccionario: rf = 1.4 y rf = 1.6 se ven distintos de a poco, y
             * no hay ningun umbral donde el tamano salte de golpe. */
            float d = sqrtf(d2);
            float cov = rf - d + 0.5f;
            if (cov > 1.0f) cov = 1.0f;

            /* Normal de la esferita. Fuera del disco geometrico (la banda de
             * cobertura) se toma el borde, nz = 0, en vez de una raiz negativa. */
            float un = dx * inv_rf, vn = dy * inv_rf;
            float rr = un * un + vn * vn;
            float nz = (rr < 1.0f) ? sqrtf(1.0f - rr) : 0.0f;

            /* Profundidad propia: el polo de la bolita esta r_world mas cerca
             * del ojo que su centro, asi dos bolitas que se solapan se recortan
             * bien en vez de taparse enteras. */
            float z = depth - nz * r_world;

            if (z >= zbuf[k]) continue;                /* algo mas cerca ya esta */

            /* Difusa local (el relieve de la bolita) modulada por k_world (de
             * que lado de la esfera GRANDE esta), mas el brillo especular. */
            float nd = un * BALL_LX + vn * BALL_LY + nz * BALL_LZ;
            if (nd < 0.0f) nd = 0.0f;
            float shade = k_world * (0.25f + 0.75f * nd);

            float nh = un * Hx + vn * Hy + nz * Hz;
            float spec = 0.0f;
            if (nh > 0.0f) spec = powf(nh, BALL_SPEC_POW) * BALL_SPEC_K * k_world;

            uint32_t px = rgb_mul(color, shade);
            if (spec > 0.004f) px = rgb_lerp(px, 0xFFFFFFFFu, spec);

            /* Con cobertura parcial se mezcla con lo que ya habia detras. El
             * z-buffer solo se escribe si la bolita cubre mas de medio pixel:
             * si apenas lo roza, no debe tapar a lo que venga despues. */
            if (cov < 1.0f)  px = rgb_lerp(fb->px[k], px, cov);
            if (cov >= 0.5f) zbuf[k] = z;

            fb->px[k] = px;
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

    /* Colores de ESTE instante. Si la deriva esta apagada (o falla el malloc)
     * se usan los fijos del SeedSet. */
    uint32_t       *anim_col = frame_colors(s, cfg, t);
    const uint32_t *col      = (anim_col != NULL) ? anim_col : s->color;

    Camera cam = camera_make(cfg);
    const float aspect = (float)w / (float)h;
    const float cam_dist = v3_len(cam.origin);   /* la camara mira al origen */

    /* Giro del frame: rotacion alrededor de Y por el tiempo, mas una
     * inclinacion fija en X para que la esfera no se vea plana de frente. */
    const float ang  = (float)(t * ((cfg != NULL) ? cfg->rot_speed : SS_DEF_ROT_SPEED));
    const float sy   = sinf(ang),  cyv = cosf(ang);
    const float tilt = 0.42f;                 /* ~24 grados de encuadre, fijo */
    const float stx  = sinf(tilt), ctx = cosf(tilt);

    /* Luz difusa fija en el espacio del mundo: da volumen a la esfera y hace
     * que el lado opuesto se vea mas oscuro. */
    const Vec3 light = v3_norm(v3(-0.4f, 0.6f, 0.7f));

    /* Radio de la bolita en pixeles. NO es un numero a ojo: en un empaque
     * hexagonal sobre la esfera cada semilla ocupa 4*pi/N de area, y de ahi la
     * distancia al vecino mas cercano sale d = sqrt(8*pi/(sqrt(3)*N)) ~
     * 3.81/sqrt(N) radianes. Para que las bolitas se TOQUEN el radio tiene que
     * ser d/2 ~ 1.9/sqrt(N). Con el 1.0/sqrt(N) que habia antes las bolitas
     * salian a la mitad de tamano y por eso la esfera se veia como puntos
     * sueltos en vez del empaque compacto de la referencia.
     *
     * BALL_FILL se queda un poco abajo del 1.9 teorico: al llegar al borde de
     * la silueta el escorzo junta las bolitas, y con el valor exacto el borde
     * se empasta. */
    const float sphere_px = (cfg != NULL ? (float)cfg->sphere_frac : (float)SS_DEF_FILL)
                            * (float)h * 0.5f;
    float radius = ball_world_radius(s->n) * sphere_px;
    if (radius < 0.5f) radius = 0.5f;      /* mas chico que esto ya no se ve */

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
        float sx    = (ndc_x * 0.5f + 0.5f) * (float)w;
        float sy_px = (1.0f - (ndc_y * 0.5f + 0.5f)) * (float)h;

        /* Sombreado difuso: la normal en la esfera unitaria ES la posicion. */
        float diff = v3_dot(p, light);
        if (diff < 0.0f) diff = 0.0f;
        float k = 0.28f + 0.72f * diff;               /* piso ambiente + difusa */

        /* Radio con perspectiva: las bolitas del frente se ven mas grandes que
         * las del fondo. Sin esto todas salen del mismo tamano y la esfera se
         * aplana. cam_dist/cz es el factor de escala de la proyeccion. Queda en
         * float: redondearlo a pixeles enteros es lo que dibujaba el anillo. */
        float rf = radius * (cam_dist / cz);

        /* Radio de la bolita en unidades de MUNDO, para su z-buffer propio:
         * sphere_px pixeles equivalen a 1 radio de la esfera grande. */
        float r_world = radius / sphere_px;

        draw_ball(fb, zbuf, sx, sy_px, rf, cz, r_world, col[idx], k);
    }

    free(anim_col);
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

    /* Colores de ESTE instante, una vez por frame y NO por pixel: el bucle
     * caliente solo indexa col[winner], igual que antes indexaba s->color. */
    uint32_t       *anim_col = frame_colors(s, cfg, t);
    const uint32_t *col      = (anim_col != NULL) ? anim_col : s->color;

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

    /* Ancho del borde de celda para ESTE N (ver EDGE_FRAC arriba). Se calcula
     * una vez por frame, no por pixel. */
    const float edge_w = edge_width_for(s->n);

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
            uint32_t base = rgb_mul(col[winner], k_shade);

            /* Borde de celda: best1-best2 es chico exactamente en la
             * frontera entre dos celdas, asi que un smoothstep sobre esa
             * diferencia da antialiasing analitico sin detectar aristas. */
            float edge = f_smoothstep(0.0f, edge_w, best1 - best2);
            fb->px[j * w + i] = rgb_lerp(RENDER_BG, base, edge);
        }
    }

    free(anim_col);
    free(rx); free(ry); free(rz);
}

/* ==========================================================================
 *  render_balls_raycast - las MISMAS bolitas, pero resueltas por pixel.
 *
 *  Por que existe, si render_points ya las dibujaba: porque el rasterizado
 *  tiene un costo casi CONSTANTE en N y por lo tanto no sirve para lo que este
 *  proyecto mide. El radio va como 1/sqrt(N), o sea el area de cada bolita va
 *  como 1/N, y hay N bolitas: el area total pintada NO depende de N. Medido a
 *  1280x720, multiplicar N por 1562 (de 128 a 200000) solo multiplica el costo
 *  por 4.8, y ese poco que crece es el bucle O(N) de proyectar, no los pixeles.
 *  Con ese kernel, N no es una perilla de carga.
 *
 *  Ademas el rasterizado NO se puede paralelizar por semillas tal cual: dos
 *  bolitas que se solapan hacen read-modify-write del mismo z-buffer, que es
 *  una carrera de datos.
 *
 *  Invertir el bucle arregla las dos cosas de un golpe: pasa a ser O(P*N) --
 *  el mismo modelo de costo que el Voronoi -- y cada pixel es independiente,
 *  asi que el 'parallel for' entra sin carreras, sin atomicos y sin z-buffer.
 *  La imagen ademas MEJORA: la oclusion entre bolitas pasa a ser exacta por
 *  pixel en vez de aproximada con profundidad por pixel.
 *
 *  ---- el test rayo-esfera, barato ----------------------------------------
 *  Un test generico cuesta ~16 flops por semilla. Aca sale en la mitad usando
 *  dos cosas que este problema regala: los centros estan sobre la esfera
 *  UNITARIA (|C| = 1) y la camara esta en el eje z, O = (0, 0, dist).
 *
 *      |C - O|^2 = 1 - 2*dist*C.z + dist^2      <- un solo madd desde C.z
 *      b         = (C - O).d = C.d - dist*d.z   <- el producto punto de siempre
 *      perp^2    = |C - O|^2 - b^2              <- distancia rayo-centro
 *
 *  y pega si perp^2 < r^2. O sea: el mismo producto punto C.d que ya hace el
 *  Voronoi, mas tres operaciones. El sqrt aparece UNA vez por pixel (para la
 *  bolita ganadora), no una vez por semilla.
 * ========================================================================== */

/* ---------------------------------------------------------------------------
 *  Sombreado de un impacto. Son DOS niveles multiplicados, igual que en el
 *  rasterizado, y el de afuera no es decorativo:
 *
 *    k_world  de que lado de la esfera GRANDE esta la semilla. Funciona como
 *             una oclusion ambiental barata -- una bolita del lado oscuro esta
 *             rodeada de vecinas que le tapan la luz -- y es lo unico que le da
 *             VOLUMEN al conjunto. Sin este factor cada bolita se ilumina solo
 *             por su propia normal, todas quedan igual de brillantes y la
 *             esfera se aplana: se ve como un mosaico de bolitas y no como un
 *             objeto redondo.
 *    relieve   la normal VERDADERA de la bolita en el punto de impacto. Aca
 *             esta la mejora sobre el rasterizado, que la reconstruia en el
 *             espacio del disco: esta es exacta y con perspectiva correcta.
 *
 *  Las constantes son las mismas que usaba render_points a proposito, para que
 *  cambiar de kernel no cambie el look.
 * ------------------------------------------------------------------------- */
static uint32_t shade_ball(Vec3 center, Vec3 nrm, Vec3 dir, Vec3 light, uint32_t color)
{
    float kw = v3_dot(center, light);         /* la normal de la esfera grande */
    if (kw < 0.0f) kw = 0.0f;
    kw = 0.28f + 0.72f * kw;                  /* piso ambiente + difusa */

    float nd = v3_dot(nrm, light);
    if (nd < 0.0f) nd = 0.0f;

    uint32_t px = rgb_mul(color, kw * (0.25f + 0.75f * nd));

    /* Vector medio de Blinn-Phong: L + V, con V = -dir (del punto hacia el ojo). */
    Vec3  hv = v3_norm(v3_sub(light, dir));
    float nh = v3_dot(nrm, hv);
    if (nh > 0.0f) {
        float spec = powf(nh, BALL_SPEC_POW) * BALL_SPEC_K * kw;
        if (spec > 0.004f) px = rgb_lerp(px, 0xFFFFFFFFu, spec);
    }
    return px;
}

static void render_balls_raycast(Framebuffer *fb, const SeedSet *s,
                                 const Config *cfg, double t)
{
    const int w = fb->w, h = fb->h;

    float *rx = (float *)malloc((size_t)s->n * sizeof(float));
    float *ry = (float *)malloc((size_t)s->n * sizeof(float));
    float *rz = (float *)malloc((size_t)s->n * sizeof(float));
    if (rx == NULL || ry == NULL || rz == NULL) {
        free(rx); free(ry); free(rz);
        return;                                        /* sin memoria, no crash */
    }

    const float ang  = (float)(t * ((cfg != NULL) ? cfg->rot_speed : SS_DEF_ROT_SPEED));
    const float sy   = sinf(ang),  cyv = cosf(ang);
    const float tilt = 0.42f;                 /* mismo encuadre que los otros modos */
    const float stx  = sinf(tilt), ctx = cosf(tilt);
    rotate_seeds(s, sy, cyv, stx, ctx, rx, ry, rz);

    uint32_t       *anim_col = frame_colors(s, cfg, t);
    const uint32_t *col      = (anim_col != NULL) ? anim_col : s->color;

    Camera      cam   = camera_make(cfg);
    const Vec3  light = v3_norm(v3(-0.4f, 0.6f, 0.7f));   /* la luz de siempre */
    const float dist  = cam.origin.z;                     /* O = (0, 0, dist)  */
    const float oc_k  = 1.0f + dist * dist;               /* |C-O|^2 = oc_k - 2*dist*C.z */

    const float r  = ball_world_radius(s->n);
    const float r2 = r * r;

    /* Tamano de un pixel en unidades de mundo, por unidad de profundidad. Es
     * lo que convierte la distancia rayo-centro en COBERTURA, que es lo que da
     * el borde suave: la misma rampa de un pixel del rasterizado, pero medida
     * en el mundo en vez de en la pantalla. */
    const float pix_k = 2.0f * cam.tan_half_fov / (float)h;

    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            Vec3 d = camera_ray(&cam, i, j, w, h);

            /* Rechazo del fondo: si el rayo ni siquiera roza la esfera que
             * ENVUELVE a las bolitas (radio 1 + r), no hay nada que probar y
             * nos ahorramos las N iteraciones. Es el mismo truco con el que el
             * Voronoi descarta el fondo en ~10 ciclos. */
            float thit;
            if (!ray_sphere_hit(cam.origin, d, v3(0.0f, 0.0f, 0.0f), 1.0f + r, &thit)) {
                fb->px[j * w + i] = RENDER_BG;
                continue;
            }

            const float od = dist * d.z;      /* O.d, con O sobre el eje z */

            /* Las DOS bolitas mas cercanas al ojo sobre este rayo. La segunda
             * solo se usa para el borde suave: en el pixel donde la de adelante
             * cubre medio pixel, lo que asoma detras es su vecina, no el fondo,
             * y mezclar contra el fondo dibujaria una costura negra en cada
             * contacto. */
            float t1 = INFINITY, p1 = 0.0f;  int w1 = -1;
            float t2 = INFINITY;             int w2 = -1;

            for (int k = 0; k < s->n; ++k) {
                float b = rx[k] * d.x + ry[k] * d.y + rz[k] * d.z - od;
                if (b <= 0.0f) continue;                  /* bolita detras del ojo */

                float perp2 = (oc_k - 2.0f * dist * rz[k]) - b * b;
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
    free(rx); free(ry); free(rz);
}

/* ==========================================================================
 *  render_frame - despacha entre los tres kernels. La firma es el contrato
 *  con main.c y no cambia (ver render.h).
 *
 *      --voronoi 1   celdas de Voronoi por raycasting      O(P*N)
 *      (por defecto) bolitas por raycasting                O(P*N)
 *      --raster 1    bolitas rasterizadas (plan B barato)  ~O(1) en N
 * ========================================================================== */
void render_frame(Framebuffer *fb, const SeedSet *s, const Config *cfg, double t)
{
    if (fb == NULL || fb->px == NULL || s == NULL) return;

    if (cfg != NULL && cfg->voronoi)     render_raycast(fb, s, cfg, t);
    else if (cfg != NULL && cfg->raster) render_points(fb, s, cfg, t);
    else                                 render_balls_raycast(fb, s, cfg, t);
}
