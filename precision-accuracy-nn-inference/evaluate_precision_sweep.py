"""Sweep numeric formats over the trained MLP's forward pass and measure the
accuracy vs. estimated-arithmetic-cost tradeoff.

Design notes
------------
- Training happens once, in full float32 precision (train_mnist.py). This
  script only quantizes the *inference* forward pass: weights are quantized
  once per format, activations are quantized after every layer.
- Matrix multiplies (dot products) are still accumulated in float64/numpy's
  native accumulator. This mirrors standard reduced-precision hardware
  practice (e.g. int8 GEMMs accumulate in int32, fp16 GEMMs accumulate in
  fp32): the *operands* are narrow, the *accumulator* is wide. It isolates
  the effect of operand precision, which is what the cost model below prices.
- The cost axis (`relative_mac_cost`) is a first-order analytical estimate,
  documented in precision/formats.py -- not a synthesized gate count.
"""

import csv
import os
import pickle
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from precision.formats import (  # noqa: E402
    FloatFormat, FixedFormat, quantize, relative_mac_cost,
    FLOAT32, FLOAT16, BFLOAT16, FP8_E4M3, FP8_E5M2,
)
from data_loader import load_mnist  # noqa: E402

# Fixed-point range assumptions, derived from the trained model's observed
# value ranges (see README): weights stay within roughly +-4, pre-activation
# / post-ReLU activations reach roughly +-128.
WEIGHT_INT_BITS = 3
ACT_INT_BITS = 8


def forward_quantized(X, weights, biases, fmt):
    """Run the 3-layer MLP forward pass, quantizing weights (once, already
    done by caller) and every intermediate activation to `fmt`. Returns
    logits (pre-softmax); softmax is monotonic so argmax(logits) == predicted
    class without needing to compute it."""
    a = quantize(X, fmt)
    n_layers = len(weights)
    for i, (w, b) in enumerate(zip(weights, biases)):
        z = a @ w + b
        z = quantize(z, fmt)
        if i < n_layers - 1:
            a = np.maximum(0.0, z)
            a = quantize(a, fmt)
        else:
            a = z
    return a


def quantize_weights(weights, biases, fmt):
    return [quantize(w, fmt) for w in weights], [quantize(b, fmt) for b in biases]


def build_format_sweep():
    """Returns a list of (label, format, family) tuples to evaluate."""
    sweep = []

    # Named floating-point formats
    for fmt in [FLOAT32, FLOAT16, BFLOAT16, FP8_E4M3, FP8_E5M2]:
        sweep.append((fmt.name, fmt, "float"))

    # Mantissa-bit sweep at fixed exponent width (isolates precision from range)
    for m in [2, 3, 4, 5, 6, 8, 10, 12, 16, 20]:
        fmt = FloatFormat(f"float_e8m{m}", exponent_bits=8, mantissa_bits=m)
        sweep.append((fmt.name, fmt, "float_mantissa_sweep"))

    # Fixed-point sweep at a uniform total bit-width; int_bits differ between
    # weights and activations (handled per-tensor in evaluate_fixed below),
    # so here we just sweep total_bits and build the two formats per point.
    for total_bits in [6, 8, 10, 12, 14, 16, 20, 24]:
        sweep.append((f"fixed_q{total_bits}", total_bits, "fixed"))

    return sweep


def evaluate_float_or_named(X, y, weights, biases, fmt):
    wq, bq = quantize_weights(weights, biases, fmt)
    logits = forward_quantized(X, wq, bq, fmt)
    acc = float((logits.argmax(axis=1) == y).mean())
    return acc, relative_mac_cost(fmt)


def evaluate_fixed(X, y, weights, biases, total_bits):
    w_fmt = FixedFormat(f"w_q{total_bits}", total_bits, total_bits - WEIGHT_INT_BITS)
    a_fmt = FixedFormat(f"a_q{total_bits}", total_bits, total_bits - ACT_INT_BITS)

    wq = [quantize(w, w_fmt) for w in weights]
    bq = [quantize(b, w_fmt) for b in biases]

    a = quantize(X, a_fmt)
    n_layers = len(wq)
    for i, (w, b) in enumerate(zip(wq, bq)):
        z = a @ w + b
        z = quantize(z, a_fmt)
        a = np.maximum(0.0, z) if i < n_layers - 1 else z
        if i < n_layers - 1:
            a = quantize(a, a_fmt)

    acc = float((a.argmax(axis=1) == y).mean())
    # cost: average of weight-format and activation-format MAC cost, since a
    # MAC consumes one operand of each
    cost = 0.5 * (relative_mac_cost(w_fmt) + relative_mac_cost(a_fmt))
    return acc, cost


