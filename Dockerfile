# Dockerfile para simular dispositivo IoT con ccoreconf + zcbor
FROM ubuntu:22.04

# Evitar prompts interactivos
ENV DEBIAN_FRONTEND=noninteractive

# Instalar dependencias básicas
RUN apt-get update && apt-get install -y \
    build-essential \
    gcc \
    make \
    netcat \
    && rm -rf /var/lib/apt/lists/*

# Crear directorio de trabajo
WORKDIR /app

# Copiar código fuente
COPY include/ ./include/
COPY src/ ./src/
COPY coreconf_zcbor_generated/ ./coreconf_zcbor_generated/
COPY examples/ ./examples/
COPY Makefile ./
COPY run_all_tests.sh ./

# Compilar la biblioteca y tests
RUN make clean && make && make examples

# Hacer ejecutable el script de tests
RUN chmod +x run_all_tests.sh

# Exponer puerto para comunicación entre dispositivos
EXPOSE 8080

# Por defecto, ejecutar tests
CMD ["./run_all_tests.sh"]
