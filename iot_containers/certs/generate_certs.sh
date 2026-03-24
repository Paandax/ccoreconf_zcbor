#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

OPENSSL_BIN="${OPENSSL_BIN:-/usr/local/opt/openssl@3/bin/openssl}"
if [[ ! -x "$OPENSSL_BIN" ]]; then
  OPENSSL_BIN="openssl"
fi

# Identidad académica por defecto (UM)
COUNTRY="${CERT_C:-ES}"
STATE="${CERT_ST:-Murcia}"
LOCALITY="${CERT_L:-Murcia}"
ORG="${CERT_O:-Universidad de Murcia}"
CA_OU="${CERT_CA_OU:-Infraestructura PKI TFG}"
SRV_OU="${CERT_SERVER_OU:-Laboratorio IoT UM}"
CLI_OU="${CERT_CLIENT_OU:-Cliente CORECONF UM}"
SERVER_CN="${CERT_SERVER_CN:-172.20.0.10}"
SERVER_SAN="${CERT_SERVER_SAN:-IP:127.0.0.1,DNS:localhost,IP:172.20.0.10,DNS:coreconf_server,DNS:server}"

"$OPENSSL_BIN" genrsa -out ca.key 2048
"$OPENSSL_BIN" req -x509 -new -nodes -key ca.key -sha256 -days 3650 \
  -subj "/C=${COUNTRY}/ST=${STATE}/L=${LOCALITY}/O=${ORG}/OU=${CA_OU}/CN=coreconf-ca-um" \
  -out ca.crt

cat > server_ext.cnf <<EOC
basicConstraints=CA:FALSE
subjectAltName=${SERVER_SAN}
keyUsage=digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
EOC

"$OPENSSL_BIN" genrsa -out server.key 2048
"$OPENSSL_BIN" req -new -key server.key \
  -subj "/C=${COUNTRY}/ST=${STATE}/L=${LOCALITY}/O=${ORG}/OU=${SRV_OU}/CN=${SERVER_CN}" \
  -out server.csr
"$OPENSSL_BIN" x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out server.crt -days 825 -sha256 -extfile server_ext.cnf

cat > client_ext.cnf <<EOC
basicConstraints=CA:FALSE
keyUsage=digitalSignature
extendedKeyUsage=clientAuth
EOC

"$OPENSSL_BIN" genrsa -out client.key 2048
"$OPENSSL_BIN" req -new -key client.key \
  -subj "/C=${COUNTRY}/ST=${STATE}/L=${LOCALITY}/O=${ORG}/OU=${CLI_OU}/CN=coreconf-client-um" \
  -out client.csr
"$OPENSSL_BIN" x509 -req -in client.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
  -out client.crt -days 825 -sha256 -extfile client_ext.cnf

echo "\n--- Generando certificados para entorno LOCAL (localhost/127.0.0.1) ---"
# CA local
openssl genrsa -out ca-local.key 2048
openssl req -x509 -new -nodes -key ca-local.key -sha256 -days 3650 -out ca-local.crt -subj "/C=ES/ST=Murcia/L=Murcia/O=UM/OU=IoT/CN=coreconf-local-CA"

# Servidor local (localhost/127.0.0.1)
openssl genrsa -out server-local.key 2048
openssl req -new -key server-local.key -out server-local.csr -subj "/C=ES/ST=Murcia/L=Murcia/O=UM/OU=IoT/CN=localhost"
openssl x509 -req -in server-local.csr -CA ca-local.crt -CAkey ca-local.key -CAcreateserial -out server-local.crt -days 3650 -sha256 -extfile <(echo "subjectAltName=DNS:localhost,IP:127.0.0.1")

# Cliente local
openssl genrsa -out client-local.key 2048
openssl req -new -key client-local.key -out client-local.csr -subj "/C=ES/ST=Murcia/L=Murcia/O=UM/OU=IoT/CN=coreconf-client-local"
openssl x509 -req -in client-local.csr -CA ca-local.crt -CAkey ca-local.key -CAcreateserial -out client-local.crt -days 3650 -sha256

rm -f *.csr *.srl server_ext.cnf client_ext.cnf
echo "certificados generados en: $(pwd)"

rm -f *.csr *.srl server_ext.cnf client_ext.cnf
echo "certificados generados en: $(pwd)"
