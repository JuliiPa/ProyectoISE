
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "receptor.h"

/* Lee un mensaje completo desde el pipe */
static int leerMensaje(int fd, Mensaje *mensaje)
{
    size_t recibidos = 0;
    size_t total = sizeof(Mensaje);
    char *destino = (char *)mensaje;

    while (recibidos < total) {
        ssize_t n = read(
            fd,
            destino + recibidos,
            total - recibidos
        );

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            perror("Error al leer el pipe");
            return -1;
        }

        if (n == 0) {
            if (recibidos == 0) {
                return 0;
            }

            fprintf(stderr,
                "Error: mensaje incompleto.\n");
            return -1;
        }

        recibidos += (size_t)n;
    }

    return 1;
}

/* Comprueba que el mensaje tenga datos validos */
static int validarMensaje(const Mensaje *mensaje)
{
    if (mensaje->tipo != MSG_INICIO &&
        mensaje->tipo != MSG_ENCUESTA &&
        mensaje->tipo != MSG_FIN) {
        return 0;
    }

    if (mensaje->idAgente <= 0) {
        return 0;
    }

    if (mensaje->pais[0] == '\0' ||
        mensaje->pais[1] == '\0' ||
        mensaje->pais[2] != '\0') {
        return 0;
    }

    if (!((mensaje->pais[0] == 'E' &&
           mensaje->pais[1] == 'S') ||
          (mensaje->pais[0] == 'F' &&
           mensaje->pais[1] == 'R') ||
          (mensaje->pais[0] == 'I' &&
           mensaje->pais[1] == 'T'))) {
        return 0;
    }

    if (mensaje->tipo == MSG_INICIO &&
        mensaje->totalEncuestas <= 0) {
        return 0;
    }

    if (mensaje->tipo == MSG_ENCUESTA) {
        if (mensaje->idEncuesta <= 0) {
            return 0;
        }

        if (mensaje->sector < INDUSTRIA ||
            mensaje->sector > CONSTRUCCION) {
            return 0;
        }

        if (!(mensaje->sentimiento >= 0.0f &&
              mensaje->sentimiento <= 100.0f)) {
            return 0;
        }
    }

    return 1;
}

/* Funcion principal del hilo receptor POSIX */
void *hilo_receptor(void *argumento)
{
    ReceptorContexto *contexto =
        (ReceptorContexto *)argumento;

    Mensaje mensaje;
    int estado;

    if (contexto == NULL) {
        return NULL;
    }

    contexto->resultado = -1;

    if (contexto->fdPipe < 0 ||
        contexto->procesar == NULL) {
        fprintf(stderr,
            "Error: configuracion del receptor.\n");
        return NULL;
    }

    printf("Hilo receptor iniciado.\n");

    while (1) {
        estado = leerMensaje(
            contexto->fdPipe,
            &mensaje
        );

        if (estado == 0) {
            printf("Fin de lectura del pipe.\n");
            contexto->resultado = 0;
            break;
        }

        if (estado < 0) {
            break;
        }

        if (!validarMensaje(&mensaje)) {
            fprintf(stderr,
                "Error: mensaje recibido no valido.\n");
            break;
        }

        if (mensaje.tipo == MSG_INICIO) {
            printf("Agente %d conectado. Pais: %s\n",
                mensaje.idAgente,
                mensaje.pais);
        }

        if (mensaje.tipo == MSG_FIN) {
            printf("Agente %d finalizo su envio.\n",
                mensaje.idAgente);
        }

        /* Entrega el mensaje al modulo del servidor */
        if (contexto->procesar(
                &mensaje,
                contexto->datos) != 0) {
            fprintf(stderr,
                "Error al procesar mensaje.\n");
            break;
        }
    }

    printf("Hilo receptor finalizado.\n");

    return NULL;
}
