"""Sweep numeric formats over the trained RNN's forward pass and compare its
accuracy/cost degradation against the MLP's (evaluate_precision_sweep.py).

The key difference from the MLP sweep: the RNN applies the *same* quantized
weights T=20 times in a row, quantizing the hidden state after every step.
Any per-step rounding error can compound across those 20 recurrent
applications, instead of being "diluted" across 3 independent feedforward
layers the way it is in the MLP. This script uses the same format list as
evaluate_precision_sweep.py so the two accuracy-vs-cost curves are directly
comparable on one plot.
"""

import csv
import os
import pickle
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from precision.formats import quantize, relative_mac_cost  # noqa: E402
from evaluate_precision_sweep import build_format_sweep, FixedFormat, WEIGHT_INT_BITS  # noqa: E402
from parity_task import sample_batch  # noqa: E402

# The RNN's hidden state is a bounded tanh output (always in [-1, 1]), unlike
# the MLP's unbounded ReLU activations -- so it needs far fewer integer bits.
RNN_ACT_INT_BITS = 1


def forward_quantized_rnn(X, rnn_w, rnn_b, fc_w, fc_b, fmt):
    """X: (batch, T, 1). Returns logits (batch, 2)."""
    batch, T, input_size = X.shape
    hidden_size = rnn_w.shape[1]
    h = np.zeros((batch, hidden_size), dtype=np.float32)
    for t in range(T):
        x_t = quantize(X[:, t, :], fmt)
        concat = np.concatenate([x_t, h], axis=1)
        pre = concat @ rnn_w + rnn_b
        h = np.tanh(pre)
        h = quantize(h, fmt)  # <- re-quantized every single timestep
    logits = h @ fc_w + fc_b
    return quantize(logits, fmt)


def forward_quantized_rnn_fixed(X, rnn_w, rnn_b, fc_w, fc_b, w_fmt, a_fmt):
    batch, T, input_size = X.shape
    hidden_size = rnn_w.shape[1]
    h = np.zeros((batch, hidden_size), dtype=np.float32)
    for t in range(T):
        x_t = quantize(X[:, t, :], a_fmt)
        concat = np.concatenate([x_t, h], axis=1)
        pre = concat @ rnn_w + rnn_b
        h = np.tanh(pre)
        h = quantize(h, a_fmt)
    logits = h @ fc_w + fc_b
    return quantize(logits, a_fmt)


def evaluate_float_or_named(X, y, rnn_w, rnn_b, fc_w, fc_b, fmt):
    rnn_w_q, rnn_b_q = quantize(rnn_w, fmt), quantize(rnn_b, fmt)
    fc_w_q, fc_b_q = quantize(fc_w, fmt), quantize(fc_b, fmt)
    logits = forward_quantized_rnn(X, rnn_w_q, rnn_b_q, fc_w_q, fc_b_q, fmt)
    acc = float((logits.argmax(axis=1) == y).mean())
    return acc, relative_mac_cost(fmt), relative_mac_cost(fmt, calibrated=True)


def evaluate_fixed(X, y, rnn_w, rnn_b, fc_w, fc_b, total_bits):
    w_fmt = FixedFormat(f"w_q{total_bits}", total_bits, total_bits - WEIGHT_INT_BITS)
    a_fmt = FixedFormat(f"a_q{total_bits}", total_bits, total_bits - RNN_ACT_INT_BITS)

    rnn_w_q, rnn_b_q = quantize(rnn_w, w_fmt), quantize(rnn_b, w_fmt)
    fc_w_q, fc_b_q = quantize(fc_w, w_fmt), quantize(fc_b, w_fmt)
    logits = forward_quantized_rnn_fixed(X, rnn_w_q, rnn_b_q, fc_w_q, fc_b_q, w_fmt, a_fmt)
    acc = float((logits.argmax(axis=1) == y).mean())
    cost = 0.5 * (relative_mac_cost(w_fmt) + relative_mac_cost(a_fmt))
    cost_cal = 0.5 * (relative_mac_cost(w_fmt, calibrated=True) + relative_mac_cost(a_fmt, calibrated=True))
    return acc, cost, cost_cal


