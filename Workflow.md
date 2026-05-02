# Workflow Proyecto 1 S.O

# Plan de desarrollo iterativo

Cada iteración deja un sistema **compilable y ejecutable**.

---

## Iteración 1 — Esqueleto y CLI | `Jaison`

**Meta:** tener un binario que parsea argumentos, valida la configuración y la imprime. No hay hilos aún.

**Archivos a crear:**

- `Makefile`
- `include/types.h` — enums (`TruckState`, `CargoType`, `SchedAlgo`) y structs (`Truck`, `SimConfig`) completas desde el inicio. Aunque no se usen todos los campos todavía, definirlos evita cambios de interfaz después.
- `include/config.h`, `src/config.c` — `config_parse_args()`, `config_print()`, `config_validate()`.
- `src/main.c` — parsea, valida, imprime. Retorna 0.

**Criterios de aceptación:**

- `make` compila sin warnings con `Wall -Wextra -Wpedantic`.
- `./terminal --help` muestra ayuda completa.
- `./terminal --algorithm rr --trucks 5 --quantum 100` imprime la configuración parseada.
- Argumentos inválidos (ej. `-trucks -1`, `-algorithm foo`) producen mensaje de error claro y salida `!= 0`.

**Fuera de alcance:** hilos, semáforos, log, métricas.

---

## Iteración 2 — Logger thread-safe y módulo de estados | `Jaison`

**Meta:** logger operativo con mutex; helpers para registrar transiciones de estado. Aún sin hilos de simulación, pero testeable con un `main` que dispare N llamadas.

**Archivos a crear:**

- `include/logger.h`, `src/logger.c`
    - `logger_init(const char *path, bool verbose)`
    - `logger_event(int truck_id, TruckState state, const char *fmt, ...)` — thread-safe, timestamp automático
    - `logger_close()`
- Helper en `types.h`: `const char *truck_state_name(TruckState)`.
- En `main.c`: después de imprimir config, abrir logger, escribir un evento de arranque, cerrar.

**Decisiones clave:**

- Formato de línea de log: `[timestamp_ms] [truck_id] [STATE] mensaje`. Fácil de parsear luego.
- Mutex interno estático en `logger.c`. El logger es un singleton por diseño; es el único caso justificado.
- Si `-verbose`, cada evento también va a `stdout`.

**Criterios de aceptación:**

- Correr el binario genera archivo `terminal.log` con al menos el evento de inicio.
- `-verbose` duplica salida a `stdout`.
- Escrituras concurrentes (se puede simular en iteración siguiente) no se entrelazan.

---

## Iteración 3 — Muelles, camiones básicos y FIFO | `Jaison`

**Meta:** primer flujo end-to-end. N camiones esperan por M muelles, son atendidos en orden FIFO, escriben al log, terminan. Sin desalojo, sin prioridad.

**Archivos a crear:**

- `include/dock.h`, `src/dock.c` — wrapper sobre `sem_t`, inicializa con capacidad M.
- `include/queue.h`, `src/queue.c` — cola de `Truck*`, thread-safe, con `queue_push`, `queue_pop_fifo`, `queue_size`. Protegida por mutex interno + `pthread_cond_t` para esperar cuando está vacía.
- `include/scheduler.h`, `src/scheduler.c` — struct `Scheduler` (la interfaz Strategy).
- `src/scheduler_fifo.c` — `scheduler_fifo_create(const SimConfig*)` retorna un `Scheduler*` configurado.
- `include/truck.h`, `src/truck.c` — `truck_run()` (función de hilo): transiciona NEW → READY, se registra en la cola, espera su turno, hace RUNNING, simula trabajo con `usleep(burst)`, transiciona a TERMINATED.
- `include/simulation.h`, `src/simulation.c` — `simulation_run(const SimConfig*, Scheduler*)`:
    - Genera N camiones con bursts aleatorios y tipos de carga.
    - Crea hilos con `pthread_create`; cada hilo corre `truck_run`.
    - Lanza un "hilo dispatcher" que toma camiones de la cola y les libera un muelle (usando `sem_wait` dentro del camión + condición).
    - Al terminar, `pthread_join` a todos.
- `main.c` se actualiza: si config es válida, construye el scheduler según `cfg.algorithm` y llama `simulation_run`.

**Decisión de diseño importante — quién hace `sem_wait`:**

Hay dos formas equivalentes. Elegimos la **primera** por simplicidad:

1. **Camión hace `sem_wait` directo** sobre el semáforo de muelles. El "planificador" solo ordena la cola; el semáforo garantiza que nunca haya más de M camiones ejecutando.
2. Dispatcher centralizado que hace el `sem_wait` y despierta al camión por `cond`. Más flexible para preempción, pero más complejo.

Para FIFO la (1) es suficiente. Para RR necesitaremos un mecanismo de desalojo que se añade en la iteración 4 sin romper esta base.

**Criterios de aceptación:**

