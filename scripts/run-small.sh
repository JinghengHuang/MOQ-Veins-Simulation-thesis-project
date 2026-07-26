#!/bin/bash
echo "=== Starting small range run... ==="
./run-comparison.sh ../simulations/results/moq_tcp 5 urban "" moq_tcp
./run-comparison.sh ../simulations/results/moq_tcp_highway 5 highway "" moq_tcp
./run-comparison.sh ../simulations/results/moq_highway 5 highway "" moq
./run-comparison.sh ../simulations/results/moq 5 urban "" moq
echo "=== Small range run complete ==="