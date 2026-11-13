#!/bin/sh
# Regenerate Teletype's ragel parsers after editing match_token.rl / scanner.rl (brew install ragel)
set -e
cd "$(dirname "$0")/../teletype/src"
ragel -C -G2 match_token.rl -o match_token.c
ragel -C -G2 scanner.rl -o scanner.c
