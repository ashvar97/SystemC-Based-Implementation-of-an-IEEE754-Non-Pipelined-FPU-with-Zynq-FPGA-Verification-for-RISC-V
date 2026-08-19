"""Per-layer (mixed) precision vs. uniform precision, at matched total bit
budgets.

evaluate_precision_sweep.py gives every layer of the MLP the same numeric
format. This script asks: at a fixed *total* number of bits spent across the
network, is it better to spend them evenly, or to route more bits to
whichever layers are actually loss-sensitive and don't already need most of
their bits just to cover their activation range?

Per-layer stats (precision/sensitivity.py):
  - sensitivity_i = mean(|dLoss/d(layer_i output)|), from one real
    forward+backward pass over test data (softmax+cross-entropy gradient,
    backprop through ReLU/FC by hand).
  - range_i = observed max(|value|) of layer i's own weights and own output
    activations (replacing the single global WEIGHT_INT_BITS/ACT_INT_BITS
    guess used in evaluate_precision_sweep.py with a per-layer measurement).

For a given total_bits_budget (summed over all 3 layers), two allocations
are compared at that identical budget:
  - "uniform": total_bits_budget // 3 bits to every layer.
  - "mixed": precision/sensitivity.allocate_mixed_precision() -- reserves
    each layer's own required integer bits, then hands out the remaining
    fractional bits proportional to normalized sensitivity.
"""

import csv
import os
import pickle
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from precision.formats import FixedFormat, quantize, relative_mac_cost  # noqa: E402
from precision.sensitivity import compute_layer_stats, allocate_mixed_precision, int_bits_for_range  # noqa: E402
from data_loader import load_mnist, one_hot  # noqa: E402


def forward_quantized_per_layer(X, weights, biases, w_fmts, a_fmts):
    a = X  # input is fed at whatever precision the first layer's activation format specifies
    a = quantize(a, a_fmts[0])
    n = len(weights)
    for i, (w, b) in enumerate(zip(weights, biases)):
        wq = quantize(w, w_fmts[i])
        bq = quantize(b, w_fmts[i])
        z = a @ wq + bq
        z = quantize(z, a_fmts[i])
        a = np.maximum(0.0, z) if i < n - 1 else z
        if i < n - 1:
            a = quantize(a, a_fmts[i])
    return a


def mac_weighted_cost(weights, w_fmts, a_fmts):
    mac_counts = [w.shape[0] * w.shape[1] for w in weights]
    total_macs = sum(mac_counts)
    cost = 0.0
    for macs, wf, af in zip(mac_counts, w_fmts, a_fmts):
        per_mac_cost = 0.5 * (relative_mac_cost(wf) + relative_mac_cost(af))
        cost += (macs / total_macs) * per_mac_cost
    return cost


def formats_from_bits(per_layer_bits, weight_ranges, activation_ranges):
    w_fmts, a_fmts = [], []
    for bits, w_range, a_range in zip(per_layer_bits, weight_ranges, activation_ranges):
        w_int = int_bits_for_range(w_range)
        a_int = int_bits_for_range(a_range)
        w_fmts.append(FixedFormat(f"w{bits}", bits, max(1, bits - w_int)))
        a_fmts.append(FixedFormat(f"a{bits}", bits, max(1, bits - a_int)))
    return w_fmts, a_fmts


