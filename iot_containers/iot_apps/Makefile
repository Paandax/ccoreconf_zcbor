# Makefile para aplicaciones IoT

CC = gcc
LIBCOAP_INC = /Users/pablo/Desktop/coap_learning/libcoap/include
LIBCOAP_LIB = /Users/pablo/Desktop/coap_learning/libcoap/build
CFLAGS = -Wall -Wextra -pedantic -std=c11 -Werror -I../../include -I../../coreconf_zcbor_generated -I$(LIBCOAP_INC) -I$(LIBCOAP_LIB)
LDFLAGS = -L../.. -L$(LIBCOAP_LIB) -lcoap-3 -pthread

# Biblioteca ccoreconf
LIB = ../../ccoreconf.a

# Aplicaciones
APPS = iot_server iot_client iot_test_local

all: $(APPS)

iot_server: iot_server.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

iot_client: iot_client.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

iot_test_local: iot_test_local.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

clean:
	rm -f $(APPS) *.o

.PHONY: all clean
