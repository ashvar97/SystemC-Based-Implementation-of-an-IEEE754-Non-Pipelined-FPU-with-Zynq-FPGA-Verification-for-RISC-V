"""Calibrates the analytical cost model in formats.py against real gate
counts, synthesized from this repo's own IEEE-754 RTL (see ../synth/).

Tool: yosys 0.68 (open-source), `synth` generic-cell flow + `abc` technology
mapping to yosys's internal cell library. This is technology-independent (no
foundry/FPGA process node), so the numbers are a real, structural gate-count
proxy for relative hardware cost -- not an absolute area in um^2. Reproduce
with `../synth/run_synthesis.sh`; full log in `../results/synth_log.txt`.

Data points (float32, exponent_bits=8, mantissa_bits=23):
  - FloatingPointMultiplier (24x24-bit mantissa multiplier core): 3611 cells
  - ieee754_adder (extract + align + 25-bit add + normalize):     2467 cells

These map onto the two terms of `float_mac_cost` in formats.py:
  - multiplier_cost = (mantissa_bits + 1)**2   -> real proxy: multiplier core
  - exponent_bits * FLOAT_CONTROL_OVERHEAD     -> real proxy: adder pipeline
    (the adder's own arithmetic is a cheap linear add; nearly all of its cost
    is the alignment shifter + leading-zero-count normalizer, i.e. exactly
    the float-specific control logic FLOAT_CONTROL_OVERHEAD is modeling)
"""

from dataclasses import dataclass

from precision.formats import FLOAT32, float_mac_cost, _FLOAT_CONTROL_OVERHEAD  # noqa: E402


@dataclass(frozen=True)
class SynthDataPoint:
    module: str
    description: str
    cells: int


MULTIPLIER_CORE = SynthDataPoint("FloatingPointMultiplier", "24x24-bit mantissa multiplier core", 3611)
MULTIPLIER_FULL = SynthDataPoint("ieee754mult", "extract + multiply + normalize", 3755)
ADDER_CORE = SynthDataPoint("ieee754_adder_core", "alignment shift + 25-bit add, pre-normalize", 1868)
NORMALIZER = SynthDataPoint("ieee754_normalizer", "leading-zero-count + shift", 585)
ADDER_FULL = SynthDataPoint("ieee754_adder", "extract + align + add + normalize", 2467)


def naive_model_prediction():
    """What formats.py's original hand-picked constant predicted for float32."""
    mult_term = (FLOAT32.mantissa_bits + 1) ** 2
    exp_term = FLOAT32.exponent_bits * _FLOAT_CONTROL_OVERHEAD
    return mult_term, exp_term, exp_term / mult_term


def calibrated_overhead_constant():
    """Solve for the FLOAT_CONTROL_OVERHEAD-per-exponent-bit constant that
    makes (exponent_bits * constant) / multiplier_term match the real
    ADDER_FULL/MULTIPLIER_CORE cell-count ratio at float32's (E=8, M=23)."""
    mult_term = (FLOAT32.mantissa_bits + 1) ** 2  # = 576, our proxy for MULTIPLIER_CORE
    real_ratio = ADDER_FULL.cells / MULTIPLIER_CORE.cells
    target_exp_term = real_ratio * mult_term
    calibrated_constant = target_exp_term / FLOAT32.exponent_bits
    return calibrated_constant, real_ratio


def summary() -> str:
    mult_term, exp_term, naive_ratio = naive_model_prediction()
    calibrated_constant, real_ratio = calibrated_overhead_constant()
    lines = [
        "Real synthesis (yosys, generic cells) at float32 (E=8, M=23):",
        f"  multiplier core  : {MULTIPLIER_CORE.cells:5d} cells  ({MULTIPLIER_CORE.description})",
        f"  full adder       : {ADDER_FULL.cells:5d} cells  ({ADDER_FULL.description})",
        f"  -> real adder/multiplier cell ratio: {real_ratio:.3f}",
        "",
        "Analytical model's original (hand-picked) prediction at the same point:",
        f"  multiplier term (mantissa+1)^2         = {mult_term}",
        f"  control term  exponent_bits * {_FLOAT_CONTROL_OVERHEAD:g}       = {exp_term:.1f}",
        f"  -> naive predicted ratio: {naive_ratio:.4f}",
        "",
        f"Naive model underestimated float control-logic overhead by "
        f"{real_ratio / naive_ratio:.1f}x relative to the multiplier.",
        f"Calibrated FLOAT_CONTROL_OVERHEAD-per-exponent-bit: {calibrated_constant:.2f} "
        f"(was {_FLOAT_CONTROL_OVERHEAD:g})",
    ]
    return "\n".join(lines)


if __name__ == "__main__":
    print(summary())
