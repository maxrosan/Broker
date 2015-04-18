#!/bin/bash -xe

if [ "x$1" == "xcomp" ]; then
	gcc -ggdb md5.c broker.c -lsqlite3 -ljson -lpthread -o broker
	gcc -ggdb md5.c sensor.c -lsqlite3 -ljson -lpthread -o sensor
	gcc -ggdb md5.c subscriber.c -lsqlite3 -ljson -lpthread -o subscriber
elif [ "x$1" == "xsubs" ]; then
	echo "{ \"type\":\"subscribe\", \"attributes\":[\"X\",\"Y\"] }" | nc -u localhost 10001;
elif [ "x$1" == "xev" ]; then
	echo "{ \"type\":\"event\", \"X\":\"1\",\"Y\":\"2\" }" | nc -u localhost 10001;
fi;


