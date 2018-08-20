#!/bin/bash

docker build -t cochain/eoscochain -f Dockerfile.custom --build-arg HTTP_PROXY=http://localhost:1080 --build-arg HTTPS_PROXY=http://localhost:1080 --network=host $(dirname "${BASH_SOURCE[0]}")
