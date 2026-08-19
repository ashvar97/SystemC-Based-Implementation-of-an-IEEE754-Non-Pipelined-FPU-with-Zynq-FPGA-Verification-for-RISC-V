"""Binary majority vote: a recurrent stress-test with none of MNIST's
redundancy.

Given a length-T sequence of random bits, output whether more than half are
1. Unlike classifying a static MNIST digit (3 feedforward layers, each
applied once), this requires the network to carry a running accumulator
through T recurrent steps -- the *same* weights are applied T times in a
row, so any per-step quantization error has T chances to compound rather
than being averaged out over a handful of independent layers. That's the
"less redundant, precision loss should bite harder" task called for in the
README's extensions.

(Exact parity -- even/odd count of 1s -- was tried first and is a famously
hard task for a vanilla RNN: it needs precise mod-2 counting through a tanh
nonlinearity and a plain Elman RNN doesn't learn it much above chance even
in float32. Majority vote keeps the "accumulate over T steps" property that
matters for this experiment while actually being learnable.)

Ground truth is exact and free to generate (no dataset needed): sequences
are sampled on the fly, so there's no train/test leakage to worry about.
"""

import numpy as np


def sample_batch(batch_size: int, seq_len: int, rng: np.random.Generator):
    bits = rng.integers(0, 2, size=(batch_size, seq_len)).astype(np.float32)
    majority = (bits.sum(axis=1) > seq_len / 2).astype(np.int64)
    X = bits[:, :, np.newaxis]  # (batch, seq_len, 1) -- one scalar bit per timestep
    return X, majority


def one_hot(y: np.ndarray, n_classes: int = 2) -> np.ndarray:
    out = np.zeros((len(y), n_classes), dtype=np.float32)
    out[np.arange(len(y)), y] = 1.0
    return out


class ParityDataLayer:
    """`data_layer`-compatible: exposes `.next()` -> (batch_X, batch_y_onehot),
    generating a fresh random batch every call (no fixed dataset/epochs)."""

    def __init__(self, batch_size: int, seq_len: int, seed: int = 0):
        self.batch_size = batch_size
        self.seq_len = seq_len
        self.rng = np.random.default_rng(seed)

    def next(self):
        X, y = sample_batch(self.batch_size, self.seq_len, self.rng)
        return X, one_hot(y)
