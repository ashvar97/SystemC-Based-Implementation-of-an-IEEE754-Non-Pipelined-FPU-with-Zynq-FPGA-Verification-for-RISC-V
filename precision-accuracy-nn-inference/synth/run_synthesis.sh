#!/usr/bin/env bash
# Reproduces the real gate-count synthesis used to calibrate precision/formats.py.
#
# Requires yosys (open-source, free): `brew install yosys` on macOS.
# Uses yosys's default technology-independent `synth` flow + `abc` mapping to
# its internal generic cell library ($_AND_, $_XOR_, ...). This is NOT tied to
# a specific foundry/FPGA process node -- it's a technology-independent proxy
# for relative hardware cost, which is exactly what the analytical cost model
# in precision/formats.py is trying to estimate. See ../results/synth_log.txt
# for the full output this script produces, and ../README.md for how the
# numbers were used.
#
# mult_group.sv / add_group.sv are extracted verbatim from
# ../../rtl-systemverilog/fpu_pipeline.sv (the FloatingPointMultiplier and
# ieee754_adder module families), with ONE documented exception: the
# leading-zero-count loop in ieee754_normalizer was rewritten from a
# data-dependent-exit `for` loop to an equivalent constant-bound priority
# search, because yosys's Verilog frontend requires a compile-time-constant
# loop bound to unroll procedural for-loops (Vivado/other tools accept the
# original form). This is a synthesizability fix only -- same combinational
# function, and the original file in ../../rtl-systemverilog is untouched.

set -euo pipefail
cd "$(dirname "$0")"
OUT=../results/synth_log.txt

{
  echo "=== yosys version ==="
  yosys -V

  echo
  echo "=== Mantissa multiplier core (FloatingPointMultiplier, 24x24-bit) ==="
  yosys -p "read_verilog -sv mult_group.sv; synth -top FloatingPointMultiplier; stat"

  echo
  echo "=== Full multiply pipeline (ieee754mult: extract + multiply + normalize) ==="
  yosys -p "read_verilog -sv mult_group.sv; synth -top ieee754mult; stat"

  echo
  echo "=== Adder alignment+add core (ieee754_adder_core, pre-normalize) ==="
  yosys -p "read_verilog -sv add_group.sv; synth -top ieee754_adder_core; stat"

  echo
  echo "=== Normalizer (ieee754_normalizer: leading-zero-count + shift) ==="
  yosys -p "read_verilog -sv add_group.sv; synth -top ieee754_normalizer; stat"

  echo
  echo "=== Full adder pipeline (ieee754_adder: extract + align + add + normalize) ==="
  yosys -p "read_verilog -sv add_group.sv; synth -top ieee754_adder; stat"
} | tee "$OUT"

echo
echo "Saved full log to $OUT"
