#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "buffers.h"
#include "calculosISE.h"
#include "receptor.h"

#ifndef TIEMPO_MAX
#define TIEMPO_MAX 60
#endif

static const char *NOMBRES_SECTOR[NUM_SECTORES] = {
    "Industria", "Servicios", "Consumidores", "Comercio", "Construccion"
};

static void uso(const char *programa)
{
    fprintf(stderr, "Uso: %s -b <tamB> -p <PipeN> -a <agentes>\n", programa);
}

/* Retorna el entero positivo del texto o -1 si no es valido */
static int leerEntero(const char *texto)
{
    char *fin;
    long valor;

    errno = 0;
    valor = strtol(texto, &fin, 10);
    if (errno != 0 || fin == texto || *fin != '\0' || valor < 1 || valor > 1000000)
        return -1;
    return (int)valor;
}

static int leerArgumentos(int argc, char *argv[], int *tamB,
                          const char **pipe, int *agentes)
{
    int opcion;

    *tamB = -1;
    *pipe = NULL;
    *agentes = -1;

    while ((opcion = getopt(argc, argv, "b:p:a:")) != -1) {
        switch (opcion) {
        case 'b':
            *tamB = leerEntero(optarg);
            break;
        case 'p':
            *pipe = optarg;
            break;
        case 'a':
            *agentes = leerEntero(optarg);
            break;
        default:
            return -1;
        }
    }

    if (optind != argc || *tamB < 1 || *pipe == NULL ||
        *agentes < 1 || *agentes > MAX_AGENTES)
        return -1;
    return 0;
}

static int crearPipe(const char *ruta)
{
    struct stat info;

    if (mkfifo(ruta, 0666) == 0)
        return 0;
    /* Un FIFO que quedo de una ejecucion anterior sirve igual */
    if (errno == EEXIST && stat(ruta, &info) == 0 && S_ISFIFO(info.st_mode))
        return 0;

    perror("Error al crear el pipe");
    return -1;
}

/*
 * Abre el pipe sin bloquearse esperando al primer agente y sin recibir EOF
 * cada vez que un agente cierra: primero la lectura en modo no bloqueante,
 * luego un escritor propio que mantiene el pipe abierto, y por ultimo la
 * lectura vuelve a modo bloqueante.
 */
static int abrirPipe(const char *ruta, int *fdLectura, int *fdEscritura)
{
    int flags;

    *fdLectura = open(ruta, O_RDONLY | O_NONBLOCK);
    if (*fdLectura == -1) {
        perror("Error al abrir el pipe para lectura");
        return -1;
    }

    *fdEscritura = open(ruta, O_WRONLY);
    if (*fdEscritura == -1) {
        perror("Error al abrir el pipe para escritura");
        close(*fdLectura);
        return -1;
    }

    flags = fcntl(*fdLectura, F_GETFL);
    if (flags == -1 || fcntl(*fdLectura, F_SETFL, flags & ~O_NONBLOCK) == -1) {
        perror("Error al configurar el pipe");
        close(*fdLectura);
        close(*fdEscritura);
        return -1;
    }

    return 0;
}

/* Retorna 1 si todos los agentes terminaron antes del limite, 0 si no */
static int esperarAgentes(GestorBuffers *g)
{
    struct timespec limite;
    int i;

    if (clock_gettime(CLOCK_REALTIME, &limite) == -1) {
        perror("Error al leer el reloj");
        return 0;
    }
    limite.tv_sec += TIEMPO_MAX;

    for (i = 0; i < g->maxAgentes; i++) {
        while (sem_timedwait(&g->terminados, &limite) == -1) {
            if (errno == EINTR)
                continue;
            return 0;
        }
    }
    return 1;
}

/* Retorna 1 si el pais del slot i aparece por primera vez en la tabla */
static int primerAgenteDelPais(const GestorBuffers *g, int i)
{
    int j;
    for (j = 0; j < i; j++) {
        if (strncmp(g->slots[j].pais, g->slots[i].pais, 3) == 0)
            return 0;
    }
    return 1;
}

