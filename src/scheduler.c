#include "scheduler.h"

#include <stdlib.h>

/* Dispatch genérico: cada implementacion tiene su propio destroy especifico,
 * aqui solo lo invocamos y liberamos el wrapper. */
void scheduler_destroy(Scheduler *s) {
    if (s == NULL) return;
    if (s->destroy != NULL) s->destroy(s);
    free(s);
}
