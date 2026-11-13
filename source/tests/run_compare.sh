#!/bin/zsh
# Compare le plugin MAZZFLATOR (SDK Akai) au moteur MPC V8 d'origine sur les 13 cas de test.
# Ne modifie rien en dehors de tests/build/.
set -e
cd "$(dirname "$0")"
SDK=../../..
mkdir -p build
clang++ -std=c++20 -O2 -Wall -Wno-unused-function -Wno-unused-parameter \
  -I "$SDK/framework" -I ../src -I reference \
  compare_v8.cpp ../src/MAZZFLATOR.cpp ../src/MazzflatorEngine.cpp -o build/compare_v8
./build/compare_v8
