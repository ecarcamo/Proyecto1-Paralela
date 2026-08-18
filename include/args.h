/* ===========================================================================
 *  args.h - Parseo de la linea de comandos con programacion defensiva.
 *
 *  El enunciado califica dos cosas que se ganan justo aqui: "programacion
 *  defensiva en caso de errores en el ingreso de datos" y "evitar hard-coded
 *  variables". Por eso TODO parametro entra por argv y termina en la Config,
 *  y ningun valor invalido pasa silenciosamente.
 *
 *  Division de responsabilidades (ver config.c):
 *    - args_parse()      valida el FORMATO de cada argumento por separado:
 *                        que --n sea un entero de verdad y no "abc" ni "3.5",
 *                        que no falte el valor, que no desborde strtol.
 *    - config_validate() valida el DOMINIO cruzado una vez que todos los
 *                        campos estan llenos: N >= 1, canvas >= 640x480, etc.
 *
 *  Proyecto 1 - Computacion Paralela y Distribuida (UVG)
 * =========================================================================== */
#ifndef ARGS_H
#define ARGS_H

#include "config.h"

/* Resultado del parseo. main() decide con que codigo salir a partir de esto. */
typedef enum {
    ARGS_OK    =  0,   /* todo bien: seguir con la ejecucion normal          */
    ARGS_HELP  =  1,   /* se pidio --help: se imprimio el uso, salir con EXITO */
    ARGS_ERROR = -1    /* argumento invalido: salir con EXIT_FAILURE          */
} ArgsStatus;

/* Parte de una Config con los valores por defecto y sobreescribe solo lo que
 * venga en argv. No aborta el proceso: reporta el error por stderr y devuelve
 * ARGS_ERROR para que main libere lo que tenga y salga limpio. */
ArgsStatus args_parse(int argc, char **argv, Config *cfg);

/* Imprime el modo de uso. Sale en --help y tambien cada vez que hay un error
 * de argumentos, para que el usuario vea de una que opciones existen. */
void args_usage(const char *prog);

#endif /* ARGS_H */
