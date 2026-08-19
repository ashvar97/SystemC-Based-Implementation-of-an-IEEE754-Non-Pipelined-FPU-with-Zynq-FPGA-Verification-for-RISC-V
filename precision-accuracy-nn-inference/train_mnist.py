"""Train a small float32-baseline MLP on MNIST using the existing NN framework.

This produces the trained weights that evaluate_precision_sweep.py then runs
forward, at reduced precision, to characterize the accuracy/cost tradeoff.
Training itself is untouched full-precision numpy -- precision reduction is
applied only to the saved model's forward pass, matching how a real
deployment would train in full precision and quantize for inference.
"""

import os
import pickle
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "nn_framework"))
sys.path.insert(0, HERE)

from Layers import FullyConnected, ReLU, SoftMax, Initializers  # noqa: E402
from Optimization import Optimizers, Loss  # noqa: E402
from NeuralNetwork import NeuralNetwork  # noqa: E402

from data_loader import load_mnist, MiniBatchDataLayer, one_hot  # noqa: E402


def build_network(learning_rate=5e-3):
    optimizer = Optimizers.Adam(learning_rate, 0.9, 0.999)
    weights_init = Initializers.He()
    bias_init = Initializers.Constant(0.0)
    net = NeuralNetwork(optimizer, weights_init, bias_init)

    net.append_layer(FullyConnected.FullyConnected(784, 128))
    net.append_layer(ReLU.ReLU())
    net.append_layer(FullyConnected.FullyConnected(128, 64))
    net.append_layer(ReLU.ReLU())
    net.append_layer(FullyConnected.FullyConnected(64, 10))
    net.append_layer(SoftMax.SoftMax())
    net.loss_layer = Loss.CrossEntropyLoss()
    return net


def evaluate_accuracy(net, X, y):
    net.phase = "test"
    pred = net.test(X)
    return float((pred.argmax(axis=1) == y).mean())


def main():
    data_dir = os.path.join(HERE, "data")
    n_train, n_test = 20000, 5000
    batch_size = 64
    iterations = 6000

    print(f"Loading MNIST (train={n_train}, test={n_test}) ...")
    X_train, y_train, X_test, y_test = load_mnist(data_dir, n_train=n_train, n_test=n_test)

    net = build_network()
    net.data_layer = MiniBatchDataLayer(X_train, y_train, batch_size)

    print(f"Training MLP (784-128-64-10) for {iterations} iterations, batch_size={batch_size} ...")
    t0 = time.time()
    net.train(iterations)
    elapsed = time.time() - t0
    print(f"Done in {elapsed:.1f}s. Final training loss: {net.loss[-1]:.4f}")

    train_acc = evaluate_accuracy(net, X_train[:2000], y_train[:2000])
    test_acc = evaluate_accuracy(net, X_test, y_test)
    print(f"Train accuracy (subset): {train_acc:.4f}")
    print(f"Test accuracy:           {test_acc:.4f}")

    ckpt_dir = os.path.join(HERE, "checkpoints")
    os.makedirs(ckpt_dir, exist_ok=True)
    ckpt_path = os.path.join(ckpt_dir, "mlp_mnist_float32.pkl")

    # Save the trained weights/biases as plain float32 numpy arrays, plus
    # architecture metadata -- decoupled from the framework's own layer
    # objects/optimizers, so the precision sweep only depends on numbers.
    layers = [l for l in net.layers if getattr(l, "trainable", False)]
    state = {
        "layer_sizes": [(l.input_size, l.output_size) for l in layers],
        "weights": [l.weights.astype(np.float32) for l in layers],
        "biases": [l.bias.astype(np.float32) for l in layers],
        "test_accuracy_float32": test_acc,
        "loss_history": net.loss,
    }
    with open(ckpt_path, "wb") as f:
        pickle.dump(state, f)
    print(f"Saved checkpoint to {ckpt_path}")


if __name__ == "__main__":
    main()
