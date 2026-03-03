# Makefile para aplicaciones IoT

CC = gcc
LIBCOAP_INC = /Users/pablo/Desktop/coap_learning/libcoap/include
LIBCOAP_LIB = /Users/pablo/Desktop/coap_learning/libcoap/build
CFLAGS = -Wall -Wextra -pedantic -std=c11 -Werror -I../../include -I../../coreconf_zcbor_generated -I$(LIBCOAP_INC) -I$(LIBCOAP_LIB)
LDFLAGS = -L../.. -L$(LIBCOAP_LIB) -lcoap-3 -pthread

# Biblioteca ccoreconf
LIB = ../../ccoreconf.a

# Aplicaciones
APPS = iot_server iot_client iot_test_local get_server get_client ipatch_server ipatch_client put_server put_client delete_server delete_client coreconf_server coreconf_cli

all: $(APPS)

iot_server: iot_server.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

iot_client: iot_client.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

iot_test_local: iot_test_local.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

get_server: get_server.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

get_client: get_client.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

ipatch_server: ipatch_server.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

ipatch_client: ipatch_client.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

put_server: put_server.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

put_client: put_client.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

delete_server: delete_server.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

delete_client: delete_client.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

coreconf_server: coreconf_server.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

coreconf_cli: coreconf_cli.c $(LIB)
	$(CC) $(CFLAGS) $< $(LIB) $(LDFLAGS) -o $@

clean:
	rm -f $(APPS) *.o

.PHONY: all clean
