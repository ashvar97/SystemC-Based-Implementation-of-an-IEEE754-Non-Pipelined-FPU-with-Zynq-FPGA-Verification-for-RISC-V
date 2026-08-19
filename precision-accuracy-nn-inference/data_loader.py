"""Download (if needed) and load MNIST as flat float32 numpy arrays."""

import gzip
import os
import ssl
import urllib.request

import numpy as np

try:
    import certifi
    _SSL_CONTEXT = ssl.create_default_context(cafile=certifi.where())
except ImportError:
    _SSL_CONTEXT = None

_BASE_URL = "https://ossci-datasets.s3.amazonaws.com/mnist/"
_FILES = {
    "train_images": "train-images-idx3-ubyte.gz",
    "train_labels": "train-labels-idx1-ubyte.gz",
    "test_images": "t10k-images-idx3-ubyte.gz",
    "test_labels": "t10k-labels-idx1-ubyte.gz",
}


def _download(data_dir: str) -> None:
    os.makedirs(data_dir, exist_ok=True)
    for fname in _FILES.values():
        path = os.path.join(data_dir, fname)
        if not os.path.exists(path):
            print(f"Downloading {fname} ...")
            with urllib.request.urlopen(_BASE_URL + fname, context=_SSL_CONTEXT) as resp:
                with open(path, "wb") as out:
                    out.write(resp.read())


def _read_idx_images(path: str) -> np.ndarray:
    with gzip.open(path, "rb") as f:
        data = f.read()
    magic = int.from_bytes(data[0:4], "big")
    assert magic == 2051, f"bad magic number for images: {magic}"
    n = int.from_bytes(data[4:8], "big")
    rows = int.from_bytes(data[8:12], "big")
    cols = int.from_bytes(data[12:16], "big")
    arr = np.frombuffer(data, dtype=np.uint8, offset=16)
    return arr.reshape(n, rows * cols).astype(np.float32)


def _read_idx_labels(path: str) -> np.ndarray:
    with gzip.open(path, "rb") as f:
        data = f.read()
    magic = int.from_bytes(data[0:4], "big")
    assert magic == 2049, f"bad magic number for labels: {magic}"
    n = int.from_bytes(data[4:8], "big")
    arr = np.frombuffer(data, dtype=np.uint8, offset=8)
    return arr.reshape(n).astype(np.int64)


def load_mnist(data_dir: str, n_train: int = None, n_test: int = None, seed: int = 0):
    """Returns (X_train, y_train, X_test, y_test).

    X_* are float32 arrays of shape (N, 784), scaled to [0, 1].
    y_* are int64 label arrays of shape (N,).
    n_train / n_test optionally subsample (with a fixed seed) for faster
    experimentation -- the precision-sweep is about the forward pass, not
    about squeezing out the last bit of MNIST accuracy.
    """
    _download(data_dir)
    X_train = _read_idx_images(os.path.join(data_dir, _FILES["train_images"])) / 255.0
    y_train = _read_idx_labels(os.path.join(data_dir, _FILES["train_labels"]))
    X_test = _read_idx_images(os.path.join(data_dir, _FILES["test_images"])) / 255.0
    y_test = _read_idx_labels(os.path.join(data_dir, _FILES["test_labels"]))

    rng = np.random.default_rng(seed)
    if n_train is not None and n_train < len(X_train):
        idx = rng.choice(len(X_train), size=n_train, replace=False)
        X_train, y_train = X_train[idx], y_train[idx]
    if n_test is not None and n_test < len(X_test):
        idx = rng.choice(len(X_test), size=n_test, replace=False)
        X_test, y_test = X_test[idx], y_test[idx]

    return X_train, y_train, X_test, y_test


def one_hot(y: np.ndarray, n_classes: int = 10) -> np.ndarray:
    out = np.zeros((len(y), n_classes), dtype=np.float32)
    out[np.arange(len(y)), y] = 1.0
    return out


class MiniBatchDataLayer:
    """A `data_layer` compatible with nn_framework.NeuralNetwork: exposes
    `.next()` returning one (batch_X, batch_y_onehot) pair, cycling through
    the training set each epoch with a reshuffle."""

    def __init__(self, X: np.ndarray, y: np.ndarray, batch_size: int, n_classes: int = 10, seed: int = 0):
        self.X = X
        self.y_onehot = one_hot(y, n_classes)
        self.batch_size = batch_size
        self.rng = np.random.default_rng(seed)
        self._order = self.rng.permutation(len(X))
        self._pos = 0

    def next(self):
        if self._pos + self.batch_size > len(self.X):
            self._order = self.rng.permutation(len(self.X))
            self._pos = 0
        idx = self._order[self._pos:self._pos + self.batch_size]
        self._pos += self.batch_size
        return self.X[idx], self.y_onehot[idx]
