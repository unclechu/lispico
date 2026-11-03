#!/usr/bin/env bash
set -o errexit || exit; set -o errtrace; set -o nounset; set -o pipefail

shopt -s nullglob
set -o xtrace
rm -vf -- lispico lispico-*-tests
