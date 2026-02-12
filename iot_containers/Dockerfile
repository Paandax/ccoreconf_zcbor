# Dockerfile para simular dispositivo IoT con ccoreconf + zcbor
FROM ubuntu:22.04

# Evitar prompts interactivos
ENV DEBIAN_FRONTEND=noninteractive

# Instalar dependencias básicas
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    make \
    netcat-traditional \
    iproute2 \
    iputils-ping \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Crear directorio de trabajo
WORKDIR /iot_device

# Copiar código fuente desde el directorio padre
COPY ../include/ ./include/
COPY ../src/ ./src/
COPY ../coreconf_zcbor_generated/ ./coreconf_zcbor_generated/
COPY ../Makefile ./

# Copiar aplicaciones IoT específicas
COPY iot_apps/ ./iot_apps/

# Compilar la biblioteca
RUN make clean && make

# Compilar aplicaciones IoT
RUN cd iot_apps && make

# Exponer puertos
EXPOSE 5683 8080

# Variable de entorno para identificar el dispositivo
ENV DEVICE_ID=unknown
ENV DEVICE_TYPE=sensor

# Por defecto, ejecutar el servidor IoT
CMD ["./iot_apps/iot_server"]
