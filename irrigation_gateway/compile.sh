gcc -o gateway main.c ble.c sys.c client.c http_client.c gateway_threads.c \
    $(pkg-config --cflags --libs dbus-1) \
    -lcurl -lcjson -lpthread