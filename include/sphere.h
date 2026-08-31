/* ===========================================================================
 *  sphere.h - La esfera de Fibonacci: el nucleo matematico del screensaver.
 *
 *  Distribuye N semillas sobre la esfera unitaria usando el angulo aureo:
 *
 *        z_n = 1 - 2(n + 1/2)/N          <- reparte AREAS iguales
 *        rho_n = sqrt(1 - z_n^2)
 *        theta_n = n * psi               <- psi = 2*pi/phi^2
 *
 *        p_n = (rho*cos(theta), rho*sin(theta), z)
 *
 *  Por que z uniforme reparte areas iguales: por el teorema de Arquimedes, el
 *  area de una franja esferica entre dos planos paralelos es 2*pi*R*dz, o sea
 *  depende SOLO de dz y no de la latitud. El area es lineal en z, asi que
 *  repartir z uniformemente reparte area uniformemente.
 *
 *  Es el analogo exacto del r = c*sqrt(n) del caso plano: en 2D el area
 *  acumulada es cuadratica y al invertirla aparece la raiz; en 3D Arquimedes
 *  la vuelve lineal y la raiz desaparece.
 *
 *  Detalle completo en docs/01-FUNDAMENTO-MATEMATICO.md, seccion 2.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#ifndef SPHERE_H
#define SPHERE_H

#include <stdint.h>

#include "config.h"
#include "vec3.h"

/* ---------------------------------------------------------------------------
 *  SeedSet - las semillas, en SoA (Structure of Arrays).
 *
 *  Se usa SoA y no AoS (un arreglo de structs {x,y,z,...}) a proposito:
 *
 *   - El bucle interno del Voronoi recorre SOLO x[], y[], z[]. Con SoA esos
 *     tres arreglos se leen secuencialmente y cada linea de cache trae 16
 *     valores utiles. Con AoS, cada linea traeria tambien velocidad,
 *     aceleracion y color, que ese bucle no usa: ~60% del ancho de banda
 *     desperdiciado.
 *   - SoA es la unica forma de que el compilador vectorice el bucle. Con AoS
 *     las componentes quedan intercaladas y haria falta un gather.
 *
 *  Es una decision de localidad de cache que va a aparecer en el informe
 *  cuando midamos por que el kernel escala como escala.
 * ------------------------------------------------------------------------- */
typedef struct {
    int       n;         /* semillas en uso                                  */
    int       capacity;  /* semillas asignadas                               */

    float    *x, *y, *z;     /* posicion sobre la esfera unitaria (|p| = 1)  */
    float    *vx, *vy, *vz;  /* velocidad tangencial (solo si hay fisica)    */
    float    *ax, *ay, *az;  /* aceleracion         (solo si hay fisica)     */
    uint32_t *color;         /* color pseudoaleatorio ARGB8888               */
} SeedSet;

/* Reserva memoria para 'capacity' semillas. Devuelve 0 si todo bien, o un
 * valor negativo si fallo malloc (y en ese caso libera lo que ya habia
 * reservado, sin fugas). */
int  seedset_alloc(SeedSet *s, int capacity);

/* Libera y deja la estructura en cero. Seguro de llamar dos veces o sobre una
 * estructura ya limpia. */
void seedset_free(SeedSet *s);

/* ---------------------------------------------------------------------------
 *  sphere_dir_fib - la posicion i-esima del patron de Fibonacci, sola.
 *
 *  Es el nucleo matematico de sphere_fill_fibonacci(), extraido para que
 *  cualquier otra rutina que necesite "donde va la semilla i" (el cañon del
 *  modo --cannon, por ejemplo) lo calcule llamando a esto y no copiando la
 *  formula. Dos copias del angulo aureo tarde o temprano se desincronizan.
 *
 *    i          indice de la semilla, 0..n-1
 *    n          numero total de semillas (para el reparto de areas)
 *    angle_rad  angulo de divergencia en RADIANES
 *
 *  Devuelve un punto sobre la esfera UNITARIA (|p| = 1 exacto, salvo error
 *  de redondeo de float).
 * ------------------------------------------------------------------------- */
Vec3 sphere_dir_fib(int i, int n, double angle_rad);

/* ---------------------------------------------------------------------------
 *  Genera la esfera de Fibonacci.
 *
 *    s          conjunto ya reservado con capacity >= n
 *    n          numero de semillas (el parametro N del enunciado)
 *    angle_rad  angulo de divergencia EN RADIANES. Pasar SS_GOLDEN_ANG para el
 *               patron correcto; cualquier otro valor sirve para demostrar en
 *               vivo como degenera (es lo que hacen las teclas [ y ]).
 *    seed       semilla del PRNG para los colores.
 *
 *  El acumulador del angulo va en double aunque las posiciones se guarden en
 *  float: n*psi con n en los miles pierde precision rapido en float y el
 *  patron se degrada de forma visible.
 * ------------------------------------------------------------------------- */
void sphere_fill_fibonacci(SeedSet *s, int n, double angle_rad, uint64_t seed);

