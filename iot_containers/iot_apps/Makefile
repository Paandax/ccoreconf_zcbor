# Makefile para aplicaciones IoT

CC = gcc
LIBCOAP_INC = /Users/pablo/Desktop/coap_learning/libcoap/include
LIBCOAP_LIB = /Users/pablo/Desktop/coap_learning/libcoap/build
OPENSSL_ROOT = /usr/local/opt/openssl@3
CFLAGS = -Wall -Wextra -pedantic -std=c11 -Werror -I../../include -I../../coreconf_zcbor_generated -I$(LIBCOAP_INC) -I$(LIBCOAP_LIB)
LDFLAGS = -L../.. -L$(LIBCOAP_LIB) -L$(OPENSSL_ROOT)/lib -lcoap-3 -lssl -lcrypto -pthread -Wl,-rpath,$(OPENSSL_ROOT)/lib

# Biblioteca ccoreconf
LIB = ../../ccoreconf.a

# Aplicaciones: por defecto solo build server/cli (rápido)
APPS = coreconf_server coreconf_cli

# APPS_FULL conserva la lista completa antigua (uso: `make full`)
APPS_FULL = iot_server iot_client iot_test_local get_server get_client ipatch_server ipatch_client put_server put_client delete_server delete_client coreconf_server coreconf_cli

all: $(APPS)

# Objetivo para construir todo (antiguo comportamiento)
full: $(APPS_FULL)

# Helper: pick source file: prefer ./name.c else legacy/name.c
SRC = $(firstword $(wildcard $@.c legacy/$@.c))

%: $(LIB)
	$(CC) $(CFLAGS) $(call getsrc,$@) $(LIB) $(LDFLAGS) -o $@

# Provide a function to return the chosen source file for a target
define getsrc
$(firstword $(wildcard $1.c legacy/$1.c))
endef

clean:
	rm -f $(APPS_FULL) *.o

.PHONY: all clean
