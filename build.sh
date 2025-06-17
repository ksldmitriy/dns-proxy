#!/bin/sh

if command -v bear >/dev/null 2>&1; then
    bear -- make all
else
    make all
fi
