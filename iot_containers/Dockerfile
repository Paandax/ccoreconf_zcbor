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

# Copiar código fuente desde el directorio raíz del proyecto
COPY include/ ./include/
COPY src/ ./src/
COPY coreconf_zcbor_generated/ ./coreconf_zcbor_generated/
COPY Makefile ./

# Copiar aplicaciones IoT específicas
COPY iot_containers/iot_apps/ ./iot_apps/

# Compilar la biblioteca
RUN make clean && make

# Ajustar rutas de includes en los archivos IoT antes de compilar
RUN cd iot_apps && \
    sed -i 's|"../../include/|"|g' *.c && \
    sed -i 's|"../../coreconf_zcbor_generated/|"|g' *.c

# Compilar aplicaciones IoT directamente con rutas correctas
RUN cd iot_apps && \
    gcc -Wall -Wextra -pedantic -std=c11 -Werror -I../include -I../coreconf_zcbor_generated \
        iot_server.c ../ccoreconf.a -pthread -o iot_server && \
    gcc -Wall -Wextra -pedantic -std=c11 -Werror -I../include -I../coreconf_zcbor_generated \
        iot_client.c ../ccoreconf.a -pthread -o iot_client && \
    gcc -Wall -Wextra -pedantic -std=c11 -Werror -I../include -I../coreconf_zcbor_generated \
        iot_test_local.c ../ccoreconf.a -pthread -o iot_test_local

# Exponer puertos
EXPOSE 5683 8080

# Variable de entorno para identificar el dispositivo
ENV DEVICE_ID=unknown
ENV DEVICE_TYPE=sensor

# Por defecto, ejecutar el servidor IoT
CMD ["./iot_apps/iot_server"]