def main():
    ckpt_path = os.path.join(HERE, "checkpoints", "mlp_mnist_float32.pkl")
    with open(ckpt_path, "rb") as f:
        state = pickle.load(f)
    weights, biases = state["weights"], state["biases"]

    data_dir = os.path.join(HERE, "data")
    X_train, y_train, X_test, y_test = load_mnist(data_dir, n_train=2000, n_test=5000)

    # Sensitivity/range stats from a batch of real (held-out-from-test) data
    stats = compute_layer_stats(weights, biases, X_train, one_hot(y_train))
    print("Per-layer stats (measured on 2000 training images):")
    for i, (s, wr, ar) in enumerate(zip(stats["sensitivity"], stats["weight_range"], stats["activation_range"])):
        print(f"  layer {i}: sensitivity={s:.5f}  weight_range=+-{wr:.2f}  activation_range=+-{ar:.2f}")

    def run(bits_per_layer):
        w_fmts, a_fmts = formats_from_bits(bits_per_layer, stats["weight_range"], stats["activation_range"])
        logits = forward_quantized_per_layer(X_test, weights, biases, w_fmts, a_fmts)
        acc = float((logits.argmax(axis=1) == y_test).mean())
        cost = mac_weighted_cost(weights, w_fmts, a_fmts)
        return acc, cost

    # A fine-grained uniform curve (actual total bits = 3*b for b=2..24) so
    # every mixed-precision point can be compared against uniform at its
    # *actual* (not nominal/requested) total bit count -- allocate_mixed_precision
    # has its own floor (each layer needs enough integer bits to cover its own
    # measured range) and can end up using more bits than requested at tight
    # budgets, so comparing by requested budget alone would be misleading.
    uniform_curve = {}
    for b in range(2, 25):
        acc, cost = run([b, b, b])
        uniform_curve[3 * b] = {"accuracy": acc, "mac_cost": cost, "bits_per_layer": [b, b, b]}

    rows = []
    for total_budget in [9, 12, 15, 18, 21, 24, 27, 30, 36, 45, 60]:
        alloc = allocate_mixed_precision(stats, total_budget)
        mixed_bits = [tb for (_, _, tb) in alloc]
        actual_total = sum(mixed_bits)
        acc_m, cost_m = run(mixed_bits)

        # uniform at the SAME actual total bit count mixed ended up using
        # (nearest multiple of 3 at or below actual_total, since uniform can
        # only split evenly)
        matched_uniform_total = (actual_total // 3) * 3
        u = uniform_curve.get(matched_uniform_total)
        if u is None:
            acc_u, cost_u = run([actual_total // 3] * 3)
        else:
            acc_u, cost_u = u["accuracy"], u["mac_cost"]

        rows.append({
            "requested_budget": total_budget,
            "mixed_bits_per_layer": str(mixed_bits),
            "mixed_actual_total_bits": actual_total,
            "mixed_accuracy": acc_m,
            "mixed_mac_cost": cost_m,
            "uniform_bits_per_layer": str([matched_uniform_total // 3] * 3),
            "uniform_accuracy": acc_u,
            "uniform_mac_cost": cost_u,
            "delta_acc_at_matched_bits": acc_m - acc_u,
        })
        print(f"requested={total_budget:3d}  mixed={mixed_bits} (actual_total={actual_total:3d}) acc={acc_m:.4f}   "
              f"vs uniform={[matched_uniform_total // 3] * 3} (total={matched_uniform_total:3d}) acc={acc_u:.4f}   "
              f"delta={acc_m - acc_u:+.4f}")

    results_dir = os.path.join(HERE, "results")
    os.makedirs(results_dir, exist_ok=True)
    csv_path = os.path.join(results_dir, "mixed_precision_sweep.csv")
    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    print(f"\nSaved results to {csv_path}")

    try:
        plot_results(rows, results_dir)
    except Exception as e:
        print(f"(plotting skipped: {e})")


def plot_results(rows, results_dir):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    totals = [r["mixed_actual_total_bits"] for r in rows]
    acc_u = [r["uniform_accuracy"] for r in rows]
    acc_m = [r["mixed_accuracy"] for r in rows]

    fig, ax = plt.subplots(figsize=(8, 5.5))
    ax.plot(totals, acc_u, "o-", color="tab:blue", label="uniform, same actual total bits")
    ax.plot(totals, acc_m, "s-", color="tab:green", label="mixed (sensitivity-weighted)")
    ax.set_xlabel("Actual total fixed-point bits used (summed across 3 layers)")
    ax.set_ylabel("Test accuracy")
    ax.set_title("Per-layer precision allocation, compared at matched actual bit counts")
    ax.legend(fontsize=9, loc="lower right")
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    out_path = os.path.join(results_dir, "mixed_precision.png")
    fig.savefig(out_path, dpi=150)
    print(f"Saved plot to {out_path}")


if __name__ == "__main__":
    main()
