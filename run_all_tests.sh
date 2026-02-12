#!/bin/bash
# run_all_tests.sh - Ejecuta todos los tests de la migración zcbor

set -e  # Salir si algún test falla

echo "╔══════════════════════════════════════════════════════════╗"
echo "║    EJECUTANDO TODOS LOS TESTS - MIGRACIÓN ZCBOR         ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""

# Contador de tests
TOTAL=0
PASSED=0

# Test 1: Basic Migration
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🧪 TEST 1/3: Basic Migration (test_migration)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if ./examples/test_migration; then
    PASSED=$((PASSED + 1))
    echo "✅ test_migration: PASSED"
else
    echo "❌ test_migration: FAILED"
fi
TOTAL=$((TOTAL + 1))
echo ""

# Test 2: FETCH Operation
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🧪 TEST 2/3: FETCH Operation (test_fetch_simple)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if ./examples/test_fetch_simple; then
    PASSED=$((PASSED + 1))
    echo "✅ test_fetch_simple: PASSED"
else
    echo "❌ test_fetch_simple: FAILED"
fi
TOTAL=$((TOTAL + 1))
echo ""

# Test 3: Exhaustive Suite
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🧪 TEST 3/3: Exhaustive Suite (test_exhaustive)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if ./examples/test_exhaustive; then
    PASSED=$((PASSED + 1))
    echo "✅ test_exhaustive: PASSED"
else
    echo "❌ test_exhaustive: FAILED"
fi
TOTAL=$((TOTAL + 1))
echo ""

# Resumen final
echo "╔══════════════════════════════════════════════════════════╗"
echo "║                    RESUMEN FINAL                         ║"
echo "╚══════════════════════════════════════════════════════════╝"
echo ""
echo "  Test Suites ejecutados:  $PASSED/$TOTAL"
echo ""

if [ $PASSED -eq $TOTAL ]; then
    echo "  ✅ ¡TODOS LOS TEST SUITES PASARON!"
    echo "  🎉 La migración a zcbor está 100% funcional"
    exit 0
else
    echo "  ❌ Algunos test suites fallaron"
    echo "  📝 Revisa los logs arriba para más detalles"
    exit 1
fi
