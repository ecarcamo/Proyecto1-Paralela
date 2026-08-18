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
 * -------------------------------------------------------------------------- */
static void regen_sphere(SeedSet *seeds, const Config *cfg)
{
    sphere_fill_fibonacci(seeds, cfg->n, cfg->angle_rad, cfg->seed);
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

    if (seedset_alloc(&seeds, cfg->n) != 0) {
        fprintf(stderr, "error: no se pudo reservar memoria para %d semillas\n", cfg->n);
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
    if (seedset_alloc(&seeds, cfg->n) != 0) {
        fprintf(stderr, "error: no se pudo reservar memoria para %d semillas\n", cfg->n);
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
         *    en pausa. El clamp evita un dt gigante tras una pausa larga o un
         *    hipo del sistema, que mandaria las semillas a volar de un salto. */
        if (!cfg->paused && cfg->physics) {
            double dt_phys = dt;
            if (dt_phys > 0.1) dt_phys = 0.1;
            PhysicsParams pp = { SS_DEF_PHYS_K, SS_DEF_PHYS_EPSILON,
                                  SS_DEF_PHYS_GAMMA, SS_DEF_PHYS_MASS };
            physics_step(&seeds, &pp, dt_phys);
        }

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

        /* -- FPS: media movil exponencial sobre el dt instantaneo -------- */
        if (dt > 0.0) {
            double inst = 1.0 / dt;
            fps_ema = (fps_ema > 0.0) ? (fps_ema * 0.9 + inst * 0.1) : inst;
        }
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

    /* 3) dejar registrado con que parametros se corrio (salvo en modo CSV,
     *    que debe quedar limpio para redirigir a un archivo). */
    if (!cfg.csv) config_print(&cfg);

    /* 4) dispatch: con ventana o headless para medir. */
    if (cfg.headless) return run_headless(&cfg);
    return run_window(&cfg);
}
