# iot_containers

This directory contains the Docker setup used to run the CORECONF server/client demo.

## Contents

- `Dockerfile`: builds image `iot-coreconf` from Ubuntu 22.04.
- `docker-compose.yml`: starts server and client containers on a private bridge network.
- `iot_apps/coreconf_server.c`: libcoap server app.
- `iot_apps/coreconf_cli.c`: interactive CLI client app.
- `certs/`: certificate files and generation script.
- `rotate_password.sh`: script invoked by server logic when SID 1735 is received as a string value in PUT payload.
- `logs/`: mounted log directory (`./logs -> /iot_device/logs`).

## What the Dockerfile builds

The image:

- Installs build tools, `libcoap3-dev`, and utilities (`tcpdump`, `curl`, etc.).
- Copies project sources (`include`, `src`, `coreconf_zcbor_generated`, `sid`, `iot_apps`, certs).
- Builds `ccoreconf.a` using the root `Makefile`.
- Compiles:
  - `iot_apps/coreconf_server`
  - `iot_apps/coreconf_cli`
- Exposes UDP ports `5683` and `5684`.

Default container command is:

```bash
./iot_apps/coreconf_server
```

## Run with Docker Compose

From this directory:

```bash
docker compose up -d --build
```

Services configured in `docker-compose.yml`:

- `coreconf_server`
  - container name: `coreconf_server`
  - static IP: `172.20.0.10`
  - published ports: `5683:5683/udp`, `5684:5684/udp`
  - environment: `CORECONF_TLS_MODE=cert`
- `coreconf_client`
  - container name: `coreconf_client`
  - static IP: `172.20.0.11`
  - command: `./iot_apps/coreconf_cli temperature`
  - environment:
    - `SERVER_HOST=172.20.0.10`
    - `SERVER_PORT=5684`
    - `CORECONF_TLS_MODE=cert`

Open the client REPL:

```bash
docker attach coreconf_client
```

Detach without stopping the container: `Ctrl-P Ctrl-Q`.

## CLI quick commands (from coreconf_cli)

Inside the attached client:

```text
store temperature 22.5
get
fetch 10 20
ipatch 20 99.9
observe 30
```

Type `help` to show the full command list implemented by the CLI.

## Certificates

Certificate generation script:

```bash
cd certs
./generate_certs.sh
```

This script generates standard and local profiles, including:

- `ca.crt`, `server.crt`, `server.key`, `client.crt`, `client.key`
- `ca-local.crt`, `server-local.crt`, `server-local.key`, `client-local.crt`, `client-local.key`

## DTLS modes and variables

Server DTLS mode:

- `CORECONF_TLS_MODE=cert` (default in server code)
- `CORECONF_TLS_MODE=psk`

Server certificate variables:

- `CORECONF_CERT_PROFILE` (`local` switches defaults to local cert files)
- `CORECONF_CA_CERT`
- `CORECONF_SERVER_CERT`
- `CORECONF_SERVER_KEY`

Server PSK variables:

- `CORECONF_PSK_ID` (default `coreconf-client`)
- `CORECONF_PSK_KEY` (default `coreconf-secret`)

Client transport/security variables:

- `CORECONF_DTLS=0` forces UDP mode
- `CORECONF_TLS_MODE=cert|psk`
- `SERVER_HOST`, `SERVER_PORT`, `SERVER_SNI`
- `CORECONF_CERT_PROFILE`, `CORECONF_CA_CERT`, `CORECONF_CLIENT_CERT`, `CORECONF_CLIENT_KEY`
- `CORECONF_PSK_ID`, `CORECONF_PSK_KEY`
- `COAP_DEBUG_LEVEL=debug` enables debug logs
