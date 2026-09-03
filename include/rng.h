/* rng.h - Pseudoaleatoriedad SIN ESTADO: un hash f(i, seed), no un generador.
 * rand() seria una carrera en paralelo, y su lock serializaria el kernel. */
#ifndef RNG_H
#define RNG_H

#include <stdint.h>

/* Mezclador de SplitMix64 (Steele, Lea & Flood, 2014): puro, ~10 instrucciones. */
uint64_t rng_mix64(uint64_t x);

/* Hash de un indice con una semilla: rng_hash(i, cfg->seed). */
uint64_t rng_hash(uint64_t index, uint64_t seed);

/* Flotante uniforme en [0, 1), de los 24 bits altos del hash. */
float    rng_f01(uint64_t index, uint64_t seed);

/* Flotante uniforme en [lo, hi). */
float    rng_range(uint64_t index, uint64_t seed, float lo, float hi);

/* Variante con "canal": varios valores independientes del mismo indice. */
float    rng_f01_ch(uint64_t index, uint64_t seed, uint32_t channel);

#endif /* RNG_H */
