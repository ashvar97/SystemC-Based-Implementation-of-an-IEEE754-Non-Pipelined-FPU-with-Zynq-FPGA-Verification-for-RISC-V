# Precision-Accuracy Tradeoffs in Neural Network Inference

Investigates how reduced-precision and custom numeric formats affect inference
accuracy and estimated computational cost in a trained neural network,
building on the IEEE-754 arithmetic background elsewhere in this repository
(`../rtl-systemc/fpu`, `../rtl-systemverilog`) and on an existing NumPy neural
network framework (see `nn_framework/`, reused from a separate project).

## What this does

1. **Train once, in full precision.** `train_mnist.py` trains a small MLP
   (784-128-64-10, ReLU, softmax + cross-entropy) on MNIST using the existing
   NN framework's `FullyConnected` / `ReLU` / `SoftMax` layers and `Adam`
   optimizer, and saves the resulting weights.
2. **Quantize only the forward pass.** `evaluate_precision_sweep.py` loads
   those weights and re-runs inference under a sweep of numeric formats,
   implemented from scratch in `precision/formats.py`:
   - **Custom floating-point formats** — arbitrary (exponent_bits,
     mantissa_bits), built by decomposing float32 into IEEE-754 sign/exponent/
     mantissa fields, rounding the mantissa to the target width
     (round-to-nearest-even), and clamping/flushing the exponent to the
     target range. Includes named formats (float16, bfloat16, fp8_e4m3,
     fp8_e5m2) and a mantissa-bit sweep at fixed exponent width to isolate
     precision from range.
   - **Fixed-point formats** — signed Qm.f, quantized by scale-round-clip.
   - Weights are quantized once per format; activations are quantized after
     every layer. Matrix multiplies still accumulate in full precision,
     matching standard reduced-precision hardware practice (e.g. int8 GEMMs
     accumulate in int32) — this isolates *operand* precision, which is what
     is actually costed below.
3. **Cost model.** Each format is priced with a documented, analytical
   relative-MAC-cost estimate (`relative_mac_cost` in `precision/formats.py`):
   multiplier cost ~ (mantissa_bits+1)², plus a per-exponent-bit control-logic
   overhead for float (alignment/normalization/rounding) that fixed-point
   arithmetic doesn't need. This is a first-order estimate, not a synthesized
   gate count — see *Possible extensions* below.

## Results

Run: MLP trained to 96.7% test accuracy in float32. Full sweep in
[`results/precision_sweep.csv`](results/precision_sweep.csv), plotted in
[`results/accuracy_vs_cost.png`](results/accuracy_vs_cost.png).

- **Floating-point degrades gracefully.** Accuracy stays within ~0.1
  percentage points of the float32 baseline all the way down to fp8 formats
  (3-bit and even 2-bit mantissas), at an estimated 5-7% of float32's MAC
  cost. The exponent field lets each value track its own magnitude, so
  cutting mantissa bits only adds proportional rounding noise — which this
   (fairly redundant, large-margin) classification task absorbs easily.
- **Fixed-point holds up fine — until it doesn't.** Accuracy is
  indistinguishable from the float32 baseline from 10 bits upward, drops
  measurably at 8 bits (96.1%), and *collapses to 9.5% (near chance) at 6
  bits*. Fixed-point has no exponent to fall back on: at a small enough total
  bit budget, the same bits that would give sub-integer precision have to be
  sacrificed just to keep the integer part from saturating (weights need
  ~3 integer bits here, activations ~8, both derived empirically from the
  trained model's value ranges — see the `WEIGHT_INT_BITS`/`ACT_INT_BITS`
  constants). That coupling between range and precision, and floating-point's
  lack of it, is the direct payoff of having designed the field layout of an
  IEEE-754 unit by hand rather than treating "precision" as a hyperparameter.

## Usage

```bash
pip install -r requirements.txt
python3 train_mnist.py              # downloads MNIST, trains, saves checkpoints/mlp_mnist_float32.pkl
python3 evaluate_precision_sweep.py # runs the format sweep, writes results/
```

## Possible extensions

- Replace the analytical cost model with real synthesis numbers (area/timing)
  from the pipelined FPU in `../rtl-systemc` / `../rtl-systemverilog`, for at
  least one or two format widths, to validate the O(mantissa²) assumption
  against actual hardware.
- Extend the sweep to the CNN/RNN/LSTM layers already in `nn_framework/` for
  a task with less redundancy than MNIST classification, where precision
  loss is expected to bite harder and sooner.
- Per-layer (rather than network-wide) precision assignment, informed by
  which layers' activation ranges are already large relative to their
  gradient sensitivity.
