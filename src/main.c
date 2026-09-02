/* ===========================================================================
 *  main.c - Bucle principal: SDL, textura y presentacion. La primera imagen.
 *
 *  A partir de este archivo el proyecto deja de ser abstracto: se abre una
 *  ventana y la esfera de Fibonacci gira con su contador de FPS. El pipeline
 *  es deliberadamente simple:
 *
 *      parse args -> validar -> alloc SeedSet -> sphere_fill_fibonacci()
 *      SDL_Init -> ventana -> renderer -> textura ARGB8888 STREAMING
 *      bucle:  eventos -> render_frame -> overlay_stats -> subir textura
 *              -> present -> medir dt -> media movil de FPS
 *      liberar TODO (incluso en los caminos de error) -> SDL_Quit
 *
 *  La rubrica califica el "manejo adecuado de inicializacion y destruccion de
 *  objetos y memoria". Por eso hay un solo 'goto cleanup': libera todo en el
 *  orden inverso al de creacion, y como cada recurso arranca en NULL/cero,
 *  saltar ahi desde cualquier punto es seguro.
 *
 *  Nota macOS: SDL exige que el bucle de eventos corra en el hilo principal.
 *  Aca corre en main(), asi que no hay nada que mover.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "args.h"
#include "bench.h"
#include "config.h"
#include "overlay.h"
#include "physics.h"
#include "render.h"
#include "sphere.h"
#include "timing.h"

/* Paso del barrido del angulo con las teclas [ y ], en radianes (~0.5 grados).
 * Es fino a proposito: al cruzar despacio el angulo aureo el patron colapsa a
 * rayas y vuelve a explotar, y ese es el mejor momento de la presentacion. */
#define ANGLE_STEP_RAD  (0.5 * SS_PI / 180.0)

/* --------------------------------------------------------------------------
 *  Rellena (o vuelve a llenar) la esfera con el angulo actual de la Config.
 *  Se usa al arrancar y cada vez que una tecla cambia el angulo.
 *
 *  Con --cannons K esto NO alcanza para animar la construccion: solo pinta el
 *  instante t=0 (una sola bolita recien disparada). El bucle con ventana
 *  llama a cannon_update() ademas de esto, en cada frame, para que la esfera
 *  se siga construyendo con el tiempo.
 * -------------------------------------------------------------------------- */
static void regen_sphere(SeedSet *seeds, const Config *cfg)
{
    if (cfg->cannon) {
        CannonParams cp = cannon_params_from_config(cfg);
        sphere_fill_cannon(seeds, &cp, 0.0);
    } else {
        sphere_fill_fibonacci(seeds, cfg->n, cfg->angle_rad, cfg->seed);
    }
}

/* --------------------------------------------------------------------------
 *  Reescribe el SoA para el instante 'sim_t' del modo canon. Se llama una vez
 *  por frame, ANTES de render_frame(): la posicion es funcion pura de t, asi
 *  que no hace falta ningun estado entre llamadas (ver sphere.h). No-op si
 *  sin --cannons.
 * -------------------------------------------------------------------------- */
static void cannon_update(SeedSet *seeds, const Config *cfg, double sim_t)
{
    if (!cfg->cannon) return;
    CannonParams cp = cannon_params_from_config(cfg);
    sphere_fill_cannon(seeds, &cp, sim_t);
}

/* ==========================================================================
 *  Camino de verificacion: un unico frame, volcado crudo a stdout, sin SDL.
 *
 *  Existe para comparar screensaver_seq contra screensaver_omp bit a bit en
 *  el mismo instante t: como todo el estado del frame es funcion pura de
 *  (i, t) -- sin historial de particulas, sin fisica cuando hay canones --
 *  los dos binarios tienen que producir exactamente el mismo framebuffer,
 *  con cualquier numero de hilos.
 *
 *      ./bin/screensaver_seq --n 2000 --dump-frame 12.5 > seq.raw
 *      ./bin/screensaver_omp --n 2000 --threads 32 --dump-frame 12.5 > omp.raw
 *      cmp seq.raw omp.raw && echo IDENTICOS
 *
 *  Reusa seedset_alloc/fb_alloc/regen_sphere/render_frame: no hay logica de
 *  render duplicada, solo el cableado para escribir el resultado a stdout
 *  en vez de a una ventana.
 * ========================================================================== */