def main():
    ckpt_path = os.path.join(HERE, "checkpoints", "rnn_parity_float32.pkl")
    with open(ckpt_path, "rb") as f:
        state = pickle.load(f)
    rnn_w, rnn_b = state["rnn_weights"], state["rnn_bias"]
    fc_w, fc_b = state["fc_weights"], state["fc_bias"]
    seq_len = state["seq_len"]

    rng = np.random.default_rng(999)  # yet another disjoint seed
    X_test, y_test = sample_batch(4000, seq_len, rng)
    print(f"Evaluating on {len(X_test)} sequences (seq_len={seq_len}). "
          f"float32 baseline accuracy from training: {state['test_accuracy_float32']:.4f}")

    rows = []
    for label, spec, family in build_format_sweep():
        if family == "fixed":
            acc, cost, cost_cal = evaluate_fixed(X_test, y_test, rnn_w, rnn_b, fc_w, fc_b, spec)
        else:
            acc, cost, cost_cal = evaluate_float_or_named(X_test, y_test, rnn_w, rnn_b, fc_w, fc_b, spec)
        rows.append({
            "format": label, "family": family, "accuracy": acc,
            "relative_mac_cost": cost, "relative_mac_cost_calibrated": cost_cal,
        })
        print(f"{label:20s} family={family:20s} accuracy={acc:.4f}  cost(naive)={cost:.4f}")

    results_dir = os.path.join(HERE, "results")
    os.makedirs(results_dir, exist_ok=True)
    csv_path = os.path.join(results_dir, "rnn_precision_sweep.csv")
    fieldnames = ["format", "family", "accuracy", "relative_mac_cost", "relative_mac_cost_calibrated"]
    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
    print(f"\nSaved results to {csv_path}")

    try:
        plot_mlp_vs_rnn(rows, results_dir)
    except Exception as e:
        print(f"(plotting skipped: {e})")


def plot_mlp_vs_rnn(rnn_rows, results_dir):
    import csv as csv_mod
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    mlp_csv = os.path.join(results_dir, "precision_sweep.csv")
    if not os.path.exists(mlp_csv):
        print("(precision_sweep.csv not found -- run evaluate_precision_sweep.py first "
              "for the MLP-vs-RNN comparison plot)")
        return
    with open(mlp_csv) as f:
        mlp_rows = list(csv_mod.DictReader(f))
    for r in mlp_rows:
        r["accuracy"] = float(r["accuracy"])
        r["relative_mac_cost"] = float(r["relative_mac_cost"])

    fig, ax = plt.subplots(figsize=(8, 5.5))

    def curve(rows, family, **kw):
        pts = sorted([r for r in rows if r["family"] == family], key=lambda r: r["relative_mac_cost"])
        ax.plot([r["relative_mac_cost"] for r in pts], [r["accuracy"] for r in pts], "o-", **kw)

    curve(mlp_rows, "float_mantissa_sweep", color="tab:blue", label="MLP / MNIST (feedforward, 3 layers)")
    curve(rnn_rows, "float_mantissa_sweep", color="tab:red", label="RNN / majority-vote (recurrent, 20 steps)")

    ax.set_xlabel("Estimated relative MAC cost (float32 = 1.0)")
    ax.set_ylabel("Test accuracy")
    ax.set_title("Precision sensitivity: feedforward (MLP) vs. recurrent (RNN)")
    ax.set_ylim(0.0, 1.02)
    ax.legend(fontsize=9, loc="lower right")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    out_path = os.path.join(results_dir, "mlp_vs_rnn_precision.png")
    fig.savefig(out_path, dpi=150)
    print(f"Saved plot to {out_path}")


if __name__ == "__main__":
    main()
