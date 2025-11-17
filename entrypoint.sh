#!/bin/bash
set -e

exec freeswitch -nc -nonat "$@"
