"""
Reduced-precision numeric formats for neural network inference.

This module simulates arbitrary custom floating-point formats (configurable
exponent/mantissa bit widths, IEEE-754-style layout) and fixed-point formats,
and quantizes numpy arrays into them. Values are always stored back in a
float64/float32 numpy container, but their bit patterns are constrained to
exactly what the target format could represent -- this is the standard
"simulated quantization" technique used to study precision/accuracy tradeoffs
without needing a real reduced-width ALU.

The bit-manipulation here (sign/exponent/mantissa extraction, round-to-nearest-
even on the mantissa, exponent clamping/flushing) mirrors the same field
structure implemented at the hardware level in the IEEE-754 FPU elsewhere in
this repository (see ../rtl-systemc/fpu and ../rtl-systemverilog).
"""

from dataclasses import dataclass
import numpy as np


# ---------------------------------------------------------------------------
# Floating-point formats: custom (exponent_bits, mantissa_bits) IEEE-754-style
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class FloatFormat:
    name: str
    exponent_bits: int
    mantissa_bits: int  # excludes the implicit leading 1

    @property
    def bias(self) -> int:
        return (1 << (self.exponent_bits - 1)) - 1

    @property
    def max_exponent(self) -> int:
        # reserve the all-ones exponent for inf/nan, as IEEE-754 does
        return (1 << self.exponent_bits) - 2 - self.bias

    @property
    def min_normal_exponent(self) -> int:
        return 1 - self.bias

    @property
    def total_bits(self) -> int:
        return 1 + self.exponent_bits + self.mantissa_bits


# Named formats used in the sweep. float32 is the baseline (no quantization
# needed -- numpy's native float32 already *is* this format).
FLOAT32 = FloatFormat("float32", exponent_bits=8, mantissa_bits=23)
FLOAT16 = FloatFormat("float16", exponent_bits=5, mantissa_bits=10)
BFLOAT16 = FloatFormat("bfloat16", exponent_bits=8, mantissa_bits=7)
FP8_E4M3 = FloatFormat("fp8_e4m3", exponent_bits=4, mantissa_bits=3)
FP8_E5M2 = FloatFormat("fp8_e5m2", exponent_bits=5, mantissa_bits=2)


def quantize_float(x: np.ndarray, fmt: FloatFormat) -> np.ndarray:
    """Round `x` to the nearest value representable in `fmt`.

    Implemented by decomposing each float32 value into IEEE-754 sign/exponent/
    mantissa fields, rounding the mantissa to `fmt.mantissa_bits` bits
    (round-to-nearest-even), and clamping the exponent to `fmt`'s range
    (flushing to zero on underflow, saturating to the format's max magnitude
    on overflow -- no inf/nan propagation, since inference activations are
    assumed finite).
    """
    if fmt.mantissa_bits >= 23 and fmt.exponent_bits >= 8:
        return x.astype(np.float32)

    x32 = np.asarray(x, dtype=np.float32)
    bits = x32.view(np.uint32)

    sign = bits & 0x80000000
    raw_exp = ((bits >> 23) & 0xFF).astype(np.int32)
    mantissa = bits & 0x7FFFFF  # 23-bit mantissa, no implicit bit

    is_zero = raw_exp == 0
    exp = raw_exp - 127  # unbiased float32 exponent

    # --- round mantissa from 23 bits down to fmt.mantissa_bits bits ---
    drop = 23 - fmt.mantissa_bits
    if drop > 0:
        half = np.uint32(1 << (drop - 1))
        rounded = (mantissa + half) >> np.uint32(drop)
        # round-to-nearest-even: if exactly halfway, round to even
        is_tie = (mantissa & np.uint32((1 << drop) - 1)) == half
        is_odd = (rounded & 1) == 1
        rounded = np.where(is_tie & is_odd, rounded - 1, rounded)
        # mantissa overflow from rounding up (e.g. 0.111.. -> 1.000) bumps exponent
        overflow = rounded >= (1 << fmt.mantissa_bits)
        rounded = np.where(overflow, 0, rounded)
        exp = np.where(overflow, exp + 1, exp)
        mantissa_q = (rounded << drop).astype(np.uint32)
    else:
        mantissa_q = mantissa

    # --- clamp exponent to the target format's representable range ---
    underflow = (exp < fmt.min_normal_exponent) & (~is_zero)
    overflow_exp = exp > fmt.max_exponent
    exp_clamped = np.clip(exp, fmt.min_normal_exponent, fmt.max_exponent)

    new_raw_exp = (exp_clamped + 127).astype(np.uint32)
    new_raw_exp = np.where(underflow, 0, new_raw_exp)      # flush-to-zero
    mantissa_q = np.where(underflow, 0, mantissa_q)
    new_raw_exp = np.where(overflow_exp, fmt.max_exponent + 127, new_raw_exp)
    mantissa_q = np.where(
        overflow_exp,
        np.uint32(((1 << fmt.mantissa_bits) - 1) << drop) if drop > 0 else np.uint32((1 << 23) - 1),
        mantissa_q,
    )
    new_raw_exp = np.where(is_zero, 0, new_raw_exp)
    mantissa_q = np.where(is_zero, 0, mantissa_q)

    new_bits = sign | (new_raw_exp.astype(np.uint32) << 23) | mantissa_q.astype(np.uint32)
    return new_bits.view(np.float32)