- `./terminal --trucks 10 --docks 3` ejecuta y termina sin colgarse.
- El log muestra transiciones NEW → READY → RUNNING → TERMINATED para cada camión.
- En ningún instante hay más de 3 camiones en estado RUNNING (verificable con `grep RUNNING terminal.log`).
- No hay mensajes entremezclados en el log.

---

## Iteración 4 — Round Robin con quantum | `Javier`

**Meta:** segundo algoritmo. Un camión solo puede usar el muelle por `quantum` milisegundos; si no termina, libera el muelle y vuelve a la cola con el burst restante.

**Archivos a crear:**

- `src/scheduler_rr.c` — `scheduler_rr_create(const SimConfig*)`. La estructura interna es igual que FIFO (cola) más un `int quantum_ms`. `preempts()` retorna `true`.

**Cambios necesarios:**

- `truck.c`: cuando el algoritmo preempta, el camión hace trabajo en trozos de `quantum` ms. Si al terminar el trozo aún le queda burst, registra evento "quantum expired", transiciona RUNNING → READY, hace `sem_post` del muelle y se re-encola. Cuando vuelve a ser seleccionado, hace otro trozo.
- La transición de vuelta a READY es lo que cuenta como **cambio de contexto**. Se incrementa un contador global en `metrics`..

**Criterios de aceptación:**

- `./terminal --algorithm rr --quantum 100 --trucks 10 --docks 3` termina.
- En el log aparecen eventos `quantum expired` y transiciones RUNNING → READY → RUNNING repetidas para camiones con burst largo.
- El contador de cambios de contexto es `> 0` para RR y exactamente `0` para FIFO (con los mismos camiones).
- Con `-quantum` muy grande, RR se comporta como FIFO (sanity check).

---

## Iteración 5 — Prioridad apropiativa | `Javier`

**Meta:** los camiones con carga perecedera (`CARGO_PERISHABLE`) saltan al frente de la cola. Funciona sobre FIFO o RR.

**Cambios necesarios:**

- `queue.c`: añadir `queue_push_priority(Truck*)` que inserta antes de camiones no perecederos.
- `scheduler_fifo.c` y `scheduler_rr.c`: si `cfg.priority_enabled`, usan `queue_push_priority`. Si no, `queue_push` normal.
- `truck.c`: al generar camiones, asignar `CARGO_PERISHABLE` con probabilidad `cfg.perishable_ratio`.
- Evento específico en el log: `priority boost` cuando un perecedero se encola.

**Decisión:** la prioridad solo ordena la **entrada** a la cola. No desaloja a un camión ya en ejecución (no hay "prioridad apropiativa pura" sobre un camión corriendo). Esto es coherente con el modelo RR donde la apropiación ya ocurre por quantum.

**Criterios de aceptación:**

- Con `-priority --perishable-ratio 0.5`, el log muestra que perecederos son atendidos antes que no perecederos que llegaron al mismo tiempo.
- Sin `-priority`, el comportamiento es FIFO puro (aunque haya perecederos).
- No hay starvation evidente en ejecuciones cortas (documentar si aparece en ejecuciones largas; mitigación con aging es opcional).

---

## Iteración 6 — Métricas, tabla comparativa y pulido | `Javier`

**Meta:** medición precisa, reporte formateado y documentación final.

**Archivos a crear / completar:**

- `include/metrics.h`, `src/metrics.c`
    - `metrics_record_arrival(int truck_id)`
    - `metrics_record_first_run(int truck_id)` — para calcular tiempo de espera inicial
    - `metrics_record_termination(int truck_id)`
    - `metrics_increment_context_switches()`
    - `metrics_print_summary(const SimConfig*, FILE*)` — imprime tabla
- Integración en `truck.c` en cada transición.
- `simulation.c` llama `metrics_print_summary(cfg, stdout)` al final.

**Definiciones operativas:**

- **Tiempo de espera** = tiempo en estado READY antes del primer RUNNING. (Definición estándar; en RR se puede sumar el tiempo total en READY incluyendo re-encolamientos; elegir una y documentarla.)
- **Turnaround** = `termination_ts - arrival_ts`.
- **Cambios de contexto** = número de veces que un camión hace RUNNING → READY (solo RR los genera).

**Criterios de aceptación:**

- `./terminal ...` siempre imprime la tabla de resumen al final.
- Tiempos coherentes: turnaround ≥ espera + burst para cada camión.
- `make clean && make` compila limpio. Valgrind / `fsanitize=thread` (opcional) no reporta errores graves.

---

## Progreso

- [x]  Iteración 1 — Esqueleto y CLI
- [x]  Iteración 2 — Logger + estados
- [x]  Iteración 3 — FIFO end-to-end
- [x]  Iteración 4 — Round Robin
- [x]  Iteración 5 — Prioridad
- [x]  Iteración 6 — Métricas y documentación

Marcar con `[x]` al completar y validar el checklist correspondiente.