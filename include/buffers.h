#ifndef BUFFERS_H
#define BUFFERS_H

#include <pthread.h>
#include <semaphore.h>
#include "protocolo.h"
#include "calculosISE.h"

#define MAX_AGENTES 4

/*
 * Buffer circular acotado. vacios cuenta los espacios libres, llenos los
 * registros disponibles y mutex protege los indices entrada y salida.
 */
typedef struct {
    Mensaje *registros;
    int capacidad;
    int entrada;
    int salida;
    sem_t vacios;
    sem_t llenos;
    sem_t mutex;
} Buffer;

/* Todo lo asociado a un agente conectado */
typedef struct {
    Buffer buffer;
    char pais[3];
    AcumuladoPais acumulado;
    long procesadas;
    long descartadas;
    pthread_t hilo;
} SlotAgente;

/* La tabla de slots solo la modifica el hilo receptor */
typedef struct {
    SlotAgente slots[MAX_AGENTES];
    int maxAgentes;
    int tamBuffer;
    int conectados;
} GestorBuffers;

int buffer_init(Buffer *b, int capacidad);
int buffer_put(Buffer *b, const Mensaje *m);
int buffer_get(Buffer *b, Mensaje *m);
void buffer_destroy(Buffer *b);

int gestor_init(GestorBuffers *g, int maxAgentes, int tamBuffer);

/* Coincide con ProcesarMensaje de receptor.h; datos es un GestorBuffers* */
int gestor_procesar_mensaje(const Mensaje *mensaje, void *datos);

/* Espera a que terminen los hilos procesadores de los agentes conectados */
int gestor_esperar(GestorBuffers *g);

void gestor_destruir(GestorBuffers *g);

/* Hilo de un agente: extrae del buffer y acumula hasta recibir MSG_FIN */
void *hilo_procesador(void *argumento);

#endif

