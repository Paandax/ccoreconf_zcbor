# 🌐 Simulación IoT con Contenedores - CORECONF/CBOR

Este proyecto simula un ecosistema IoT completo usando contenedores Docker, donde los dispositivos se comunican usando **CORECONF** sobre **CBOR** (Concise Binary Object Representation).

## 📋 Arquitectura

```
                    ┌─────────────────┐
                    │   IoT Gateway   │
                    │  (172.20.0.10)  │
                    │   Puerto 5683   │
                    └────────┬────────┘
                             │
            ┌────────────────┼────────────────┐
            │                │                │
    ┌───────▼──────┐  ┌─────▼──────┐  ┌─────▼──────┐
    │  Temp Sensor │  │ Hum Sensor │  │  Actuator  │
    │ 172.20.0.11  │  │172.20.0.12 │  │172.20.0.13 │
    └──────────────┘  └────────────┘  └────────────┘
                             │
                      ┌──────▼──────┐
                      │ Edge Device │
                      │ 172.20.0.14 │
                      └─────────────┘
```

### Componentes

1. **Gateway** (172.20.0.10)
   - Servidor central que recibe datos CBOR
   - Procesa mensajes de todos los dispositivos
   - Responde con acknowledgments

2. **Sensores**
   - **Temperature Sensor** (172.20.0.11): Envía temperatura en °C
   - **Humidity Sensor** (172.20.0.12): Envía humedad en %
   - Frecuencia: Cada 5 segundos

3. **Actuador** (172.20.0.13)
   - Relay/LED simulado
   - Reporta estado ON/OFF

4. **Edge Device** (172.20.0.14)
   - Procesa datos de múltiples sensores
   - Envía datos agregados al gateway

## 🚀 Inicio Rápido

### Opción 1: Test Local (sin contenedores)

```bash
cd iot_containers/iot_apps
make
./iot_test_local
```

### Opción 2: Contenedores Docker

#### Construir imagen
```bash
cd iot_containers
docker build -t iot-coreconf .
```

#### Iniciar todos los servicios
```bash
docker-compose up
```

#### Iniciar servicios individuales
```bash
# Solo el gateway
docker-compose up iot_gateway

# Gateway + sensor de temperatura
docker-compose up iot_gateway iot_sensor_temp

# Todos los servicios
docker-compose up -d  # modo detached
```

#### Ver logs en tiempo real
```bash
# Todos los contenedores
docker-compose logs -f

# Solo el gateway
docker-compose logs -f iot_gateway

# Solo un sensor
docker-compose logs -f iot_sensor_temp
```

#### Detener servicios
```bash
docker-compose down
```

## 📡 Protocolo de Comunicación

### Formato de Mensajes (CBOR)

Todos los mensajes se intercambian en formato CBOR compacto.

#### Mensaje de Sensor (ejemplo temperatura)
```json
{
  "device_id": "sensor-temp-001",
  "type": "temperature",
  "timestamp": 1707739200,
  "temperature": 23.5,
  "unit": "celsius"
}
```

**CBOR encoding** (~50 bytes):
```
bf 69 64 65 76 69 63 65 5f 69 64 71 73 65 6e 73 
6f 72 2d 74 65 6d 70 2d 30 30 31 64 74 79 70 65 
6b 74 65 6d 70 65 72 61 74 75 72 65 ...
```

#### Respuesta del Gateway
```json
{
  "device_id": "gateway-001",
  "status": "ack",
  "timestamp": 1707739200
}
```

### Ventajas de CBOR en IoT

- **Tamaño**: 40-60% más pequeño que JSON
- **Parsing**: Más rápido y eficiente en CPU
- **Tipos nativos**: Soporta enteros, floats, booleans sin conversión
- **Binario**: Perfecto para redes con bajo ancho de banda

## 🧪 Escenarios de Prueba

### Escenario 1: Monitoreo Continuo
```bash
docker-compose up
```
- Todos los sensores envían datos cada 5 segundos
- El gateway recibe y procesa todos los mensajes
- Logs muestran decodificación CBOR en tiempo real

