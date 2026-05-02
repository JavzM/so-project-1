# Sistema de Gestión de una Terminal de Carga Automatizada

**Curso:** EIF 212 Sistemas Operativos — I Ciclo 2026

Simulador de una terminal logística escrito en C (POSIX) donde múltiples camiones (hilos `pthread`) compiten por un número limitado de muelles de carga (controlados por semáforo). El sistema implementa dos algoritmos de planificación intercambiables — FIFO y Round Robin — con soporte opcional para prioridad a cargas perecederas.

- Javier Martinez Blanco
- Jaison Mora Viquez
---
> **Nota:**  Revisar `Workflow.md`
---

## Compilación

Requisitos: Linux, `gcc` (C11), `make`, `libpthread`.

```bash
make
```

El binario se genera como `./terminal`. Para borrar artefactos:

```bash
make clean
```

---

## Ejecución

Argumentos completos (también disponibles con `./terminal --help`):

| Argumento | Descripción | Default |
|---|---|---|
| `--algorithm <fifo\|rr>` `-a` | Algoritmo de planificación | `fifo` |
| `--trucks <N>` `-t` | Número de camiones a simular | `10` |
| `--docks <M>` `-d` | Número de muelles disponibles | `3` |
| `--quantum <ms>` `-q` | Quantum para RR (ignorado en FIFO) | `200` |
| `--priority` `-p` | Activa prioridad para perecederos | off |
| `--burst-min <ms>` | Mínimo de trabajo por camión | `500` |
| `--burst-max <ms>` | Máximo de trabajo por camión | `2000` |
| `--arrival-max <ms>` | Tiempo máximo entre llegadas | `300` |
| `--perishable-ratio <0..1>` | Proporción de carga perecedera | `0.3` |
| `--log-file <ruta>` `-l` | Archivo de log | `terminal.log` |
| `--seed <N>` `-s` | Semilla aleatoria | `time(NULL)` |
| `--verbose` `-v` | Eventos también a stdout | off |

### Ejemplos de uso

```bash
# Corrida básica (FIFO por defecto, 10 camiones, 3 muelles)
./terminal

# Round Robin con quantum de 150 ms
./terminal --algorithm rr --quantum 150 --trucks 20 --docks 4

# Con prioridad para perecederos
./terminal --algorithm rr --priority --perishable-ratio 0.4

# Comparación reproducible (misma semilla, distintos algoritmos)
./terminal --algorithm fifo --seed 42 --trucks 15
./terminal --algorithm rr   --seed 42 --trucks 15 --quantum 200

# O usando el script de comparación:
make compare
```

---

## Salida real del programa

### Corrida FIFO — seed=42, 15 camiones, 3 muelles

```
===== Resumen de la simulacion =====
Algoritmo: FIFO (prioridad: off)
Camiones: 15   Muelles: 3   Quantum: N/A
-------------------------------------
Tiempo espera promedio: 1946.07 ms
Turnaround promedio:    3205.27 ms
Cambios de contexto:    0
Camiones completados:   15/15
=====================================
```

### Corrida Round Robin — seed=42, 15 camiones, 3 muelles, quantum=200ms

```
===== Resumen de la simulacion =====
Algoritmo: ROUND ROBIN (prioridad: off)
Camiones: 15   Muelles: 3   Quantum: 200ms
-------------------------------------
Tiempo espera promedio: 1943.27 ms
Turnaround promedio:    3203.67 ms
Cambios de contexto:    86
Camiones completados:   15/15
=====================================
```

### Corrida con prioridad — seed=7, 10 camiones, 40% perecederos

```
===== Resumen de la simulacion =====
Algoritmo: FIFO (prioridad: on)
Camiones: 10   Muelles: 3   Quantum: N/A
-------------------------------------
Tiempo espera promedio: 460.30 ms
Turnaround promedio:    1230.40 ms
Cambios de contexto:    0
Camiones completados:   10/10
=====================================
```

### Formato del archivo de log (`terminal.log`)

```
[1164051] [truck=0] [NEW]        creado burst=345ms cargo=GENERAL
[1164051] [truck=0] [READY]      en cola de listos
[1164052] [truck=0] [BLOCKED]    esperando muelle
[1164052] [truck=0] [RUNNING]    ocupando muelle, burst=345ms
[1164152] [truck=0] [RUNNING]    quantum expirado, remaining=245ms
[1164152] [truck=0] [READY]      re-encolado tras quantum
[1164152] [truck=0] [BLOCKED]    esperando muelle (re-encola)
[1164152] [truck=0] [RUNNING]    reanudando muelle, remaining=245ms
[1164220] [truck=0] [TERMINATED] carga completada turnaround=169ms
```

El timestamp es tiempo monotónico en milisegundos (`CLOCK_MONOTONIC`).

---

## Tabla comparativa

Para reproducir la comparación del informe con datos idénticos:

```bash
make compare
# o equivalentemente:
SEED=42 TRUCKS=15 bash tests/compare.sh
```

Para cambiar los parámetros:

```bash
SEED=99 TRUCKS=20 QUANTUM=150 bash tests/compare.sh
```

---

## Estructura del proyecto

```
.
├── README.md       # este archivo
├── Makefile
├── include/               # headers públicos de cada módulo
│   ├── types.h           # enums y structs base
│   ├── config.h          # parseo CLI
│   ├── logger.h          # log thread-safe
│   ├── queue.h          # cola FIFO + prioridad
│   ├── dock.h             # semáforo de muelles
│   ├── scheduler.h   # interfaz Strategy + factories
│   ├── truck.h            # ciclo de vida del hilo
│   ├── metrics.h        # registro de tiempos y resumen
│   └── simulation.h  # orquestación
├── src/                       # implementación
│   ├── main.c
│   ├── config.c
│   ├── logger.c
│   ├── queue.c
│   ├── dock.c
│   ├── scheduler.c
│   ├── scheduler_fifo.c
│   ├── scheduler_rr.c
│   ├── truck.c
│   ├── metrics.c
│   └── simulation.c
└── tests/
    └── compare.sh        # comparación FIFO vs RR misma semilla
```

---
## Como evitamos el deadlock

### El problema 
Tres recursos compartidos en el sistema:
- **Muelles** (semáforo)
- **Cola de listos** (mutex)
- **Log** (mutex)

### La solución
Le pusimos niveles a los recursos:

```
muelles (nivel 1)  →  cola (nivel 2)  →  log (nivel 3)
```
>Aplicamos prevención por ordenamiento total de recursos: muelles → cola → log, en ese orden y nunca al revés. Esto rompe la condición de espera circular de Coffman
> - <https://1984.lsi.us.es/wiki-ssoo/index.php/Condiciones_para_el_interbloqueo_y_estrategias_de_resoluci%C3%B3n>

En `truck.c`:
1. Primero `sem_wait(muelle)` (nivel 1)
2. Mientras tiene el muelle, puede llamar `logger_event()` (nivel 3) — sube de nivel
3. Suelta el log automáticamente al salir de `logger_event`
4. Al final: `sem_post(muelle)` libera nivel 1

Y nunca:
- El logger pide otro recurso mientras tiene su mutex.
- La cola llama al logger mientras tiene su mutex.
