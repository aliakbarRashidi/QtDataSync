#!/bin/sh

echo "[server]" > $QDSAPP_CONFIG_FILE
echo "port=4242" >> $QDSAPP_CONFIG_FILE
echo "[database]" >> $QDSAPP_CONFIG_FILE
echo "host=$QDSAPP_DATABASE_HOST" >> $QDSAPP_CONFIG_FILE
echo "port=$QDSAPP_DATABASE_PORT" >> $QDSAPP_CONFIG_FILE
echo "username=$QDSAPP_DATABASE_USER" >> $QDSAPP_CONFIG_FILE
echo "password=$QDSAPP_DATABASE_PASSWORD" >> $QDSAPP_CONFIG_FILE
echo "name=$QDSAPP_DATABASE_NAME" >> $QDSAPP_CONFIG_FILE

exec /usr/bin/qdsappd