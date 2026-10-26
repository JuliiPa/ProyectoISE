#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include "protocolo.h"

typedef struct {
    int idEncuesta;
    int sector;
    float sentimiento;
} Encuesta;

static void uso(const char *programa) {
    fprintf(stderr, "Uso: %s -n encuestas_XX.csv -p pipeNominal\n", programa);
}

static int sector_valido(int sector) {
    return sector >= INDUSTRIA && sector <= CONSTRUCCION;
}

static int pais_valido(const char *pais) {
    return strcmp(pais, "ES") == 0 || strcmp(pais, "FR") == 0 || strcmp(pais, "IT") == 0;
}

/* Lee los registros del CSV y los conserva en memoria antes del envio. */
static int cargar_csv(const char *ruta, Encuesta **salida, size_t *total, char pais[3]) {
    FILE *archivo = fopen(ruta, "r");
    char linea[512];
    Encuesta *datos = NULL;
    size_t cantidad = 0, capacidad = 0;
    unsigned long numero_linea = 1;

    if (!archivo) {
        perror("Error al abrir el CSV");
        return -1;
    }
    if (!fgets(linea, sizeof(linea), archivo)) {
        fprintf(stderr, "Error: CSV vacio o sin cabecera.\n");
        fclose(archivo);
        return -1;
    }
    if (!strchr(linea, '\n') && !feof(archivo)) {
        fprintf(stderr, "Error: cabecera demasiado larga.\n");
        fclose(archivo);
        return -1;
    }
    pais[0] = '\0';

    while (fgets(linea, sizeof(linea), archivo)) {
        int id, idPais, idSector, pos = 0;
        char codigo[3], nombreSector[32];
        float sentimiento;
        char *resto;
        numero_linea++;

        if (!strchr(linea, '\n') && !feof(archivo)) {
            fprintf(stderr, "Error: linea %lu demasiado larga.\n", numero_linea);
            goto error;
        }
        if (linea[0] == '\n' || linea[0] == '\r') {
            fprintf(stderr, "Error: linea %lu vacia.\n", numero_linea);
            goto error;
        }
        /* %n permite detectar caracteres sobrantes distintos del salto de linea. */
        if (sscanf(linea, " %d , %d , %2[^,] , %31[^,] , %d , %f %n",
                   &id, &idPais, codigo, nombreSector, &idSector,
                   &sentimiento, &pos) != 6) {
            fprintf(stderr, "Error: formato CSV invalido en linea %lu.\n", numero_linea);
            goto error;
        }
        resto = linea + pos;
        while (*resto == '\r' || *resto == '\n' || *resto == ' ' || *resto == '\t') resto++;
        if (*resto != '\0' || id <= 0 || idPais < 0 || !sector_valido(idSector) ||
            !(sentimiento >= 0.0f && sentimiento <= 100.0f) || !pais_valido(codigo)) {
            fprintf(stderr, "Error: datos invalidos en linea %lu.\n", numero_linea);
            goto error;
        }
        if (pais[0] == '\0') {
            strcpy(pais, codigo);
        } else if (strcmp(pais, codigo) != 0) {
            fprintf(stderr, "Error: el archivo mezcla paises (linea %lu).\n", numero_linea);
            goto error;
        }
        if (cantidad == capacidad) {
            size_t nueva = capacidad ? capacidad * 2 : 128;
            Encuesta *temporal;
            if (nueva < capacidad || nueva > SIZE_MAX / sizeof(*datos)) {
                fprintf(stderr, "Error: demasiadas encuestas.\n");
                goto error;
            }
            temporal = realloc(datos, nueva * sizeof(*datos));
            if (!temporal) {
                perror("Error al reservar memoria");
                goto error;
            }
            datos = temporal;
            capacidad = nueva;
        }
        datos[cantidad].idEncuesta = id;
        datos[cantidad].sector = idSector;
        datos[cantidad].sentimiento = sentimiento;
        cantidad++;
    }
    if (ferror(archivo)) {
        perror("Error al leer el CSV");
        goto error;
    }
    if (fclose(archivo) != 0) {
        perror("Error al cerrar CSV");
        free(datos);
        return -1;
    }
    if (cantidad == 0 || cantidad > INT_MAX) {
        fprintf(stderr, "Error: cantidad de encuestas invalida.\n");
        free(datos);
        return -1;
    }
    *salida = datos;
    *total = cantidad;
    return 0;

error:
    fclose(archivo);
    free(datos);
    return -1;
}

