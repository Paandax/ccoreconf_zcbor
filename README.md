# ccoreconf_zcbor

ccoreconf_zcbor is a C implementation for CORECONF data handling and CoAP/CoAPS transport examples, based on SID-driven CBOR encodings.

The project is focused on implementation work, interoperability experiments, and draft-oriented demonstrations for constrained environments.

## Scope

- CORECONF value model and C containers (CoreconfValueT, hash tables, and arrays).
- SID-based CBOR encoding/decoding using zcbor-generated code.
- Operation helpers for GET, FETCH, iPATCH, PUT, DELETE, and POST.
- libcoap-based server and interactive CLI for end-to-end testing.
- Runtime loading of SID dictionaries and SID fingerprint compatibility checks between client and server.

## Standards alignment

This repository follows CORECONF behavior and on-the-wire formats used in:

- RFC 9254 concepts for COMI/CORECONF operations and SID-based encodings.
- Current CORECONF draft examples for payload shape and response semantics.

Implementation note:
This code is an implementation and test bench, not a normative text. As drafts evolve, repository behavior may be updated to preserve interoperability and fidelity with examples.

## Operation coverage

Resource /c supports datastore operations and RPC/action invocation:

- GET /c: full datastore read.
- FETCH /c: selective read using SID lists or IID lists.
- iPATCH /c: partial datastore update.
- PUT /c: full datastore replacement.
- DELETE /c: full datastore deletion.
- POST /c: RPC/action invocation with application/yang-instances+cbor-seq.

Additional resources:

- /s: observe stream (GET with Observe, plus filtered FETCH).
- /sid: SID fingerprint endpoint for dictionary compatibility between client and server.

## Repository structure

- src/: main implementation modules.
- include/: public headers.
- coreconf_zcbor_generated/: zcbor-generated C/H files.
- examples/: test and migration examples.
- iot_containers/: Docker image, compose stack, certificates, and demo apps.
- sid/: SID dictionaries used at runtime.

## Build

Build the static library:

```bash
make clean
make
```

Artifacts:

- ccoreconf.a
- object files in obj/

Build examples:

```bash
make examples
```

Regenerate support artifacts:

```bash
./generate_coreconf_zcbor.sh
make sids
```

## Docker demo

From iot_containers/:

```bash
docker compose up -d --build
docker attach coreconf_client
```

Default topology:

- coreconf_server: 172.20.0.10 (5683 CoAP, 5684 CoAPS/DTLS)
- coreconf_client: 172.20.0.11 (interactive REPL)

To detach from the client without stopping containers: Ctrl-P Ctrl-Q.

## CLI quick reference

- store [type] [value]
- store ietf-example
- get
- fetch <SID> [SID...] [[SID,key]...]
- sfetch <SID> [SID...] [[SID,key]...]
- ipatch <SID> <value>
- ipatch <SID> null
- ipatch [SID,key] null|<value>
- post reboot [delay]
- post reset <name> <reset-at-rfc3339>
- put <SID> <v> [SID <v> ...]
- delete
- observe [seconds]
- demo
- sidcheck
- info
- quit / exit

## Example: RPC and Action via POST

The current demo includes draft-style RPC/action examples:

- RPC reboot (SID 61000) with request/response form and null output.
- Action reset (SID 60002) over list-instance IID [SID, "name"] with output timestamp.

Both examples are executed from the CLI and validated over CoAPS in Docker.

## SID dictionaries and compatibility

At startup, server and client load SID files at runtime from sid/, including:

- ietf-system.sid
- ietf-interfaces.sid
- ietf-comi-notification.sid
- ietf-coreconf-actions.sid

The client validates compatibility by querying /sid and comparing the remote fingerprint against its local fingerprint. On mismatch, the CLI exits to avoid interpreting incorrect SIDs.

## Security and transport configuration

Server environment variables:

- CORECONF_TLS_MODE: cert (default) or psk
- CORECONF_CERT_PROFILE: local profile for local certificate set
- CORECONF_CA_CERT, CORECONF_SERVER_CERT, CORECONF_SERVER_KEY
- CORECONF_PSK_ID, CORECONF_PSK_KEY

Client environment variables:

- SERVER_HOST, SERVER_PORT, SERVER_SNI
- CORECONF_DTLS (0 forces unencrypted UDP)
- CORECONF_TLS_MODE: cert or psk
- CORECONF_CERT_PROFILE
- CORECONF_CA_CERT, CORECONF_CLIENT_CERT, CORECONF_CLIENT_KEY
- CORECONF_PSK_ID, CORECONF_PSK_KEY
- COAP_DEBUG_LEVEL

## Intended use

This repository is appropriate for:

- Prototyping CORECONF implementations.
- On-the-wire format interoperability testing and SID dictionary checks.
- Demonstrating operation behavior in constrained CoAP/DTLS scenarios.

For strict conformance evaluation, always compare against the latest published RFC and active draft revisions.

## Project status and license

This repository is part of a Final Degree Project (TFG).

At this time, no software license has been granted for this code.
Until a license is explicitly added, it must be treated as all rights reserved.

That implies usage, redistribution, and derivative works are currently not authorized.
