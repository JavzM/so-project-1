#!/usr/bin/env bash
# compare.sh — Ejecuta FIFO y RR con la misma semilla y muestra las tablas
#              lado a lado para facilitar el analisis comparativo.
#
# Uso:
#   bash tests/compare.sh              # parametros por defecto
#   SEED=99 TRUCKS=20 bash tests/compare.sh
#
# Requiere que el binario ./terminal este compilado (make).

set -euo pipefail

SEED=${SEED:-42}
TRUCKS=${TRUCKS:-15}
DOCKS=${DOCKS:-3}
QUANTUM=${QUANTUM:-200}
BURST_MIN=${BURST_MIN:-500}
BURST_MAX=${BURST_MAX:-2000}
LOG_FIFO=$(mktemp /tmp/terminal_fifo_XXXXXX.log)
LOG_RR=$(mktemp   /tmp/terminal_rr_XXXXXX.log)

cleanup() { rm -f "$LOG_FIFO" "$LOG_RR"; }
trap cleanup EXIT

BINARY=./terminal
if [ ! -x "$BINARY" ]; then
    echo "Error: $BINARY no encontrado. Ejecuta 'make' primero." >&2
    exit 1
fi

echo "========================================================"
echo " Comparacion FIFO vs Round Robin"
echo " Parametros: seed=$SEED  trucks=$TRUCKS  docks=$DOCKS"
echo " Burst: [${BURST_MIN}ms .. ${BURST_MAX}ms]  Quantum RR: ${QUANTUM}ms"
echo "========================================================"
echo ""

echo "-------- FIFO ------------------------------------------"
"$BINARY" --algorithm fifo \
          --seed "$SEED" --trucks "$TRUCKS" --docks "$DOCKS" \
          --burst-min "$BURST_MIN" --burst-max "$BURST_MAX" \
          --log-file "$LOG_FIFO"

echo ""
echo "-------- Round Robin (quantum=${QUANTUM}ms) -------------"
"$BINARY" --algorithm rr --quantum "$QUANTUM" \
          --seed "$SEED" --trucks "$TRUCKS" --docks "$DOCKS" \
          --burst-min "$BURST_MIN" --burst-max "$BURST_MAX" \
          --log-file "$LOG_RR"

echo ""
echo "========================================================"
echo " Contexto: la misma semilla garantiza los mismos bursts"
echo " y el mismo orden de llegada en ambas corridas."
echo " FIFO: cambios de contexto = 0 (sin preemption)."
echo " RR:   cambios de contexto > 0 si burst > quantum."
echo "========================================================"