# ---------------------------------------------------------------------------
# Fixed-point formats: signed Qm.f, m integer bits (incl. sign), f fractional
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class FixedFormat:
    name: str
    total_bits: int
    frac_bits: int

    @property
    def int_bits(self) -> int:
        return self.total_bits - self.frac_bits

    @property
    def scale(self) -> float:
        # 2.0 ** frac_bits, generalized to negative frac_bits (a format whose
        # step size is coarser than 1 -- e.g. a very low total-bit budget
        # that has to sacrifice sub-integer precision just to cover its
        # required dynamic range).
        return float(2.0 ** self.frac_bits)

    @property
    def q_min(self) -> int:
        return -(1 << (self.total_bits - 1))

    @property
    def q_max(self) -> int:
        return (1 << (self.total_bits - 1)) - 1


def quantize_fixed(x: np.ndarray, fmt: FixedFormat) -> np.ndarray:
    """Round-and-clip `x` into a signed Qm.f fixed-point grid, returned as float."""
    scaled = np.round(np.asarray(x, dtype=np.float64) * fmt.scale)
    clipped = np.clip(scaled, fmt.q_min, fmt.q_max)
    return (clipped / fmt.scale).astype(np.float32)


# ---------------------------------------------------------------------------
# Analytical arithmetic-cost model
# ---------------------------------------------------------------------------
#
# This is a heuristic, first-order cost model, not a synthesized gate count.
# It reflects two well-known facts about arithmetic hardware:
#   1. A naive array multiplier's area/delay scales ~quadratically with
#      operand mantissa width (an MxM-bit multiply is O(M^2) partial products).
#   2. Floating-point add/multiply needs extra control logic beyond the
#      multiplier itself -- exponent alignment (shifting), normalization,
#      and rounding -- that fixed-point arithmetic of the same total width
#      does not need.
# The constants below are chosen so that float32 MAC cost = 1.0 (the baseline
# unit), and are documented, not fitted to real measurements. A natural next
# step (noted in the README) is to replace this with actual synthesis numbers
# from the pipelined FPU in this repo.

_FLOAT_CONTROL_OVERHEAD = 4.0   # relative cost of exponent align/normalize/round logic
_FIXED_CONTROL_OVERHEAD = 1.0   # fixed-point still needs an adder + saturation logic


def float_mac_cost(fmt: FloatFormat) -> float:
    multiplier_cost = (fmt.mantissa_bits + 1) ** 2  # +1 for the implicit leading bit
    exponent_cost = fmt.exponent_bits * _FLOAT_CONTROL_OVERHEAD
    return multiplier_cost + exponent_cost


def fixed_mac_cost(fmt: FixedFormat) -> float:
    return fmt.total_bits ** 2 + _FIXED_CONTROL_OVERHEAD


_FLOAT32_BASELINE_COST = float_mac_cost(FLOAT32)


def relative_mac_cost(fmt) -> float:
    """MAC cost relative to float32 == 1.0."""
    if isinstance(fmt, FloatFormat):
        return float_mac_cost(fmt) / _FLOAT32_BASELINE_COST
    if isinstance(fmt, FixedFormat):
        return fixed_mac_cost(fmt) / _FLOAT32_BASELINE_COST
    raise TypeError(f"unknown format type: {type(fmt)}")


def quantize(x: np.ndarray, fmt) -> np.ndarray:
    """Dispatch to quantize_float or quantize_fixed based on format type."""
    if isinstance(fmt, FloatFormat):
        return quantize_float(x, fmt)
    if isinstance(fmt, FixedFormat):
        return quantize_fixed(x, fmt)
    raise TypeError(f"unknown format type: {type(fmt)}")
