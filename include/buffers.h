#ifndef BUFFERS_H
#define BUFFERS_H

#include <pthread.h>
#include <semaphore.h>
#include "protocolo.h"
#include "calculosISE.h"

#define MAX_AGENTES 4

/* Marca en totalEncuestas de un MSG_FIN que genera el servidor, no el agente */
#define FIN_FORZADO -1

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
    int idAgente;
    AcumuladoPais acumulado;
    long procesadas;
    long descartadas;
    int completo;
    sem_t *terminados;
    pthread_t hilo;
} SlotAgente;

/* La tabla de slots solo la modifica el hilo receptor */
typedef struct {
    SlotAgente slots[MAX_AGENTES];
    int maxAgentes;
    int tamBuffer;
    int conectados;
    sem_t terminados;
} GestorBuffers;

int buffer_init(Buffer *b, int capacidad);
int buffer_put(Buffer *b, const Mensaje *m);
int buffer_get(Buffer *b, Mensaje *m);
void buffer_destroy(Buffer *b);

int gestor_init(GestorBuffers *g, int maxAgentes, int tamBuffer);

/*
 * Coincide con ProcesarMensaje de receptor.h; datos es un GestorBuffers*.
 * Retorna distinto de 0 solo en errores graves, porque el receptor se
 * detiene. Un agente de mas, o un segundo agente del mismo pais, se rechaza
 * con un aviso y se descartan sus mensajes.
 */
int gestor_procesar_mensaje(const Mensaje *mensaje, void *datos);

/* Cierra a la fuerza el buffer de cada agente conectado. Llamar con el receptor detenido */
int gestor_forzar_fin(GestorBuffers *g);

/* Espera a que terminen los hilos procesadores de los agentes conectados */
int gestor_esperar(GestorBuffers *g);

void gestor_destruir(GestorBuffers *g);

/* Hilo de un agente: extrae del buffer y acumula hasta recibir MSG_FIN */
void *hilo_procesador(void *argumento);

#endif
