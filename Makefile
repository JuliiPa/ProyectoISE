# Compilador de archivos
CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -Iinclude
PTHREAD = -pthread

# Reglas que no representan archivos
.PHONY: all clean

# Compilar todos los programas
all: generadorCSV agentePais servidorISE

# Generar el ejecutable de encuestas
generadorCSV: src/generadorCSV.c
	$(CC) $(CFLAGS) -o $@ $<

# Compilar el agente de lectura
agentePais: src/agentePais.c include/protocolo.h
	$(CC) $(CFLAGS) -o $@ src/agentePais.c

# Compilar el servidor y sus modulos
servidorISE: src/servidorISE.c src/receptor.c src/buffers.c src/calculosISE.c include/protocolo.h include/receptor.h include/buffers.h include/calculosISE.h
	$(CC) $(CFLAGS) $(PTHREAD) -o $@ src/servidorISE.c src/receptor.c src/buffers.c src/calculosISE.c

# Eliminar ejecutables y archivos objeto
clean:
	rm -f generadorCSV agentePais servidorISE *.o
