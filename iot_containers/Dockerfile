# Dockerfile to simulate an IoT device with ccoreconf + zcbor
FROM ubuntu:22.04

# Avoid interactive prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install base dependencies + libcoap3
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

# Create working directory
WORKDIR /iot_device

# Copy source code from project root
COPY include/ ./include/
COPY src/ ./src/
COPY coreconf_zcbor_generated/ ./coreconf_zcbor_generated/
COPY sid/ ./sid/
COPY iot_containers/certs/ ./certs/
COPY Makefile ./

# Copy IoT applications
COPY iot_containers/iot_apps/ ./iot_apps/

# Build library
RUN make clean && make

# Adjust include paths and libcoap log constants
# Replace DEBUG with WARN for Ubuntu compiler compatibility
RUN cd iot_apps && \
    sed -i 's|"../../include/|"|g' coreconf_server.c coreconf_cli.c && \
    sed -i 's|"../../coreconf_zcbor_generated/|"|g' coreconf_server.c coreconf_cli.c && \
    sed -i 's/COAP_LOG_DEBUG/COAP_LOG_WARN/g' coreconf_server.c coreconf_cli.c && \
    sed -i 's/COAP_LOG_INFO/COAP_LOG_WARN/g' coreconf_server.c coreconf_cli.c

# Build CORECONF applications (draft-ietf-core-comi-20)
RUN COAP_PKG=$(pkg-config --exists libcoap-3-openssl && echo libcoap-3-openssl || \
               (pkg-config --exists libcoap-3-gnutls && echo libcoap-3-gnutls || echo libcoap-3-notls)) && \
    COAP_LIBS=$(pkg-config --libs "$COAP_PKG" 2>/dev/null || echo "-lcoap-3") && \
    COAP_INC=$(pkg-config --cflags "$COAP_PKG" 2>/dev/null || echo "") && \
    echo "Using libcoap backend: $COAP_PKG" && \
    echo "Using flags: $COAP_INC $COAP_LIBS" && \
    cd iot_apps && \
    gcc -Wall -Wextra -pedantic -std=c11 -I../include -I../coreconf_zcbor_generated $COAP_INC \
        coreconf_server.c ../ccoreconf.a $COAP_LIBS -pthread -o coreconf_server && \
    gcc -Wall -Wextra -pedantic -std=c11 -I../include -I../coreconf_zcbor_generated $COAP_INC \
        coreconf_cli.c ../ccoreconf.a $COAP_LIBS -pthread -o coreconf_cli

# CoAP / CoAPS ports
EXPOSE 5683/udp
EXPOSE 5684/udp

# 1. Create test user 'iotadmin'
RUN useradd -m iotadmin && echo "iotadmin:admin1234" | chpasswd

# 2. Copy password rotation script into the container
COPY iot_containers/rotate_password.sh /usr/local/bin/rotate_password.sh

# 3. Make script executable
RUN chmod +x /usr/local/bin/rotate_password.sh

# Create security audit log file
RUN touch /var/log/iot_security.log && chmod 666 /var/log/iot_security.log

# Default entrypoint: run CORECONF server
CMD ["./iot_apps/coreconf_server"]