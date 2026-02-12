# 🚀 Guía de Inicio Rápido - CORECONF con zcbor

## ⚡ 5 Minutos para Empezar

### 1️⃣ Compilar la Librería (30 segundos)

```bash
cd ccoreconf_zcbor
make clean && make
```

**✅ Resultado esperado**: `ccoreconf.a` creado (114KB), 0 errores

---

### 2️⃣ Ejecutar Tests (1 minuto)

```bash
cd examples
./run_all_tests.sh
```

**✅ Resultado esperado**: 50/50 tests pasados ✅

---

### 3️⃣ Probar Comunicación IoT (2 minutos)

**Terminal 1 - Servidor:**
```bash
cd iot_containers/iot_apps
./iot_server
```

**Terminal 2 - Cliente:**
```bash
cd iot_containers/iot_apps
DEVICE_ID="mi-sensor" GATEWAY_HOST="127.0.0.1" ./iot_client temperature
```

**✅ Resultado esperado**: 
- Servidor recibe datos CBOR ✅
- Cliente envía STORE y recibe ACK ✅
- Cliente envía FETCH y recibe valor ✅

---

## 📚 ¿Qué Hacer Después?

### Explorar los Tests

```bash
# Test básico de encode/decode
./examples/test_zcbor_migration

# Demo de operación FETCH
./examples/test_fetch_simple

# Suite completa (40 tests)
./examples/test_exhaustive
```

### Leer la Documentación

- [`README.md`](README.md) - Documentación completa del proyecto
- [`RESUMEN_PROYECTO.md`](RESUMEN_PROYECTO.md) - Resumen ejecutivo
- [`iot_containers/README.md`](iot_containers/README.md) - Guía IoT detallada

### Modificar el Código

**Ejemplo: Crear tu propio sensor**

Edita `iot_containers/iot_apps/iot_client.c`:

```c
// Añadir nuevo tipo de sensor
if (strcmp(device_type, "mi_sensor") == 0) {
    // SID 20: tu valor personalizado
    insertCoreconfHashMap(map, 20, createCoreconfReal(mi_valor));
    
    // SID 21: tu unidad
    insertCoreconfHashMap(map, 21, createCoreconfString("mi_unidad"));
}
```

Recompilar:
```bash
cd iot_containers/iot_apps
make
DEVICE_ID="test" GATEWAY_HOST="127.0.0.1" ./iot_client mi_sensor
```

---

## 🐳 Docker (Opcional)

**Build de imagen**:
```bash
cd iot_containers
docker build -t iot-coreconf .
```

**Lanzar ecosistema completo**:
```bash
docker-compose up
```

Esto lanza 5 contenedores:
- Gateway (172.20.0.10:5683)
- Sensor temperatura (172.20.0.11)
- Sensor humedad (172.20.0.12)
- Actuador (172.20.0.13)
- Edge device (172.20.0.14)

---

## 🔧 Comandos Útiles

### Compilación

```bash
# Limpiar y recompilar todo
make clean && make

# Solo librería principal
make ccoreconf.a

# Solo tests
cd examples && make

# Solo apps IoT
cd iot_containers/iot_apps && make
```

### Testing

```bash
# Todos los tests
cd examples && ./run_all_tests.sh

# Test individual
./examples/test_exhaustive

# Test local IoT (sin red)
cd iot_containers/iot_apps && ./iot_test_local
```

### Limpieza

```bash
# Limpiar builds
make clean
cd examples && make clean
cd iot_containers/iot_apps && make clean

# Limpiar todo (incluye Docker)
docker-compose down
docker rmi iot-coreconf
```

---

## ❓ Troubleshooting

### Error: "ccoreconf.a not found"

```bash
cd ccoreconf_zcbor
make clean && make
```

### Error: "cannot connect to gateway"

Verifica que el servidor esté corriendo:
```bash
ps aux | grep iot_server
```

Si no está, inícialo:
```bash
cd iot_containers/iot_apps
./iot_server
```

### Error de compilación: "zcbor_*.h not found"

Verifica que el subdirectorio zcbor exista:
```bash
ls -la zcbor/include/
```

Si falta, clona el submódulo:
```bash
git submodule update --init --recursive
```

---

## 📈 Siguientes Pasos

1. ✅ **Completado**: Migración y tests básicos
2. ✅ **Completado**: Comunicación IoT local
3. ⏭️ **Siguiente**: Docker multi-contenedor
4. ⏭️ **Futuro**: Integración con hardware real

---

## 🆘 ¿Necesitas Ayuda?

- 📖 **Documentación completa**: Ver [README.md](README.md)
- 📝 **Resumen técnico**: Ver [RESUMEN_PROYECTO.md](RESUMEN_PROYECTO.md)
- 🌐 **Guía IoT**: Ver [iot_containers/README.md](iot_containers/README.md)
- 🔍 **Buscar en código**: `grep -r "término" .`

---

## ✨ Consejos Finales

1. **Empieza con los tests**: Son la mejor manera de entender el código
2. **Lee el README completo**: Tiene ejemplos detallados de API
3. **Experimenta con los valores**: Cambia SIDs, tipos, valores
4. **Revisa los logs del servidor**: Muestran todo el flujo CBOR
5. **Usa `printCoreconf()`**: Para debug de estructuras

---

<div align="center">

**¡Listo para explorar CORECONF/CBOR! 🚀**

</div>
