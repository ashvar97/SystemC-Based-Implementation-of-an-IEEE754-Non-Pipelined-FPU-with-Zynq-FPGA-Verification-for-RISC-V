import numpy as np
from Optimization import Optimizers
from Layers import Base
import copy

# NOTE ON PROVENANCE: unlike the other files in nn_framework/ (copied verbatim
# from the existing FC/CNN framework), this RNN layer was written fresh for
# this project. The original RNN.py became inaccessible mid-session (a local
# filesystem issue on the Downloads folder this repo doesn't touch), before
# it had been pulled in. It follows the same conventions as FullyConnected.py
# (weights/bias layout, optimizer wiring, initialize() signature) so it slots
# into the same NeuralNetwork/append_layer driver unchanged.


class RNN(Base.BaseLayer):
    """A vanilla Elman RNN layer, trained with full backprop-through-time.

    forward(input_tensor) expects shape (batch, T, input_size) and returns
    the final hidden state (batch, hidden_size) -- i.e. this layer is meant
    to sit at the start of a network, followed by a FullyConnected head that
    turns the final hidden state into class scores (a standard "read out only
    at the end of the sequence" architecture).
    """

    def __init__(self, input_size, hidden_size):
        super().__init__()
        self.trainable = True
        self.input_size = input_size
        self.hidden_size = hidden_size
        self.weights = np.random.uniform(size=(input_size + hidden_size, hidden_size))
        self.bias = np.random.uniform(size=(1, hidden_size))
        self.gradient_weights = None
        self.gradient_bias = None
        self._optimizer = None

    @property
    def optimizer(self):
        return self._optimizer

    @optimizer.setter
    def optimizer(self, optimizer):
        self._optimizer = optimizer
        self._optimizer.weight = copy.deepcopy(optimizer)
        self._optimizer.bias = copy.deepcopy(optimizer)

    def initialize(self, weights_initializer, bias_initializer):
        fan_in = self.input_size + self.hidden_size
        self.weights = weights_initializer.initialize(self.weights.shape, fan_in, self.hidden_size)
        self.bias = bias_initializer.initialize(self.bias.shape, 1, self.hidden_size)

    @property
    def weights(self):
        return self._weights

    @weights.setter
    def weights(self, weights):
        self._weights = weights

    def forward(self, input_tensor):
        batch, T, _ = input_tensor.shape
        h = np.zeros((batch, self.hidden_size))
        self._concat_cache = []
        self._h_cache = []
        for t in range(T):
            concat = np.concatenate([input_tensor[:, t, :], h], axis=1)
            pre = concat @ self.weights + self.bias
            h = np.tanh(pre)
            self._concat_cache.append(concat)
            self._h_cache.append(h)
        self._T = T
        return h

    def backward(self, error_tensor):
        dW = np.zeros_like(self.weights)
        db = np.zeros_like(self.bias)
        dh_next = error_tensor
        dx_seq = [None] * self._T

        for t in reversed(range(self._T)):
            h_t = self._h_cache[t]
            dpre = dh_next * (1.0 - h_t ** 2)
            dW += self._concat_cache[t].T @ dpre
            db += dpre.sum(axis=0, keepdims=True)
            dconcat = dpre @ self.weights.T
            dx_seq[t] = dconcat[:, : self.input_size]
            dh_next = dconcat[:, self.input_size :]

        if self._optimizer is not None:
            self.weights = self._optimizer.weight.calculate_update(self.weights, dW)
            self.bias = self._optimizer.bias.calculate_update(self.bias, db)

        self.gradient_weights = dW
        self.gradient_bias = db
        return np.stack(dx_seq, axis=1)
