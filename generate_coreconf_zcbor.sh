#!/bin/bash
# Script to generate C code from CORECONF CDDL using zcbor

echo "=== Generating CORECONF C code with zcbor ==="

# Output directory
OUTPUT_DIR="./coreconf_zcbor_generated"
mkdir -p "$OUTPUT_DIR"

# Generate encode code
echo "Generating encoding functions..."
python3 zcbor/zcbor/zcbor.py code \
    --cddl coreconf.cddl \
    --output-c "$OUTPUT_DIR/coreconf_encode.c" \
    --output-h "$OUTPUT_DIR/coreconf_encode.h" \
    --encode \
    -t coreconf-value \
    -t key-mapping

# Generate decode code
echo "Generating decoding functions..."
python3 zcbor/zcbor/zcbor.py code \
    --cddl coreconf.cddl \
    --output-c "$OUTPUT_DIR/coreconf_decode.c" \
    --output-h "$OUTPUT_DIR/coreconf_decode.h" \
    --decode \
    -t coreconf-value \
    -t key-mapping

echo ""
echo "=== Code generated in $OUTPUT_DIR ==="
ls -lh "$OUTPUT_DIR"

echo ""
echo "Generated files:"
echo "  - coreconf_encode.h/c : Serialization functions"
echo "  - coreconf_decode.h/c : Deserialization functions"
echo ""
echo "Next step: Integrate these files with ccoreconf"