static int run_dump_frame(Config *cfg)
{
    SeedSet     seeds = {0};
    Framebuffer fb    = {0};
    int rc = EXIT_FAILURE;

    CannonParams cp = cannon_params_from_config(cfg);
    int cap = cfg->cannon ? sphere_cannon_capacity(&cp) : cfg->n;
    if (seedset_alloc(&seeds, cap) != 0) {
        fprintf(stderr, "error: no se pudo reservar memoria para %d semillas\n", cap);
        goto cleanup;
    }
    if (fb_alloc(&fb, cfg->width, cfg->height) != 0) {
        fprintf(stderr, "error: no se pudo reservar el framebuffer %dx%d\n",
                cfg->width, cfg->height);
        goto cleanup;
    }
    regen_sphere(&seeds, cfg);
    if (cfg->cannon) sphere_fill_cannon(&seeds, &cp, cfg->dump_frame_t);

    render_frame(&fb, &seeds, cfg, cfg->dump_frame_t);

    /* Crudo, sin ningun encabezado: el 'cmp' de dos volcados con el mismo
     * w/h/formato ya alcanza. fwrite en un solo llamado, tamano fijo. */
    size_t n_px = (size_t)fb.w * (size_t)fb.h;
    size_t wrote = fwrite(fb.px, sizeof(uint32_t), n_px, stdout);
    if (wrote != n_px) {
        fprintf(stderr, "error: fwrite escribio %zu de %zu pixeles\n", wrote, n_px);
        goto cleanup;
    }

    rc = EXIT_SUCCESS;

cleanup:
    fb_free(&fb);
    seedset_free(&seeds);
    return rc;
}

/* ==========================================================================
 *  Camino headless: sin ventana, para medir el costo por frame (--no-render,
 *  --bench). No abre SDL. Sirve al protocolo de medicion de docs/02.
 * ========================================================================== */
static int run_headless(Config *cfg)
{
    SeedSet     seeds = {0};
    Framebuffer fb    = {0};
    int rc = EXIT_FAILURE;

    CannonParams cp = cannon_params_from_config(cfg);
    int cap = cfg->cannon ? sphere_cannon_capacity(&cp) : cfg->n;
    if (seedset_alloc(&seeds, cap) != 0) {
        fprintf(stderr, "error: no se pudo reservar memoria para %d semillas\n", cap);
        goto cleanup;
    }
    if (fb_alloc(&fb, cfg->width, cfg->height) != 0) {
        fprintf(stderr, "error: no se pudo reservar el framebuffer %dx%d\n",
                cfg->width, cfg->height);
        goto cleanup;
    }
    regen_sphere(&seeds, cfg);

    BenchStats st = bench_run(&fb, &seeds, cfg);
    if (cfg->csv) bench_print_csv(&st, cfg);
    else          bench_print_human(&st, cfg);

    rc = EXIT_SUCCESS;

cleanup:
    fb_free(&fb);
    seedset_free(&seeds);
    return rc;
}

/* ==========================================================================
 *  Camino con ventana: SDL + el bucle principal.
 * ========================================================================== */
