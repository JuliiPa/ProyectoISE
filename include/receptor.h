
#ifndef RECEPTOR_H
#define RECEPTOR_H

#include "protocolo.h"

/* Funcion que procesa un mensaje recibido */
typedef int (*ProcesarMensaje)(
    const Mensaje *mensaje,
    void *datos
);

/* Informacion necesaria para el hilo receptor */
typedef struct {
    int fdPipe;
    ProcesarMensaje procesar;
    void *datos;
    int resultado;
} ReceptorContexto;

/* Funcion ejecutada por el hilo POSIX */
void *hilo_receptor(void *argumento);

#endif
