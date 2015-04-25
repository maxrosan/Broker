#!/bin/bash -xe

if [ "x$1" == "xcomp" ]; then

	CFLAGS=`pkg-config python --cflags`
	LIBS=`pkg-config python --libs`
	LIBSWS=`pkg-config libwebsockets --libs`

	gcc -ggdb $CFLAGS -c interpreter.c
	gcc -ggdb $CFLAGS -c md5.c
	gcc -ggdb $CFLAGS -c queue.c
	gcc -ggdb $CFLAGS -c crypto.c

	gcc -ggdb $CFLAGS interpreter.o md5.o queue.o crypto.o broker.c -lsqlite3 -ljson -lpthread $LIBS -o broker

	gcc -ggdb $CFLAGS md5.o sensor.c -lsqlite3 -ljson -lpthread $LIBS -o sensor
	gcc -ggdb $CFLAGS md5.o subscriber.c -lsqlite3 -ljson -lpthread $LIBS -o subscriber

	gcc -ggdb crypto.o subscriber_websocket.c $LIBSWS -lpthread -o subscriber_websocket

elif [ "x$1" == "xsubs" ]; then
	echo "{ \"type\":\"subscribe\", \"attributes\":[\"X\",\"Y\"] }" | nc -u localhost 10001;
elif [ "x$1" == "xev" ]; then
	echo "{ \"type\":\"event\", \"X\":\"1\",\"Y\":\"2\" }" | nc -u localhost 10001;
elif [ "x$1" == "xrun" ]; then

	nohup ./broker configuration.json &
	sleep 1
	nohup ./subscriber_websocket 127.0.0.1 9001 9000 place temperature humidity &
	sleep 1
	nohup ./subscriber_websocket 127.0.0.1 9001 9002 place light reed &


fi;