/* Una llamada write por mensaje mantiene atomicidad si sizeof(Mensaje) <= PIPE_BUF. */
static int enviar_mensaje(int fd, const Mensaje *mensaje) {
    ssize_t escritos;
    do {
        escritos = write(fd, mensaje, sizeof(*mensaje));
    } while (escritos < 0 && errno == EINTR);
    if (escritos != (ssize_t)sizeof(*mensaje)) {
        if (escritos >= 0) fprintf(stderr, "Error: escritura incompleta en el FIFO.\n");
        else perror("Error al enviar mensaje al FIFO");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[]) {
    const char *ruta = NULL, *nombre_fifo = NULL;
    Encuesta *encuestas = NULL;
    size_t cantidad = 0, i;
    char pais[3];
    int fd = -1, idAgente;
    Mensaje mensaje = {0};
    long pipe_buf;
    int resultado = EXIT_FAILURE;

    for (int j = 1; j < argc; j++) {
        if (strcmp(argv[j], "-n") == 0 && j + 1 < argc && !ruta) {
            ruta = argv[++j];
        } else if (strcmp(argv[j], "-p") == 0 && j + 1 < argc && !nombre_fifo) {
            nombre_fifo = argv[++j];
        } else {
            uso(argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (!ruta || !nombre_fifo) {
        uso(argv[0]);
        return EXIT_FAILURE;
    }
    if (cargar_csv(ruta, &encuestas, &cantidad, pais) != 0) return EXIT_FAILURE;

    idAgente = (int)getpid();
    signal(SIGPIPE, SIG_IGN);
    printf("Pais: %s\nPipe: %s\nLeidas %zu encuestas.\n", pais, nombre_fifo, cantidad);
    printf("Conectando al proceso principal...\n");
    fflush(stdout);
    fd = open(nombre_fifo, O_WRONLY); /* Espera hasta que exista un lector. */
    if (fd < 0) {
        perror("Error al abrir el pipe nominal");
        goto terminar;
    }
    pipe_buf = fpathconf(fd, _PC_PIPE_BUF);
    if (pipe_buf < 0 || (size_t)pipe_buf < sizeof(Mensaje)) {
        fprintf(stderr, "Error: el mensaje supera el limite de atomicidad del pipe.\n");
        goto terminar;
    }
    printf("Conectado.\nEnviando...\n");

    mensaje.tipo = MSG_INICIO;
    mensaje.idAgente = idAgente;
    strcpy(mensaje.pais, pais);
    mensaje.totalEncuestas = (int)cantidad;
    if (enviar_mensaje(fd, &mensaje) != 0) goto terminar;

    for (i = 0; i < cantidad; i++) {
        mensaje.tipo = MSG_ENCUESTA;
        mensaje.idEncuesta = encuestas[i].idEncuesta;
        mensaje.sector = encuestas[i].sector;
        mensaje.sentimiento = encuestas[i].sentimiento;
        if (enviar_mensaje(fd, &mensaje) != 0) goto terminar;
        if ((i + 1) % 500 == 0) printf("  %zu/%zu\n", i + 1, cantidad);
    }

    mensaje.tipo = MSG_FIN;
    if (enviar_mensaje(fd, &mensaje) != 0) goto terminar;
    printf("Enviadas %zu/%zu encuestas.\n", cantidad, cantidad);
    printf("Mensaje de finalizacion enviado.\nEnvio %s finalizado satisfactoriamente.\n", pais);
    resultado = EXIT_SUCCESS;

terminar:
    if (fd >= 0 && close(fd) != 0) {
        perror("Error al cerrar el FIFO");
        resultado = EXIT_FAILURE;
    }
    free(encuestas);
    return resultado;
}
