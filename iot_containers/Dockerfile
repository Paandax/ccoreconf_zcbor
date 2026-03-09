# Dockerfile para simular dispositivo IoT con ccoreconf + zcbor
FROM ubuntu:22.04

# Evitar prompts interactivos
ENV DEBIAN_FRONTEND=noninteractive

# Instalar dependencias básicas + libcoap3
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    make \
    pkg-config \
    netcat-traditional \
    iproute2 \
    iputils-ping \
    curl \
    libcoap3-dev \
    tcpdump \
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

# Compilar aplicaciones CORECONF (draft-ietf-core-comi-20)
# Ubuntu 22.04 libcoap3-dev instala con sufijo -lcoap-3-notls
RUN COAP_LIBS=$(pkg-config --libs libcoap-3-notls 2>/dev/null || echo "-lcoap-3-notls") && \
    COAP_INC=$(pkg-config --cflags libcoap-3-notls 2>/dev/null || echo "") && \
    echo "Usando flags: $COAP_INC $COAP_LIBS" && \
    cd iot_apps && \
    gcc -Wall -Wextra -pedantic -std=c11 -Werror -I../include -I../coreconf_zcbor_generated $COAP_INC \
        coreconf_server.c ../ccoreconf.a $COAP_LIBS -pthread -o coreconf_server && \
    gcc -Wall -Wextra -pedantic -std=c11 -Werror -I../include -I../coreconf_zcbor_generated $COAP_INC \
        coreconf_cli.c ../ccoreconf.a $COAP_LIBS -pthread -o coreconf_cli

# Puerto CoAP
EXPOSE 5683/udp

# Por defecto, ejecutar el servidor CORECONF
CMD ["./iot_apps/coreconf_server"]
