#!/bin/bash

USERNAME=$1
PASSWORD=$2

# Add user and credentials to password file.

echo $USERNAME:$(echo -n "$USERNAME:$PASSWORD" | sha256sum | awk '{print $1}') >> local/user/passwd

# Create user home directory.

mkdir -p local/user/$USERNAME
echo '{}' > local/user/$USERNAME/index.json

echo "Created new user $USERNAME"

