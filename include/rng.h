/* ===========================================================================
 *  rng.h - Generacion pseudoaleatoria SIN ESTADO GLOBAL.
 *
 *  El enunciado pide colores "generados de forma pseudoaleatoria". La tentacion
 *  obvia es usar rand(), y seria un error grave en este proyecto por tres
 *  razones:
 *
 *    1. rand() tiene ESTADO GLOBAL compartido. En la fase paralela, varios
 *       hilos llamandolo simultaneamente son una condicion de carrera pura.
 *    2. Aunque se protegiera con un lock, ese lock serializaria el kernel y
 *       destruiria el speedup: justo lo contrario de lo que buscamos.
 *    3. Con estado, el orden de las llamadas cambia el resultado. Como en
 *       paralelo el orden NO esta definido, la salida secuencial y la paralela
 *       no coincidirian y no podriamos validar por checksum que el paralelo
 *       es correcto.
 *
 *  La solucion es un HASH, no un generador: una funcion pura que mapea
 *  (indice, semilla) -> valor pseudoaleatorio. Sin estado, reentrante,
 *  determinista y llamable desde cualquier hilo sin sincronia.
 *
 *      color_de_la_semilla_i = f(i, seed)      siempre, en cualquier orden
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#ifndef RNG_H
#define RNG_H

#include <stdint.h>

/* Mezclador de bits de SplitMix64 (Steele, Lea & Flood, 2014).
 * Pasa las pruebas de BigCrush y es una sola funcion sin bucles: unas 10
 * instrucciones. Puro: mismo x, mismo resultado, siempre. */
uint64_t rng_mix64(uint64_t x);

/* Hash de un indice con una semilla. Es la funcion que se usa en la practica:
 *     rng_hash(i, cfg->seed)  ->  valor pseudoaleatorio de la semilla i        */
uint64_t rng_hash(uint64_t index, uint64_t seed);

/* Flotante uniforme en [0, 1) a partir del hash.
 * Usa los 24 bits altos, que son los de mejor calidad en un mezclador. */
float    rng_f01(uint64_t index, uint64_t seed);

/* Flotante uniforme en [lo, hi). */
float    rng_range(uint64_t index, uint64_t seed, float lo, float hi);

/* Variante con "canal": permite sacar varios valores independientes del mismo
 * indice sin correlacion entre ellos (por ejemplo tono, saturacion y brillo).
 * Equivale a rng_hash(index, seed ^ mezcla(channel)). */
float    rng_f01_ch(uint64_t index, uint64_t seed, uint32_t channel);

#endif /* RNG_H */
