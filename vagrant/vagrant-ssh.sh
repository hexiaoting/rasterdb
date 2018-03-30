#!/bin/bash

set -x

# Create gpadmin user
sudo groupadd -g 555 gpadmin
sudo adduser -u 555 -g 555 gpadmin
sudo echo "%gpadmin ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/gpadmin

SSH_DIR=/home/gpadmin/.ssh
AUTH_FILE="$SSH_DIR"/authorized_keys

sudo rm -rf "$SSH_DIR"

sudo mkdir "$SSH_DIR"
sudo chmod 0700 "$SSH_DIR"
sudo cat /vagrant/id_ecdsa.pub > "$AUTH_FILE"
sudo chmod 0600 "$AUTH_FILE"
sudo chown -R gpadmin "$SSH_DIR"
sudo chgrp -R gpadmin "$SSH_DIR"

