/* ===========================================================================
 *  timing.c - Reloj monotonico, un #ifdef por plataforma.
 *
 *    Windows : QueryPerformanceCounter / QueryPerformanceFrequency
 *    macOS   : clock_gettime(CLOCK_MONOTONIC)   (existe desde 10.12)
 *    Linux   : clock_gettime(CLOCK_MONOTONIC)
 *
 *  Se prefiere CLOCK_MONOTONIC sobre CLOCK_MONOTONIC_RAW: RAW no lo ajusta NTP
 *  y suena "mas puro", pero justamente por eso deriva respecto al segundo real
 *  y para medir FPS queremos segundos honestos. En Windows, QPC es el
 *  equivalente de alta resolucion y ya es monotonico.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#include "timing.h"

#if defined(_WIN32)

#include <windows.h>

double now_seconds(void)
{
    /* La frecuencia del contador es fija mientras el sistema esta encendido,
     * asi que se consulta una sola vez y se cachea. */
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) {
        QueryPerformanceFrequency(&freq);
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}

#else  /* POSIX: Linux y macOS comparten exactamente el mismo camino */

#include <time.h>

double now_seconds(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

#endif
