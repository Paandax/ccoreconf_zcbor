#!/bin/bash
# Script de prueba rápida de comunicación IoT

echo "╔═══════════════════════════════════════════════════════════╗"
echo "║           PRUEBA RÁPIDA IoT - CORECONF/CBOR               ║"
echo "╚═══════════════════════════════════════════════════════════╝"
echo ""

cd "$(dirname "$0")/iot_apps"

# Verificar que existen los ejecutables
if [ ! -f "./iot_server" ] || [ ! -f "./iot_client" ]; then
    echo "❌ Ejecutables no encontrados. Compilando..."
    make
    if [ $? -ne 0 ]; then
        echo "❌ Error en compilación"
        exit 1
    fi
fi

# Matar servidor previo si existe
killall iot_server 2>/dev/null
sleep 1

echo "🚀 Iniciando servidor en background..."
./iot_server > server.log 2>&1 &
SERVER_PID=$!

sleep 2

if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "❌ El servidor no arrancó correctamente"
    cat server.log
    exit 1
fi

echo "✅ Servidor corriendo (PID: $SERVER_PID)"
echo ""

# Ejecutar cliente
echo "📡 Ejecutando cliente..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
DEVICE_ID="temp-sensor-test" GATEWAY_HOST="127.0.0.1" GATEWAY_PORT="5683" ./iot_client temperature

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "📊 Log del servidor:"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
cat server.log
echo ""

# Limpiar
echo "🧹 Limpiando..."
kill $SERVER_PID 2>/dev/null
rm -f server.log

echo ""
echo "✅ Prueba completada"