def main():
    ckpt_path = os.path.join(HERE, "checkpoints", "mlp_mnist_float32.pkl")
    with open(ckpt_path, "rb") as f:
        state = pickle.load(f)
    weights, biases = state["weights"], state["biases"]

    data_dir = os.path.join(HERE, "data")
    _, _, X_test, y_test = load_mnist(data_dir, n_test=5000)
    print(f"Evaluating on {len(X_test)} test images. "
          f"float32 baseline accuracy from training: {state['test_accuracy_float32']:.4f}")

    rows = []
    for label, spec, family in build_format_sweep():
        if family == "fixed":
            acc, cost = evaluate_fixed(X_test, y_test, weights, biases, spec)
        else:
            acc, cost = evaluate_float_or_named(X_test, y_test, weights, biases, spec)
        rows.append({"format": label, "family": family, "accuracy": acc, "relative_mac_cost": cost})
        print(f"{label:20s} family={family:20s} accuracy={acc:.4f}  relative_mac_cost={cost:.4f}")

    results_dir = os.path.join(HERE, "results")
    os.makedirs(results_dir, exist_ok=True)
    csv_path = os.path.join(results_dir, "precision_sweep.csv")
    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["format", "family", "accuracy", "relative_mac_cost"])
        writer.writeheader()
        writer.writerows(rows)
    print(f"\nSaved results to {csv_path}")

    try:
        plot_results(rows, results_dir)
    except Exception as e:  # matplotlib backend issues shouldn't kill the run
        print(f"(plotting skipped: {e})")


def plot_results(rows, results_dir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(1, 2, figsize=(12, 5), sharey=True)

    # Plot 1: mantissa sweep (line, shows graceful float degradation) +
    # named formats (scattered markers) plotted separately for clarity.
    mant_rows = sorted(
        [r for r in rows if r["family"] == "float_mantissa_sweep"],
        key=lambda r: r["relative_mac_cost"],
    )
    named_rows = [r for r in rows if r["family"] == "float"]

    mc = [r["relative_mac_cost"] for r in mant_rows]
    ma = [r["accuracy"] for r in mant_rows]
    axes[0].plot(mc, ma, "o-", color="tab:blue", label="float, e8m* sweep")

    nc = [r["relative_mac_cost"] for r in named_rows]
    na = [r["accuracy"] for r in named_rows]
    nl = [r["format"] for r in named_rows]
    axes[0].scatter(nc, na, marker="*", s=140, color="tab:red", zorder=5, label="named formats")
    for c, a, l in zip(nc, na, nl):
        axes[0].annotate(l, (c, a), fontsize=8, xytext=(4, 4), textcoords="offset points")

    axes[0].set_xlabel("Estimated relative MAC cost (float32 = 1.0)")
    axes[0].set_ylabel("Test accuracy")
    axes[0].set_title("Floating-point formats: accuracy vs. cost")
    axes[0].legend(fontsize=8, loc="lower right")
    axes[0].grid(True, alpha=0.3)

    # Plot 2: fixed-point sweep
    fixed_rows = sorted([r for r in rows if r["family"] == "fixed"], key=lambda r: r["relative_mac_cost"])
    fc = [r["relative_mac_cost"] for r in fixed_rows]
    fa = [r["accuracy"] for r in fixed_rows]
    fl = [r["format"] for r in fixed_rows]
    axes[1].plot(fc, fa, "s-", color="darkorange")
    for c, a, l in zip(fc, fa, fl):
        axes[1].annotate(l, (c, a), fontsize=8, xytext=(4, 4), textcoords="offset points")
    axes[1].set_xlabel("Estimated relative MAC cost (float32 = 1.0)")
    axes[1].set_title("Fixed-point formats: accuracy vs. cost")
    axes[1].grid(True, alpha=0.3)

    axes[0].set_ylim(0.0, 1.02)
    fig.suptitle("Precision/accuracy tradeoff, MLP on MNIST (shared y-axis: float degrades gracefully, fixed-point cliffs)")
    fig.tight_layout()
    out_path = os.path.join(results_dir, "accuracy_vs_cost.png")
    fig.savefig(out_path, dpi=150)
    print(f"Saved plot to {out_path}")


if __name__ == "__main__":
    main()
