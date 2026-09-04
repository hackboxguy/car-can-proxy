#!/bin/sh
# Create the bench topology: vcan0 = contract side, vcan1 = vehicle side.
# Idempotent. Needs root (or sudo) for ip link.
set -e
SUDO=""
[ "$(id -u)" -eq 0 ] || SUDO="sudo"
$SUDO modprobe vcan
for IF in vcan0 vcan1; do
    if ! ip link show "$IF" >/dev/null 2>&1; then
        $SUDO ip link add dev "$IF" type vcan
    fi
    $SUDO ip link set up "$IF"
done
for IF in vcan0 vcan1; do ip -br link show "$IF"; done