/* ---------------------------------------------------------------------------
 *  CannonParams - todo lo que necesita el canon, en un solo lugar.
 *
 *  Se agrupan en una estructura y no como diez argumentos sueltos por dos
 *  razones: la lista ya no cabe en una linea legible, y sobre todo porque
 *  sphere_fill_cannon() se llama UNA VEZ POR FRAME desde tres sitios
 *  distintos (main.c con ventana, main.c al regenerar, bench.c). Con los
 *  argumentos sueltos, agregar un parametro obliga a tocar los tres; con la
 *  estructura, se toca cannon_params_from_config() y nada mas.
 *
 *  No hay estado adentro: es una copia de los campos de Config que le
 *  interesan al canon. sphere.c no depende del resto de la Config, y los
 *  tests pueden armar una CannonParams a mano sin parsear argumentos.
 * ------------------------------------------------------------------------- */
typedef struct {
    int      n;             /* semillas del patron (el parametro N)          */
    double   angle_rad;     /* angulo de divergencia, en radianes            */
    uint64_t seed;          /* semilla del PRNG de colores                   */

    double   fire_rate;     /* R: disparos por segundo, POR CANON (> 0)      */
    double   muzzle_speed;  /* V: radios/s; el vuelo dura 1/V (> 0)          */
    int      trail;         /* L: fantasmas de estela por bolita (>= 0)      */

    int      cannons;       /* K: canones simultaneos (>= 1)                 */
    int      layout;        /* SS_CANNON_ROUNDROBIN o SS_CANNON_BLOCKS       */
    double   muzzle_radius; /* r0: radio de la esfera chica de bocas, [0, 1) */
} CannonParams;

/* Copia de Config los campos que le importan al canon. Es el unico puente
 * entre la Config del programa y el nucleo geometrico. */
CannonParams cannon_params_from_config(const Config *cfg);

/* ---------------------------------------------------------------------------
 *  cannon_slot - a que canon le toca el indice i, y en que ronda lo dispara.
 *
 *  Es la decision de diseno del reparto (--cannon-layout), aislada en una
 *  funcion pura para poder testearla: la union de los K canones tiene que
 *  cubrir los N indices EXACTAMENTE una vez, sin huecos ni repetidos.
 *
 *    ROUNDROBIN: canon = i % K,  ronda = i / K.
 *    BLOCKS:     bloques contiguos con base(c) = techo(c*N/K); el inverso
 *                exacto de ese reparto es canon = piso(i*K/N), y la ronda es
 *                la posicion dentro del bloque.
 *
 *  En los dos casos la ronda queda en [0, cannon_rounds(N,K)), que es lo que
 *  permite que todos los canones compartan el mismo periodo de recirculacion.
 *
 *  'cannon_out' y/o 'round_out' pueden ser NULL si solo interesa uno.
 * ------------------------------------------------------------------------- */
void cannon_slot(int i, int n, int cannons, int layout,
                 int *cannon_out, int *round_out);

/* Rondas de disparo que da cada canon en un ciclo completo: techo(n/K). El
 * periodo de recirculacion es T_ciclo = cannon_rounds(n,K) / fire_rate. */
int cannon_rounds(int n, int cannons);

/* ---------------------------------------------------------------------------
 *  cannon_muzzle - donde esta la boca del canon c.
 *
 *  Los K canones se colocan sobre una esfera chica de radio 'muzzle_radius'
 *  usando la MISMA construccion de Fibonacci con K puntos. Cero matematica
 *  nueva: es sphere_dir_fib(c, K, angle_rad) escalado.
 *
 *  Con muzzle_radius = 0 todas las bocas colapsan al origen, que es el
 *  comportamiento del canon unico original.
 * ------------------------------------------------------------------------- */
Vec3 cannon_muzzle(int c, const CannonParams *p);

/* ---------------------------------------------------------------------------
 *  Cuantas semillas hay que reservar para el modo canon: las n del patron mas
 *  los fantasmas de estela de las que estan en pleno vuelo.
 *
 *  En regimen permanente, la fraccion del ciclo que una bolita pasa volando es
 *  (1/V) / T_ciclo, asi que la cantidad en vuelo en cualquier instante es
 *
 *      en_vuelo = n * (1/V) / T_ciclo = K * R / V
 *
 *  A eso se le suma un margen de K: cada canon puede tener una bolita de mas
 *  en el aire por como cae el redondeo de la ronda dentro de la ventana de
 *  vuelo. Y se recorta a n, porque no puede haber mas bolitas en vuelo que
 *  indices en el patron.
 *
 *      capacity = n + min(techo(K*R/V) + K, n) * L
 *
 *  Es la cota del PEOR caso: todas las que vuelan con la cola completa de L
 *  fantasmas. El pico real ronda la mitad, porque en cualquier instante solo
 *  la fraccion mas vieja de las que vuelan alcanzo a desplegar los L (el
 *  modelo de carga en regimen permanente es n + (K*R/V)*(L/2), ver
 *  config_print). La holgura es a proposito: quedarse corto no significaria
 *  "usar mas memoria de la ideal" sino truncar la escritura y que se vean
 *  bolitas desaparecer.
 *
 *  Se calcula ANTES de seedset_alloc(): esa funcion reparte diez punteros en
 *  un solo malloc y no hay camino de crecimiento despues.
 *
 *  Devuelve al menos n (si trail es 0 o los parametros son degenerados, no
 *  reserva de menos).
 * ------------------------------------------------------------------------- */
