/* args.h - Parseo defensivo de argv (el dominio cruzado lo valida config.h). */
#ifndef ARGS_H
#define ARGS_H

#include "config.h"

/* Resultado del parseo; main() decide con que codigo salir a partir de esto. */
typedef enum {
    ARGS_OK    =  0,   /* seguir con la ejecucion normal    */
    ARGS_HELP  =  1,   /* se pidio --help: salir con EXITO  */
    ARGS_ERROR = -1    /* argumento invalido: EXIT_FAILURE  */
} ArgsStatus;

/* Parte de los defaults y sobreescribe lo que venga en argv; nunca aborta. */
ArgsStatus args_parse(int argc, char **argv, Config *cfg);

/* Modo de uso; sale en --help y ante cualquier error de argumentos. */
void args_usage(const char *prog);

#endif /* ARGS_H */
