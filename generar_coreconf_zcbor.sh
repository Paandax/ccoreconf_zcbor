#!/bin/bash
# Script para generar código C desde CDDL de CORECONF usando zcbor

echo "=== Generando código C para CORECONF con zcbor ==="

# Directorio de salida
OUTPUT_DIR="./coreconf_zcbor_generated"
mkdir -p "$OUTPUT_DIR"

# Generar código de encode
echo "Generando funciones de encoding..."
python3 zcbor/zcbor/zcbor.py code \
    --cddl coreconf.cddl \
    --output-c "$OUTPUT_DIR/coreconf_encode.c" \
    --output-h "$OUTPUT_DIR/coreconf_encode.h" \
    --encode \
    -t coreconf-value \
    -t key-mapping

# Generar código de decode
echo "Generando funciones de decoding..."
python3 zcbor/zcbor/zcbor.py code \
    --cddl coreconf.cddl \
    --output-c "$OUTPUT_DIR/coreconf_decode.c" \
    --output-h "$OUTPUT_DIR/coreconf_decode.h" \
    --decode \
    -t coreconf-value \
    -t key-mapping

echo ""
echo "=== Código generado en $OUTPUT_DIR ==="
ls -lh "$OUTPUT_DIR"

echo ""
echo "Archivos generados:"
echo "  - coreconf_encode.h/c : Funciones de serialización"
echo "  - coreconf_decode.h/c : Funciones de deserialización"
echo ""
echo "Próximo paso: Integrar estos archivos con ccoreconf"
