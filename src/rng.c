/* ===========================================================================
 *  rng.c - Implementacion del hash pseudoaleatorio sin estado.
 *
 *  Ver rng.h para la justificacion de por que esto es un hash y no un
 *  generador con estado. Resumen: sin estado global no hay condicion de
 *  carrera, no hay lock que serialice el kernel, y la salida no depende del
 *  orden en que los hilos hagan las llamadas.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#include "rng.h"

/* Constantes de SplitMix64. No son arbitrarias: se eligieron para maximizar
 * el efecto avalancha (que cambiar un bit de la entrada cambie ~la mitad de
 * los bits de la salida). */
uint64_t rng_mix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;          /* 2^64 / phi: el numero aureo otra vez */
    x  = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x  = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

uint64_t rng_hash(uint64_t index, uint64_t seed)
{
    /* Se mezcla la semilla ANTES de combinarla para que semillas parecidas
     * (12345 y 12346) den secuencias completamente distintas. */
    return rng_mix64(index + rng_mix64(seed));
}

float rng_f01(uint64_t index, uint64_t seed)
{
    /* 24 bits altos -> [0, 1). Se usan los altos porque en un mezclador son
     * los de mejor distribucion, y 24 bits es exactamente la mantisa de un
     * float, asi que no se pierde nada. */
    uint64_t h = rng_hash(index, seed);
    return (float)(h >> 40) * (1.0f / 16777216.0f);   /* 2^24 */
}

float rng_f01_ch(uint64_t index, uint64_t seed, uint32_t channel)
{
    uint64_t h = rng_hash(index, seed ^ rng_mix64((uint64_t)channel + 1u));
    return (float)(h >> 40) * (1.0f / 16777216.0f);
}

float rng_range(uint64_t index, uint64_t seed, float lo, float hi)
{
    return lo + (hi - lo) * rng_f01(index, seed);
}