### Escenario 2: Edge Computing
```bash
docker-compose up iot_gateway iot_edge
```
- Edge device agrega datos de múltiples sensores virtuales
- Envía un solo mensaje CBOR compacto al gateway
- Reduce tráfico de red significativamente

### Escenario 3: Control de Actuadores
```bash
docker-compose up iot_gateway iot_actuator
```
- Actuador reporta estado cada 5 segundos
- Simula relay o LED
- Gateway puede enviar comandos de control (futuro)

### Escenario 4: Test de Carga
```bash
# Escalar sensores
docker-compose up --scale iot_sensor_temp=5 --scale iot_sensor_humidity=3
```
- Múltiples instancias del mismo sensor
- Prueba capacidad del gateway
- Verifica manejo de concurrencia

## 📊 Métricas y Observabilidad

### Ver estadísticas de contenedores
```bash
docker stats
```

### Inspeccionar red IoT
```bash
docker network inspect iot_containers_iot_network
```

### Entrar a un contenedor
```bash
# Gateway
docker exec -it iot_gateway /bin/bash

# Sensor
docker exec -it iot_sensor_temp /bin/bash

# Probar conectividad desde dentro
ping 172.20.0.10
nc -zv 172.20.0.10 5683
```

### Capturar tráfico CBOR
```bash
# Desde el host
sudo tcpdump -i docker0 port 5683 -X

# Guardar en archivo
sudo tcpdump -i docker0 port 5683 -w iot_traffic.pcap
```

## 🔧 Configuración Avanzada

### Variables de Entorno

Edita [docker-compose.yml](docker-compose.yml) para cambiar:

```yaml
environment:
  - DEVICE_ID=custom-sensor-001
  - GATEWAY_HOST=172.20.0.10
  - GATEWAY_PORT=5683
```

### Cambiar Frecuencia de Envío

Edita `iot_apps/iot_client.c`:
```c
#define SEND_INTERVAL 5  // cambiar a 10, 30, etc.
```

Recompilar:
```bash
docker-compose build
docker-compose up
```

## 📁 Estructura de Archivos

```
iot_containers/
├── Dockerfile                  # Imagen base para dispositivos IoT
├── docker-compose.yml          # Orquestación de servicios
├── README.md                   # Esta documentación
├── iot_apps/
│   ├── Makefile               # Build de aplicaciones IoT
│   ├── iot_server.c           # Gateway server (recibe CBOR)
│   ├── iot_client.c           # Cliente IoT (envía CBOR)
│   └── iot_test_local.c       # Tests sin red
└── logs/                      # Logs persistentes (auto-creado)
```

## 🐛 Troubleshooting

### Gateway no arranca
```bash
# Verificar que el puerto 5683 esté libre
sudo lsof -i :5683

# Ver logs detallados
docker-compose logs iot_gateway
```

### Sensores no conectan
```bash
# Verificar red
docker network ls
docker network inspect iot_containers_iot_network

# Verificar que gateway esté arriba
docker-compose ps

# Probar conectividad manual
docker exec iot_sensor_temp ping 172.20.0.10
```

### Recompilar todo desde cero
```bash
docker-compose down
docker-compose build --no-cache
docker-compose up
```

## 📚 Próximos Pasos

1. **Persistencia**: Guardar datos en base de datos
2. **Dashboard**: Visualización web de datos en tiempo real
3. **CoAP**: Implementar protocolo CoAP sobre CBOR
4. **DTLS**: Añadir encriptación a las comunicaciones
5. **MQTT Bridge**: Conectar con broker MQTT
6. **Kubernetes**: Deploy en cluster K8s

## 🎯 Casos de Uso Reales

Este simulador es útil para:

- **Desarrollo**: Probar código IoT sin hardware real
- **Testing**: Validar protocolos de comunicación
- **Educación**: Aprender arquitecturas IoT
- **Demos**: Mostrar funcionamiento de redes IoT
- **Benchmarking**: Medir rendimiento de CBOR vs JSON

## 📄 Licencia

Este proyecto es parte del TFG sobre migración de ccoreconf a zcbor.

---

**🎉 ¡Disfruta experimentando con IoT + CBOR!**