int sphere_cannon_capacity(const CannonParams *p);

/* ---------------------------------------------------------------------------
 *  sphere_fill_cannon - la esfera de Fibonacci, construyendose a canonazos,
 *  para siempre.
 *
 *  Reescribe el SoA para el instante 't'. K canones reparten los N indices
 *  entre ellos (cannon_slot) y disparan una bolita por ronda cada 1/R
 *  segundos. La bolita del indice i sale de la boca de su canon y viaja en
 *  linea recta hasta su posicion definitiva de Fibonacci (sphere_dir_fib),
 *  tardando 1/V segundos.
 *
 *  RECIRCULACION -- la pieza que hace que la animacion no se termine. En vez
 *  de que el slot i aterrice y se quede quieto para siempre, se vuelve a
 *  disparar cada T_ciclo segundos:
 *
 *      T_ciclo      = rondas(N, K) / R
 *      t_disparo(i) = ronda(i) / R
 *      fase(i, t)   = fmod(t - t_disparo(i), T_ciclo)
 *      radio(i, t)  = clamp(V * fase(i, t), 0, 1)
 *      pos(i, t)    = lerp(boca(canon(i)), dir_fib(i), radio(i, t))
 *
 *  La esfera se mantiene llena, los canones no paran nunca y la formula sigue
 *  siendo cerrada: solo se le agrego un fmod. Ademas la carga dibujada queda
 *  CONSTANTE en regimen permanente, que es lo que hace de K una perilla de
 *  carga util para el informe de escalabilidad.
 *
 *  ESTELA: detras de cada bolita en vuelo se dibujan 'trail' fantasmas; el
 *  fantasma j es la MISMA formula evaluada en (fase - j*delta), asi que la
 *  estela tampoco tiene estado que integrar. Un fantasma nunca cruza hacia
 *  atras del disparo que lo genero (fase - j*delta < 0 corta la cola): sin
 *  eso, una bolita recien recirculada arrastraria una cola pegada a la
 *  superficie del ciclo anterior.
 *
 *  TODO sale de una funcion pura de (i, t): dos llamadas con el mismo t dan
 *  el mismo resultado bit a bit, sin importar cuantos frames se dibujaron
 *  antes. Es la propiedad que permite comparar el framebuffer secuencial
 *  contra el paralelo en el mismo instante.
 *
 *    s   conjunto ya reservado con capacity >= sphere_cannon_capacity(p)
 *    p   parametros del canon (ver CannonParams)
 *    t   tiempo de la animacion, en segundos (t <= 0 => esfera casi vacia)
 *
 *  Deja s->n en (vivas + fantasmas dibujados este instante) y escribe
 *  vx/vy/vz/ax/ay/az en cero: el modo canon es incompatible con --physics
 *  (config_validate lo exige), pero el SoA no debe quedar con basura.
 * ------------------------------------------------------------------------- */
void sphere_fill_cannon(SeedSet *s, const CannonParams *p, double t);

/* Devuelve la semilla i como Vec3. */
static inline Vec3 seed_pos(const SeedSet *s, int i)
{
    return v3(s->x[i], s->y[i], s->z[i]);
}

/* ---------------------------------------------------------------------------
 *  Angulo de divergencia medio medido sobre las posiciones ACTUALES.
 *
 *  Con el patron generado analiticamente esto devuelve el angulo que se paso
 *  como parametro (es un test de consistencia). Su verdadero valor aparece
 *  cuando la fisica esta activa: ahi las semillas se mueven por repulsion y
 *  este numero se ve converger solo hacia 137.5 grados. Es el momento mas
 *  fuerte de la presentacion: no programamos el angulo aureo, programamos que
 *  las semillas se empujen y phi aparecio.
 *
 *  Devuelve el angulo en GRADOS.
 *
 *  NO filtra bolitas en vuelo ni fantasmas de estela: recorre s->n de punta a
 *  punta asumiendo que todas estan sobre la esfera. Eso esta bien porque el
 *  unico llamador (main.c) solo la invoca si cfg->physics esta activo, y
 *  config_validate() ya rechaza --cannon junto con --physics -- las dos
 *  situaciones nunca coinciden. Si algun dia se llega a llamar con --cannon,
 *  hay que filtrar por radio antes (ver rr[] en render.c).
 * ------------------------------------------------------------------------- */
double sphere_mean_divergence_deg(const SeedSet *s);

#endif /* SPHERE_H */
