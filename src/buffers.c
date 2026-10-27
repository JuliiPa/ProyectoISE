#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "buffers.h"

/* sem_wait puede ser interrumpido por una senal */
static int esperar(sem_t *s)
{
    while (sem_wait(s) == -1) {
        if (errno != EINTR)
            return -1;
    }
    return 0;
}

int buffer_init(Buffer *b, int capacidad)
{
    if (capacidad < 1)
        return -1;

    b->registros = malloc((size_t)capacidad * sizeof(Mensaje));
    if (b->registros == NULL)
        return -1;
    b->capacidad = capacidad;
    b->entrada = 0;
    b->salida = 0;

    if (sem_init(&b->vacios, 0, (unsigned int)capacidad) == -1)
        goto errVacios;
    if (sem_init(&b->llenos, 0, 0) == -1)
        goto errLlenos;
    if (sem_init(&b->mutex, 0, 1) == -1)
        goto errMutex;
    return 0;

errMutex:
    sem_destroy(&b->llenos);
errLlenos:
    sem_destroy(&b->vacios);
errVacios:
    free(b->registros);
    b->registros = NULL;
    return -1;
}

int buffer_put(Buffer *b, const Mensaje *m)
{
    if (esperar(&b->vacios) == -1)
        return -1;
    if (esperar(&b->mutex) == -1) {
        sem_post(&b->vacios);
        return -1;
    }

    b->registros[b->entrada] = *m;
    b->entrada = (b->entrada + 1) % b->capacidad;

    sem_post(&b->mutex);
    sem_post(&b->llenos);
    return 0;
}

int buffer_get(Buffer *b, Mensaje *m)
{
    if (esperar(&b->llenos) == -1)
        return -1;
    if (esperar(&b->mutex) == -1) {
        sem_post(&b->llenos);
        return -1;
    }

    *m = b->registros[b->salida];
    b->salida = (b->salida + 1) % b->capacidad;

    sem_post(&b->mutex);
    sem_post(&b->vacios);
    return 0;
}

void buffer_destroy(Buffer *b)
{
    sem_destroy(&b->vacios);
    sem_destroy(&b->llenos);
    sem_destroy(&b->mutex);
    free(b->registros);
    b->registros = NULL;
}

int gestor_init(GestorBuffers *g, int maxAgentes, int tamBuffer)
{
    if (maxAgentes < 1 || maxAgentes > MAX_AGENTES || tamBuffer < 1)
        return -1;
    if (sem_init(&g->terminados, 0, 0) == -1)
        return -1;
    g->maxAgentes = maxAgentes;
    g->tamBuffer = tamBuffer;
    g->conectados = 0;
    return 0;
}

/* Retorna el slot del pais o -1 si no esta conectado */
static int buscar_slot(const GestorBuffers *g, const char pais[3])
{
    int i;
    for (i = 0; i < g->conectados; i++) {
        if (strncmp(g->slots[i].pais, pais, 3) == 0)
            return i;
    }
    return -1;
}

static int conectar_agente(GestorBuffers *g, const Mensaje *m)
{
    SlotAgente *s;

    /* Dos agentes del mismo pais mezclarian sus datos en un solo buffer */
    if (buscar_slot(g, m->pais) != -1) {
        fprintf(stderr, "Error: el agente %.2s ya esta conectado\n", m->pais);
        return -1;
    }

    /* Un agente de mas no es fatal: se avisa y se descartan sus mensajes */
    if (g->conectados >= g->maxAgentes) {
        fprintf(stderr, "Agente %.2s rechazado: ya hay %d agentes\n",
                m->pais, g->maxAgentes);
        return 0;
    }

    s = &g->slots[g->conectados];
    memcpy(s->pais, m->pais, 3);
    acum_init(&s->acumulado);
    s->procesadas = 0;
    s->descartadas = 0;
    s->completo = 0;
    s->terminados = &g->terminados;

    if (buffer_init(&s->buffer, g->tamBuffer) == -1)
        return -1;
    if (pthread_create(&s->hilo, NULL, hilo_procesador, s) != 0) {
        buffer_destroy(&s->buffer);
        return -1;
    }

    g->conectados++;
    return 0;
}

int gestor_procesar_mensaje(const Mensaje *mensaje, void *datos)
{
    GestorBuffers *g = datos;
    int i;

    if (mensaje->tipo == MSG_INICIO)
        return conectar_agente(g, mensaje);

    if (mensaje->tipo != MSG_ENCUESTA && mensaje->tipo != MSG_FIN)
        return -1;

    i = buscar_slot(g, mensaje->pais);
    if (i == -1)
        return 0;

    /* Bloquea si el buffer esta lleno hasta que el procesador libere espacio */
    return buffer_put(&g->slots[i].buffer, mensaje);
}

void *hilo_procesador(void *argumento)
{
    SlotAgente *s = argumento;
    Mensaje m;

    while (buffer_get(&s->buffer, &m) == 0) {
        /* MSG_FIN llega despues de todas las encuestas del agente */
        if (m.tipo == MSG_FIN) {
            s->completo = (m.totalEncuestas != FIN_FORZADO);
            if (s->completo)
                printf("Agente %.2s: se recibieron todos los datos\n", s->pais);
            else
                printf("Agente %.2s: tiempo agotado, datos incompletos\n", s->pais);
            break;
        }
        if (acum_agregar(&s->acumulado, m.sector, m.sentimiento) == 0)
            s->procesadas++;
        else
            s->descartadas++;
    }

    sem_post(s->terminados);
    return NULL;
}

int gestor_forzar_fin(GestorBuffers *g)
{
    int i;
    int resultado = 0;
    Mensaje m;

    for (i = 0; i < g->conectados; i++) {
        memset(&m, 0, sizeof(m));
        m.tipo = MSG_FIN;
        memcpy(m.pais, g->slots[i].pais, 3);
        m.totalEncuestas = FIN_FORZADO;
        if (buffer_put(&g->slots[i].buffer, &m) == -1)
            resultado = -1;
    }
    return resultado;
}

int gestor_esperar(GestorBuffers *g)
{
    int i;
    int resultado = 0;

    for (i = 0; i < g->conectados; i++) {
        if (pthread_join(g->slots[i].hilo, NULL) != 0)
            resultado = -1;
    }
    return resultado;
}

void gestor_destruir(GestorBuffers *g)
{
    int i;
    for (i = 0; i < g->conectados; i++)
        buffer_destroy(&g->slots[i].buffer);
    sem_destroy(&g->terminados);
}
