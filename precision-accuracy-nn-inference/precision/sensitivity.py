"""Per-layer sensitivity and dynamic-range statistics for the trained MLP,
used to allocate a fixed-point bit budget non-uniformly across layers
instead of giving every layer the same width.

Sensitivity is the standard first-order proxy: mean(|dLoss/dActivation|) at
each layer's output, from one manual forward+backward pass over a batch of
real test data. A layer with high sensitivity is one where a small
perturbation to its output activation (e.g. from quantization) moves the
loss a lot; a layer with a large dynamic range needs more integer bits just
to avoid saturating, which -- at a fixed total bit budget -- leaves fewer
bits for precision. The two numbers together are what motivate spending the
budget unevenly: give more bits to layers that are both sensitive *and*
don't otherwise need many bits for range, and fewer to layers that are
insensitive even though they have a wide range.
"""

import numpy as np


def relu(x):
    return np.maximum(0.0, x)


def forward_with_cache(X, weights, biases):
    """Manual forward pass through the 3-layer MLP, caching what backward needs."""
    cache = {"a0": X}
    a = X
    n = len(weights)
    for i, (w, b) in enumerate(zip(weights, biases)):
        z = a @ w + b
        cache[f"z{i+1}"] = z
        if i < n - 1:
            a = relu(z)
            cache[f"a{i+1}"] = a
        else:
            a = z  # logits, no activation
    return a, cache


def backward_sensitivity(logits, y_onehot, weights, cache):
    """Returns, for each layer i, mean(|dLoss/d(layer_i output)|) -- the
    layer's output-activation sensitivity -- using the combined
    softmax+cross-entropy gradient (p - y) at the output, then standard
    backprop through ReLU/FC layers."""
    n = len(weights)
    # softmax(logits), computed stably
    shifted = logits - logits.max(axis=1, keepdims=True)
    exp = np.exp(shifted)
    p = exp / exp.sum(axis=1, keepdims=True)
    d_out = p - y_onehot  # dL/dz_n (per-sample; loss is summed over the batch upstream)

    sensitivities = [None] * n
    sensitivities[n - 1] = float(np.abs(d_out).mean())

    d_act = d_out
    for i in reversed(range(n - 1)):
        d_a = d_act @ weights[i + 1].T          # dL/d(a_i) via the next layer's weights
        z_i = cache[f"z{i+1}"]
        d_act = d_a * (z_i > 0)                  # dL/d(z_i), through ReLU'
        sensitivities[i] = float(np.abs(d_a).mean())

    return sensitivities


def layer_ranges(cache, weights, n_layers):
    """max(|value|) observed for each layer's weights and output activations."""
    weight_ranges = [float(np.abs(w).max()) for w in weights]
    act_ranges = []
    for i in range(n_layers):
        key = f"a{i+1}" if i < n_layers - 1 else f"z{i+1}"  # last layer has no ReLU
        act_ranges.append(float(np.abs(cache[key]).max()))
    return weight_ranges, act_ranges


def compute_layer_stats(weights, biases, X, y_onehot):
    logits, cache = forward_with_cache(X, weights, biases)
    sens = backward_sensitivity(logits, y_onehot, weights, cache)
    w_range, a_range = layer_ranges(cache, weights, len(weights))
    return {
        "sensitivity": sens,
        "weight_range": w_range,
        "activation_range": a_range,
    }


def int_bits_for_range(max_abs_value: float, headroom_bits: int = 0) -> int:
    """Minimum signed integer bits to cover +-max_abs_value, plus headroom."""
    if max_abs_value <= 0:
        return 1
    return int(np.ceil(np.log2(max_abs_value + 1e-9))) + 1 + headroom_bits


def allocate_mixed_precision(stats, total_bits_budget: int, min_frac_bits: int = 1):
    """Distributes `total_bits_budget` (summed across all layers) across
    layers proportionally to normalized sensitivity, after reserving each
    layer's required integer bits (from its activation range) plus a fixed
    minimum fractional-bit floor. Returns a list of (int_bits, frac_bits,
    total_bits) per layer.
    """
    n = len(stats["sensitivity"])
    int_bits = [int_bits_for_range(r) for r in stats["activation_range"]]
    floor_total = sum(int_bits) + n * min_frac_bits
    extra = max(0, total_bits_budget - floor_total)

    sens = np.array(stats["sensitivity"])
    weights = sens / sens.sum() if sens.sum() > 0 else np.ones(n) / n

    extra_alloc = np.floor(weights * extra).astype(int)
    # distribute any leftover bits (from flooring) to the most sensitive layers
    leftover = extra - extra_alloc.sum()
    order = np.argsort(-weights)
    for i in range(leftover):
        extra_alloc[order[i % n]] += 1

    frac_bits = [min_frac_bits + int(e) for e in extra_alloc]
    total_bits = [ib + fb for ib, fb in zip(int_bits, frac_bits)]
    return list(zip(int_bits, frac_bits, total_bits))
