/* rng.c - Hash pseudoaleatorio sin estado (SplitMix64). Ver rng.h. */
#include "rng.h"

/* Constantes de SplitMix64: maximizan el efecto avalancha. */
uint64_t rng_mix64(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;          /* 2^64 / phi */
    x  = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x  = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

/* La semilla se mezcla antes para que 12345 y 12346 den secuencias distintas. */
uint64_t rng_hash(uint64_t index, uint64_t seed)
{
    return rng_mix64(index + rng_mix64(seed));
}

/* 24 bits altos -> [0,1): los de mejor distribucion, y son la mantisa del float. */
float rng_f01(uint64_t index, uint64_t seed)
{
    uint64_t h = rng_hash(index, seed);
    return (float)(h >> 40) * (1.0f / 16777216.0f);   /* 2^24 */
}

/* Igual, con un canal para sacar valores independientes del mismo indice. */
float rng_f01_ch(uint64_t index, uint64_t seed, uint32_t channel)
{
    uint64_t h = rng_hash(index, seed ^ rng_mix64((uint64_t)channel + 1u));
    return (float)(h >> 40) * (1.0f / 16777216.0f);
}

float rng_range(uint64_t index, uint64_t seed, float lo, float hi)
{
    return lo + (hi - lo) * rng_f01(index, seed);
}