static int run_window(Config *cfg)
{
    /* Todos en estado neutro para que 'goto cleanup' sea seguro desde cualquier
     * punto de fallo. */
    SeedSet       seeds    = {0};
    Framebuffer   fb       = {0};
    SDL_Window   *window   = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture  *texture  = NULL;
    int rc = EXIT_FAILURE;

    /* --- datos: semillas y framebuffer --------------------------------- */
    CannonParams cp = cannon_params_from_config(cfg);
    int cap = cfg->cannon ? sphere_cannon_capacity(&cp) : cfg->n;
    if (seedset_alloc(&seeds, cap) != 0) {
        fprintf(stderr, "error: no se pudo reservar memoria para %d semillas\n", cap);
        goto cleanup;
    }
    if (fb_alloc(&fb, cfg->width, cfg->height) != 0) {
        fprintf(stderr, "error: no se pudo reservar el framebuffer %dx%d\n",
                cfg->width, cfg->height);
        goto cleanup;
    }
    regen_sphere(&seeds, cfg);

    /* --- SDL ------------------------------------------------------------ */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "error: SDL_Init: %s\n", SDL_GetError());
        goto cleanup;
    }

    window = SDL_CreateWindow(
        "Screensaver - Esfera de Fibonacci",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        cfg->width, cfg->height, SDL_WINDOW_SHOWN);
    if (window == NULL) {
        fprintf(stderr, "error: SDL_CreateWindow: %s\n", SDL_GetError());
        goto cleanup;
    }

    Uint32 rflags = SDL_RENDERER_ACCELERATED;
    if (cfg->vsync) rflags |= SDL_RENDERER_PRESENTVSYNC;
    renderer = SDL_CreateRenderer(window, -1, rflags);
    if (renderer == NULL) {
        fprintf(stderr, "error: SDL_CreateRenderer: %s\n", SDL_GetError());
        goto cleanup;
    }

    /* ARGB8888 STREAMING: mismo formato que el framebuffer, asi subirlo es un
     * memcpy que hace SDL_UpdateTexture sin conversion por pixel. */
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                cfg->width, cfg->height);
    if (texture == NULL) {
        fprintf(stderr, "error: SDL_CreateTexture: %s\n", SDL_GetError());
        goto cleanup;
    }

    /* --- bucle principal ----------------------------------------------- */
    double sim_t     = 0.0;                 /* tiempo de animacion acumulado */
    double last      = now_seconds();
    double fps_ema   = 0.0;                  /* media movil exponencial */
    int    running   = 1;
    int    primer_frame = 1;                 /* su dt no es un frame real */

    while (running) {
        /* -- eventos (en el hilo principal, como exige macOS) ------------ */
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = 0;
            } else if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                    case SDLK_ESCAPE:
                    case SDLK_q:
                        running = 0;
                        break;
                    case SDLK_p:
                        cfg->paused = !cfg->paused;
                        break;
                    case SDLK_r:                       /* reiniciar */
                        cfg->angle_rad = SS_GOLDEN_ANG;
                        sim_t = 0.0;
                        regen_sphere(&seeds, cfg);
                        break;
                    case SDLK_LEFTBRACKET:             /* barrer angulo hacia abajo */
                        cfg->angle_rad -= ANGLE_STEP_RAD;
                        regen_sphere(&seeds, cfg);
                        break;
                    case SDLK_RIGHTBRACKET:            /* barrer angulo hacia arriba */
                        cfg->angle_rad += ANGLE_STEP_RAD;
                        regen_sphere(&seeds, cfg);
                        break;
                    default:
                        break;
                }
            }
        }

        /* -- tiempo: dt real, y avance de la animacion solo si no esta en pausa */
        double now = now_seconds();
        double dt  = now - last;
        last = now;
        if (!cfg->paused) sim_t += dt;

        /* -- fisica: repulsion de Coulomb + Verlet, solo si esta activa y no
         *    en pausa. El clamp NO es un 0.1 fijo: el limite de estabilidad de
         *    Verlet depende de N (va como 1/sqrt(N), ver physics_max_dt). Con
         *    un tope fijo y N grande la integracion explota y el patron de
         *    Fibonacci se deshace en menos de un segundo. Pasado el tope la
         *    fisica avanza en camara lenta, que es preferible a que reviente. */
        if (!cfg->paused && cfg->physics) {
            double dt_max  = physics_max_dt(seeds.n);
            double dt_phys = (dt < dt_max) ? dt : dt_max;
            PhysicsParams pp = { SS_DEF_PHYS_K, SS_DEF_PHYS_EPSILON,
                                  SS_DEF_PHYS_GAMMA, SS_DEF_PHYS_MASS };
            physics_step(&seeds, &pp, dt_phys);
        }

        /* -- canon: reescribe el SoA para este instante, funcion pura de
         *    sim_t (ver sphere.h). En pausa sim_t no avanza, asi que la
         *    construccion se congela sola sin codigo extra aca. */
        cannon_update(&seeds, cfg, sim_t);

        /* -- dibujo: render_frame se lleva el ~90% del tiempo ------------ */
        render_frame(&fb, &seeds, cfg, sim_t);
        overlay_stats(&fb, cfg, fps_ema, seeds.n);
        if (cfg->physics) {
            double div = sphere_mean_divergence_deg(&seeds);
            overlay_physics(&fb, cfg, div);
        }

        /* -- presentar --------------------------------------------------- */
        SDL_UpdateTexture(texture, NULL, fb.px, fb.w * (int)sizeof(uint32_t));
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        /* -- FPS: media movil exponencial sobre el dt instantaneo.
         *    El primer frame NO cuenta: su 'dt' solo mide el tiempo entre que
         *    se armo el reloj y el primer PollEvent (microsegundos), no un
         *    frame de verdad. Sembrar la media con eso mostraba FPS de decenas
         *    de miles durante los primeros segundos. */
        if (dt > 0.0 && !primer_frame) {
            double inst = 1.0 / dt;
            fps_ema = (fps_ema > 0.0) ? (fps_ema * 0.9 + inst * 0.1) : inst;
        }
        primer_frame = 0;
    }

    rc = EXIT_SUCCESS;

cleanup:
    /* Orden inverso a la creacion. Cada if evita tocar lo que nunca se creo. */
    if (texture)  SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window)   SDL_DestroyWindow(window);
    SDL_Quit();
    fb_free(&fb);
    seedset_free(&seeds);
    return rc;
}

int main(int argc, char **argv)
{
    Config cfg;

    /* 1) argumentos: --help sale con exito; un error sale con EXIT_FAILURE. */
    ArgsStatus st = args_parse(argc, argv, &cfg);
    if (st == ARGS_HELP)  return EXIT_SUCCESS;
    if (st != ARGS_OK)    return EXIT_FAILURE;

    /* 2) validacion cruzada del dominio (N, canvas, fill, ...). */
    if (config_validate(&cfg) != 0) return EXIT_FAILURE;

    /* 2.5) hilos de OpenMP, ANTES de que exista cualquier region paralela.
     *    El #ifdef es lo que permite que este mismo main.c compile en
     *    screensaver_seq sin -fopenmp: el bloque entero desaparece. */
#ifdef _OPENMP
    if (cfg.threads > 0) omp_set_num_threads(cfg.threads);
#endif

    /* 3) dejar registrado con que parametros se corrio -- salvo en modo CSV
     *    o dump-frame, que redirigen stdout a un archivo y tienen que
     *    quedar limpios: config_print() mezclado con los bytes crudos del
     *    framebuffer romperia el 'cmp' byte a byte entre seq y omp. */
    if (!cfg.csv && !cfg.dump_frame) config_print(&cfg);

    /* 4) dispatch: volcado de verificacion, ventana, o headless para medir. */
    if (cfg.dump_frame) return run_dump_frame(&cfg);
    if (cfg.headless)   return run_headless(&cfg);
    return run_window(&cfg);
}
