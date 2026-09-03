/* timing.c - Reloj monotonico, un #ifdef por plataforma. */
#include "timing.h"

#if defined(_WIN32)

#include <windows.h>

double now_seconds(void)
{
    /* La frecuencia es fija mientras el sistema esta encendido: se cachea. */
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}

#else  /* POSIX: Linux y macOS */

#include <time.h>

/* MONOTONIC y no MONOTONIC_RAW: RAW no lo ajusta NTP y deriva del segundo real. */
double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

#endif
