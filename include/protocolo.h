#ifndef PROTOCOLO_H
#define PROTOCOLO_H

/* Tipos de mensajes */
#define MSG_INICIO 1
#define MSG_ENCUESTA 2
#define MSG_FIN 3

/* Identificadores de sectores */
#define INDUSTRIA 0
#define SERVICIOS 1
#define CONSUMIDORES 2
#define COMERCIO 3
#define CONSTRUCCION 4

/* Cantidad de sectores economicos */
#define NUM_SECTORES 5

/* Estructura para la comunicacion entre agentes y servidor */
typedef struct {
    int tipo;
    int idAgente;
    char pais[3];
    int idEncuesta;
    int sector;
    float sentimiento;
    int totalEncuestas;
} Mensaje;

#endif /* PROTOCOLO_H */
