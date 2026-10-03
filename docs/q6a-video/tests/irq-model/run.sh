#!/usr/bin/env bash
# Locking model of the IRQ-thread / power-off interaction (see the "keep the IRQ thread off the registers" commit). Not kernel code.
set -u; cd "$(dirname "$0")" || exit 2
cc -O1 -pthread -o /tmp/irq_model irq_model.c && /tmp/irq_model
