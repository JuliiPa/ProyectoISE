#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <locale.h>

#define SECTORES 5
#define MIN_SECTOR 2

typedef struct {
    const char *codigo;
    int idPais;
    int maximoTotal;
    int maximoSector[SECTORES];
} Pais;

static const Pais PAISES[] = {
    {"ES", 0, 6580,  {1760, 1430, 2020, 1080, 290}},
    {"FR", 1, 36640, {3850, 4240, 1670, 2860, 24020}},
    {"IT", 2, 13100, {5440, 3100, 2000, 1560, 1000}}
};

static const char *NOMBRES[SECTORES] = {
    "Industria", "Servicios", "Consumidores", "Comercio", "Construccion"
};

static const Pais *buscarPais(const char *codigo) {
    size_t i;
    for (i = 0; i < sizeof(PAISES) / sizeof(PAISES[0]); i++) {
        if (strcmp(codigo, PAISES[i].codigo) == 0) return &PAISES[i];
    }
    return NULL;
}

static int leerCantidad(const char *texto, int *cantidad) {
    char *fin;
    long valor;
    errno = 0;
    valor = strtol(texto, &fin, 10);
    if (errno != 0 || texto == fin || *fin != '\0' || valor < 1 || valor > INT_MAX) {
        return 0;
    }
    *cantidad = (int)valor;
    return 1;
}

int main(int argc, char *argv[]) {
    const char *codigo = NULL;
    const Pais *pais;
    int cantidad = 0, vistoCantidad = 0;
    int limite[SECTORES], cuenta[SECTORES];
    int restantes, disponibles, elegido, i, j, numeroEncuesta;
    int capacidadTotal = 0, maxPorcentaje;
    char archivo[64];
    FILE *salida;

    setlocale(LC_NUMERIC, "C");

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) {
            if (codigo != NULL || i + 1 >= argc) {
                fprintf(stderr, "Error: parametro -p repetido o sin valor.\n");
                return EXIT_FAILURE;
            }
            codigo = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0) {
            if (vistoCantidad || i + 1 >= argc || !leerCantidad(argv[i + 1], &cantidad)) {
                fprintf(stderr, "Error: parametro -c incorrecto.\n");
                return EXIT_FAILURE;
            }
            vistoCantidad = 1;
            i++;
        } else {
            fprintf(stderr, "Error: argumento desconocido: %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    pais = (codigo != NULL) ? buscarPais(codigo) : NULL;
    if (pais == NULL || !vistoCantidad) {
        fprintf(stderr, "Uso: ./generadorCSV -p ES|FR|IT -c cantidad\n");
        return EXIT_FAILURE;
    }
    if (cantidad < SECTORES * MIN_SECTOR || cantidad > pais->maximoTotal) {
        fprintf(stderr, "Error: cantidad fuera de rango (minimo %d, maximo %d para %s).\n",
                SECTORES * MIN_SECTOR, pais->maximoTotal, codigo);
        return EXIT_FAILURE;
    }

    maxPorcentaje = (cantidad * 3LL) / 10;
    for (i = 0; i < SECTORES; i++) {
        limite[i] = pais->maximoSector[i] < maxPorcentaje ? pais->maximoSector[i] : maxPorcentaje;
        if (limite[i] < MIN_SECTOR) {
            fprintf(stderr, "Error: no se puede cumplir el minimo de encuestas por sector.\n");
            return EXIT_FAILURE;
        }
        capacidadTotal += limite[i];
        cuenta[i] = MIN_SECTOR;
    }
    if (capacidadTotal < cantidad) {
        fprintf(stderr, "Error: %d encuestas no caben dentro de los limites sectoriales de %s (capacidad %d).\n",
                cantidad, codigo, capacidadTotal);
        return EXIT_FAILURE;
    }

    srand((unsigned int)time(NULL));
    restantes = cantidad - SECTORES * MIN_SECTOR;
    while (restantes > 0) {
        disponibles = 0;
        for (i = 0; i < SECTORES; i++) {
            if (cuenta[i] < limite[i]) disponibles++;
        }
        elegido = rand() % disponibles;
        for (i = 0; i < SECTORES; i++) {
            if (cuenta[i] < limite[i]) {
                if (elegido == 0) {
                    cuenta[i]++;
                    break;
                }
                elegido--;
            }
        }
        restantes--;
    }

    snprintf(archivo, sizeof(archivo), "encuestas_%s.csv", codigo);
    salida = fopen(archivo, "w");
    if (salida == NULL) {
        perror("Error al crear el CSV");
        return EXIT_FAILURE;
    }
    if (fprintf(salida, "ID_Encuesta,ID_Pais,Pais,Sector,ID_Sector,Sentimiento\n") < 0) {
        perror("Error al escribir cabecera");
        fclose(salida);
        remove(archivo);
        return EXIT_FAILURE;
    }

    numeroEncuesta = 1;
    for (i = 0; i < SECTORES; i++) {
        for (j = 0; j < cuenta[i]; j++) {
            double sentimiento = (rand() % 10001) / 100.0;
            if (fprintf(salida, "%d,%d,%s,%s,%d,%.2f\n", numeroEncuesta,
                        pais->idPais, codigo, NOMBRES[i], i, sentimiento) < 0) {
                perror("Error al escribir encuesta");
                fclose(salida);
                remove(archivo);
                return EXIT_FAILURE;
            }
            numeroEncuesta++;
        }
    }
    if (fclose(salida) != 0) {
        perror("Error al cerrar el CSV");
        remove(archivo);
        return EXIT_FAILURE;
    }

    printf("Generando fichero %s para pais %s con %d encuestas\n", archivo, codigo, cantidad);
    printf("Encuestas por sector:\n");
    for (i = 0; i < SECTORES; i++) {
        printf("  %-14s: %d/%d encuestas\n", NOMBRES[i], cuenta[i], cantidad);
    }
    printf("Total encuestas: %d\nFichero CSV generado correctamente.\n", cantidad);
    return EXIT_SUCCESS;
}