/* Los agentes de un mismo pais se juntan en un solo ISE */
static void imprimirReporte(const GestorBuffers *g)
{
    double isePaises[MAX_AGENTES];
    double ise, global;
    double prom[NUM_SECTORES];
    int n = 0;
    int i, j, k;

    printf("\n===== Resumen ISE =====\n");

    for (i = 0; i < g->conectados; i++) {
        AcumuladoPais total;
        long procesadas = 0;
        long descartadas = 0;
        int agentes = 0;
        int completo = 1;

        if (!primerAgenteDelPais(g, i))
            continue;

        acum_init(&total);
        for (k = i; k < g->conectados; k++) {
            const SlotAgente *s = &g->slots[k];
            if (strncmp(s->pais, g->slots[i].pais, 3) != 0)
                continue;
            acum_sumar(&total, &s->acumulado);
            procesadas += s->procesadas;
            descartadas += s->descartadas;
            if (!s->completo)
                completo = 0;
            agentes++;
        }

        printf("\nPais %.2s (%d %s, %s)\n", g->slots[i].pais, agentes,
               agentes == 1 ? "agente" : "agentes",
               completo ? "completo" : "incompleto por tiempo");

        if (ise_pais(&total, &ise, prom) == -1) {
            printf("  Sin encuestas\n");
            continue;
        }

        for (j = 0; j < NUM_SECTORES; j++) {
            if (total.cuenta[j] > 0)
                printf("  %-13s %6.2f (%ld encuestas)\n", NOMBRES_SECTOR[j],
                       prom[j], total.cuenta[j]);
            else
                printf("  %-13s sin datos\n", NOMBRES_SECTOR[j]);
        }
        printf("  Procesadas: %ld, descartadas: %ld\n", procesadas, descartadas);
        printf("  ISE: %.4f -> %s\n", ise, recomendacion(ise));
        isePaises[n++] = ise;
    }

    if (ise_global(isePaises, n, &global) == -1)
        printf("\nNo hay datos para calcular el ISE global\n");
    else
        printf("\nISE global: %.4f -> %s\n", global, recomendacion(global));
}

int main(int argc, char *argv[])
{
    int tamB, agentes;
    int fdLectura, fdEscritura;
    int terminaron;
    const char *ruta;
    GestorBuffers gestor;
    ReceptorContexto contexto;
    pthread_t hiloReceptor;

    if (leerArgumentos(argc, argv, &tamB, &ruta, &agentes) == -1) {
        uso(argv[0]);
        return 1;
    }

    if (gestor_init(&gestor, agentes, tamB) == -1) {
        fprintf(stderr, "Error al inicializar los buffers\n");
        return 1;
    }
    if (crearPipe(ruta) == -1) {
        gestor_destruir(&gestor);
        return 1;
    }
    if (abrirPipe(ruta, &fdLectura, &fdEscritura) == -1) {
        unlink(ruta);
        gestor_destruir(&gestor);
        return 1;
    }

    contexto.fdPipe = fdLectura;
    contexto.procesar = gestor_procesar_mensaje;
    contexto.datos = &gestor;
    contexto.resultado = 0;

    if (pthread_create(&hiloReceptor, NULL, hilo_receptor, &contexto) != 0) {
        fprintf(stderr, "Error al crear el hilo receptor\n");
        close(fdLectura);
        close(fdEscritura);
        unlink(ruta);
        gestor_destruir(&gestor);
        return 1;
    }

    printf("Servidor ISE listo en %s: %d agentes, buffer de %d, maximo %d s\n",
           ruta, agentes, tamB, TIEMPO_MAX);

    terminaron = esperarAgentes(&gestor);

    /* El receptor esta bloqueado en read(), que es punto de cancelacion */
    pthread_cancel(hiloReceptor);
    pthread_join(hiloReceptor, NULL);

    if (!terminaron) {
        printf("\nTiempo agotado: se cierran los agentes que no terminaron\n");
        gestor_forzar_fin(&gestor);
    }
    gestor_esperar(&gestor);

    imprimirReporte(&gestor);

    close(fdLectura);
    close(fdEscritura);
    unlink(ruta);
    gestor_destruir(&gestor);
    return 0;
}
