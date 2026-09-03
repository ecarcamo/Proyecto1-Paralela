/* main.c - Bucle principal: SDL, textura y presentacion.
 * Un solo 'goto cleanup' libera todo en orden inverso a la creacion; como cada
 * recurso arranca en NULL/cero, saltar ahi desde cualquier punto es seguro. */
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

/* Paso de las teclas [ y ]: fino para ver el patron colapsar y rearmarse. */
#define ANGLE_STEP_RAD  (0.5 * SS_PI / 180.0)

/* Rellena la esfera con el angulo actual; con canones solo pinta t = 0. */
static void regen_sphere(SeedSet *seeds, const Config *cfg)
{
    if (cfg->cannon) {
        CannonParams cp = cannon_params_from_config(cfg);
        sphere_fill_cannon(seeds, &cp, 0.0);
    } else {
        sphere_fill_fibonacci(seeds, cfg->n, cfg->angle_rad, cfg->seed);
    }
}

/* Reescribe el SoA para el instante sim_t; no-op sin --cannons. */
static void cannon_update(SeedSet *seeds, const Config *cfg, double sim_t)
{
    if (!cfg->cannon) return;
    CannonParams cp = cannon_params_from_config(cfg);
    sphere_fill_cannon(seeds, &cp, sim_t);
}

/* --dump-frame T: un frame crudo a stdout, para comparar seq vs omp con cmp. */
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

    /* Sin encabezado: el cmp de dos volcados del mismo w/h/formato ya alcanza. */
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

/* Camino headless (--no-render, --bench): mide sin abrir SDL. */
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

/* Camino con ventana: SDL + el bucle principal. */
static int run_window(Config *cfg)
{
    /* Todos en estado neutro: 'goto cleanup' es seguro desde cualquier fallo. */
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

    /* ARGB8888 STREAMING: mismo formato que el fb, subirlo es un memcpy. */
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
        /* Eventos: en el hilo principal, como exige macOS. */
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

        /* Tiempo: dt real; la animacion avanza solo si no esta en pausa. */
        double now = now_seconds();
        double dt  = now - last;
        last = now;
        if (!cfg->paused) sim_t += dt;

        /* Fisica: el tope de dt depende de N (1/sqrt(N), ver physics_max_dt);
         * pasado el tope avanza en camara lenta en vez de explotar. */
        if (!cfg->paused && cfg->physics) {
            double dt_max  = physics_max_dt(seeds.n);
            double dt_phys = (dt < dt_max) ? dt : dt_max;
            PhysicsParams pp = { SS_DEF_PHYS_K, SS_DEF_PHYS_EPSILON,
                                  SS_DEF_PHYS_GAMMA, SS_DEF_PHYS_MASS };
            physics_step(&seeds, &pp, dt_phys);
        }

        /* Canon: funcion pura de sim_t, asi que la pausa lo congela sola. */
        cannon_update(&seeds, cfg, sim_t);

        /* Dibujo: render_frame se lleva el ~90% del tiempo. */
        render_frame(&fb, &seeds, cfg, sim_t);
        overlay_stats(&fb, cfg, fps_ema, seeds.n);
        if (cfg->physics) {
            double div = sphere_mean_divergence_deg(&seeds);
            overlay_physics(&fb, cfg, div);
        }

        /* Presentar. */
        SDL_UpdateTexture(texture, NULL, fb.px, fb.w * (int)sizeof(uint32_t));
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        /* FPS: media movil exponencial. El primer frame no cuenta, su dt no
         * es un frame real y sembraba la media con decenas de miles. */
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

    /* 1) argumentos: --help sale con exito, un error con EXIT_FAILURE. */
    ArgsStatus st = args_parse(argc, argv, &cfg);
    if (st == ARGS_HELP)  return EXIT_SUCCESS;
    if (st != ARGS_OK)    return EXIT_FAILURE;

    /* 2) validacion cruzada del dominio (N, canvas, fill, ...). */
    if (config_validate(&cfg) != 0) return EXIT_FAILURE;

    /* 2.5) hilos de OpenMP, antes de cualquier region paralela. */
#ifdef _OPENMP
    if (cfg.threads > 0) omp_set_num_threads(cfg.threads);
#endif

    /* 3) registrar los parametros, salvo en CSV o dump-frame: ahi stdout
     *    tiene que quedar limpio para el cmp byte a byte. */
    if (!cfg.csv && !cfg.dump_frame) config_print(&cfg);

    /* 4) dispatch: volcado de verificacion, ventana o headless. */
    if (cfg.dump_frame) return run_dump_frame(&cfg);
    if (cfg.headless)   return run_headless(&cfg);
    return run_window(&cfg);
}
