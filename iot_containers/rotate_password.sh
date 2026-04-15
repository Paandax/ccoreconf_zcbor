#!/bin/bash
# Script to securely rotate a user password

USERNAME=$1
NEW_PASSWORD=$2

# Check required parameters
if [ -z "$USERNAME" ] || [ -z "$NEW_PASSWORD" ]; then
    echo "Error: missing parameters."
    exit 1
fi

# 1. Change password using chpasswd
echo "$USERNAME:$NEW_PASSWORD" | chpasswd

# 2. Append an audit log entry
echo "$(date): Password updated via secure CoAP/CORECONF for user $USERNAME" >> /var/log/iot_security.log

exit 0