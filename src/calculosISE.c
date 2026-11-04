#include <stddef.h>
#include "calculosISE.h"

/* Pesos en el orden de los ID de sector (0 a 4) */
static const double PESOS[NUM_SECTORES] = { 0.40, 0.30, 0.20, 0.05, 0.05 };

void acum_init(AcumuladoPais *a)
{
    int i;
    for (i = 0; i < NUM_SECTORES; i++) {
        a->suma[i] = 0.0;
        a->cuenta[i] = 0;
    }
}

void acum_sumar(AcumuladoPais *destino, const AcumuladoPais *origen)
{
    int i;
    for (i = 0; i < NUM_SECTORES; i++) {
        destino->suma[i] += origen->suma[i];
        destino->cuenta[i] += origen->cuenta[i];
    }
}

int acum_agregar(AcumuladoPais *a, int sector, double sentimiento)
{
    if (sector < 0 || sector >= NUM_SECTORES)
        return -1;
    if (sentimiento < 0.0 || sentimiento > 100.0)
        return -1;
    a->suma[sector] += sentimiento;
    a->cuenta[sector]++;
    return 0;
}

int ise_pais(const AcumuladoPais *a, double *ise, double prom[NUM_SECTORES])
{
    int i;
    double acum = 0.0;
    double pesoUsado = 0.0;

    for (i = 0; i < NUM_SECTORES; i++) {
        double p = 0.0;
        if (a->cuenta[i] > 0) {
            p = a->suma[i] / (double)a->cuenta[i];
            acum += p * PESOS[i];
            pesoUsado += PESOS[i];
        }
        if (prom != NULL)
            prom[i] = p;
    }

    if (pesoUsado <= 0.0)
        return -1;

    *ise = acum / pesoUsado;
    return 0;
}

int ise_global(const double *isePaises, int n, double *ise)
{
    int i;
    double suma = 0.0;

    if (n <= 0)
        return -1;
    for (i = 0; i < n; i++)
        suma += isePaises[i];
    *ise = suma / (double)n;
    return 0;
}

const char *recomendacion(double ise)
{
    if (ise > 55.0)
        return "Vender";
    if (ise < 45.0)
        return "Comprar";
    return "Neutro";
}
