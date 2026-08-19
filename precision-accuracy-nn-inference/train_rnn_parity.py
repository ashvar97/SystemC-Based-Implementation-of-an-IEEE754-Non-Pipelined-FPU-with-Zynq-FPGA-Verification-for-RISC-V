"""Train an RNN(1->32) + FC(32->2) network on binary parity, float32 baseline."""

import os
import pickle
import sys
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "nn_framework"))
sys.path.insert(0, HERE)

from Layers import FullyConnected, RNN, SoftMax, Initializers  # noqa: E402
from Optimization import Optimizers, Loss  # noqa: E402
from NeuralNetwork import NeuralNetwork  # noqa: E402

from parity_task import ParityDataLayer, sample_batch  # noqa: E402

SEQ_LEN = 20
HIDDEN_SIZE = 32


def build_network(learning_rate=2e-3):
    optimizer = Optimizers.Adam(learning_rate, 0.9, 0.999)
    weights_init = Initializers.Xavier()
    bias_init = Initializers.Constant(0.0)
    net = NeuralNetwork(optimizer, weights_init, bias_init)

    net.append_layer(RNN.RNN(input_size=1, hidden_size=HIDDEN_SIZE))
    net.append_layer(FullyConnected.FullyConnected(HIDDEN_SIZE, 2))
    net.append_layer(SoftMax.SoftMax())
    net.loss_layer = Loss.CrossEntropyLoss()
    return net


def evaluate_accuracy(net, X, y):
    net.phase = "test"
    pred = net.test(X)
    return float((pred.argmax(axis=1) == y).mean())


def main():
    batch_size = 64
    iterations = 4000

    net = build_network()
    net.data_layer = ParityDataLayer(batch_size, SEQ_LEN, seed=0)

    print(f"Training RNN(1->{HIDDEN_SIZE}) + FC({HIDDEN_SIZE}->2) on parity, "
          f"seq_len={SEQ_LEN}, for {iterations} iterations ...")
    t0 = time.time()
    net.train(iterations)
    elapsed = time.time() - t0
    print(f"Done in {elapsed:.1f}s. Final training loss: {net.loss[-1]:.4f}")

    rng = np.random.default_rng(12345)  # held-out seed, disjoint from training stream
    X_test, y_test = sample_batch(4000, SEQ_LEN, rng)
    test_acc = evaluate_accuracy(net, X_test, y_test)
    print(f"Test accuracy (fresh random sequences): {test_acc:.4f}")

    ckpt_dir = os.path.join(HERE, "checkpoints")
    os.makedirs(ckpt_dir, exist_ok=True)
    ckpt_path = os.path.join(ckpt_dir, "rnn_parity_float32.pkl")

    rnn_layer, fc_layer = net.layers[0], net.layers[1]
    state = {
        "seq_len": SEQ_LEN,
        "hidden_size": HIDDEN_SIZE,
        "rnn_weights": rnn_layer.weights.astype(np.float32),
        "rnn_bias": rnn_layer.bias.astype(np.float32),
        "fc_weights": fc_layer.weights.astype(np.float32),
        "fc_bias": fc_layer.bias.astype(np.float32),
        "test_accuracy_float32": test_acc,
        "loss_history": net.loss,
    }
    with open(ckpt_path, "wb") as f:
        pickle.dump(state, f)
    print(f"Saved checkpoint to {ckpt_path}")


if __name__ == "__main__":
    main()
