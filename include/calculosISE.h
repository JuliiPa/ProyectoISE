#ifndef CALCULOSISE_H
#define CALCULOSISE_H

#include "protocolo.h"

/* Suma de sentimientos y cantidad de encuestas por sector */
typedef struct {
    double suma[NUM_SECTORES];
    long cuenta[NUM_SECTORES];
} AcumuladoPais;

void acum_init(AcumuladoPais *a);

/* Suma los datos de origen en destino (juntar agentes de un mismo pais) */
void acum_sumar(AcumuladoPais *destino, const AcumuladoPais *origen);

/* Retorna -1 si el sector o el sentimiento estan fuera de rango */
int acum_agregar(AcumuladoPais *a, int sector, double sentimiento);

/*
 * Un sector sin encuestas se omite y los pesos restantes se renormalizan
 * (evita dividir por cero). Retorna -1 si ningun sector tiene datos.
 * prom puede ser NULL.
 */
int ise_pais(const AcumuladoPais *a, double *ise, double prom[NUM_SECTORES]);

int ise_global(const double *isePaises, int n, double *ise);

/* Vender > 55, Neutro entre 45 y 55, Comprar < 45 */
const char *recomendacion(double ise);

#endif
