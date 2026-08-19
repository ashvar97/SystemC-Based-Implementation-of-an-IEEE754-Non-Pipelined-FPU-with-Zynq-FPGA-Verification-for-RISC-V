# Precision-Accuracy Tradeoffs in Neural Network Inference

Investigates how reduced-precision and custom numeric formats affect inference
accuracy and estimated computational cost in trained neural networks,
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
   arithmetic doesn't need — now calibrated against real synthesized gate
   counts, see *Extension 1* below.

## Results

Run: MLP trained to 96.7% test accuracy in float32. Full sweep in
[`results/precision_sweep.csv`](results/precision_sweep.csv), plotted in
[`results/accuracy_vs_cost.png`](results/accuracy_vs_cost.png).

- **Floating-point degrades gracefully.** Accuracy stays within ~0.1
  percentage points of the float32 baseline all the way down to fp8 formats
  (3-bit and even 2-bit mantissas), at an estimated 5-7% of float32's naive
  MAC cost. The exponent field lets each value track its own magnitude, so
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
python3 train_mnist.py                    # downloads MNIST, trains, saves checkpoints/mlp_mnist_float32.pkl
python3 evaluate_precision_sweep.py       # MLP format sweep, naive + hw-calibrated cost, writes results/
python3 train_rnn_parity.py               # trains the RNN majority-vote model
python3 evaluate_rnn_precision_sweep.py   # RNN format sweep + MLP-vs-RNN comparison plot
python3 evaluate_mixed_precision.py       # per-layer vs. uniform precision allocation
./synth/run_synthesis.sh                  # reproduces the real gate-count synthesis (needs yosys)
```

## Extensions

Three follow-ups, each actually run (not just proposed):

### 1. Calibrating the cost model against real synthesized hardware

The original cost model's float control-logic constant was a hand-picked
guess. `synth/` extracts the multiplier and adder module families straight
from this repo's own RTL (`../rtl-systemverilog/fpu_pipeline.sv`) and
synthesizes them with **yosys** (open-source, generic-cell `synth`+`abc`
flow — technology-independent, not tied to a specific foundry/FPGA node, but
a real structural gate count rather than a formula). Reproduce with
`synth/run_synthesis.sh`; full log in `results/synth_log.txt`; the
calibration itself in `precision/hw_calibration.py`.

Real gate counts at float32 (E=8, M=23):

| module | what it is | cells |
|---|---|---|
| `FloatingPointMultiplier` | 24×24-bit mantissa multiplier core | 3611 |
| `ieee754_adder` | extract + align + 25-bit add + normalize | 2467 |

The naive model predicted the adder-equivalent control logic would cost
only **5.6%** of the multiplier's cost (`exponent_bits × 4.0`); real
synthesis puts it at **68.3%** — a **12.3× underestimate**. Alignment
shifting and leading-zero-count normalization are far from the "small
extra logic" a first guess assumed; they're comparable in size to the
multiplier itself. `precision/formats.py` now carries both constants
(`relative_mac_cost(fmt, calibrated=True/False)`); re-running the MLP sweep
with the calibrated constant (`results/cost_model_calibration.png`) shows
named float formats getting noticeably *more* expensive relative to
fixed-point once real control-logic overhead is priced in — e.g. `fixed_q8`
(96.1% acc) drops from looking *more* expensive than `fp8_e4m3` under the
naive model to costing **~3× less** than it under the calibrated one, at
comparable accuracy. One synthesized data point can't validate the full
O(mantissa²) *trend* (this RTL isn't parameterized across widths), but it
directly corrected the model's biggest constant.

One synthesizability note: `ieee754_normalizer`'s leading-zero-count loop
uses a data-dependent loop-exit condition that Vivado accepts but yosys's
frontend doesn't (it needs a compile-time-constant bound to unroll). The
copy in `synth/add_group.sv` rewrites it as an equivalent constant-bound
priority search — same combinational function, synthesizability fix only;
`../rtl-systemverilog/fpu_pipeline.sv` itself is untouched.

### 2. Recurrent task: does precision loss bite harder without redundancy?

MNIST classification turned out to be very forgiving of quantization (see
Results above) — plausibly because 3 independent feedforward layers average
out rounding noise. `train_rnn_parity.py` / `evaluate_rnn_precision_sweep.py`
test a task with the opposite structure: **majority vote** over a 20-bit
random sequence, using a from-scratch vanilla Elman RNN (`nn_framework/Layers/RNN.py`,
written for this project — see the provenance note in that file) trained
with full backprop-through-time. The *same* weights are applied 20 times in
a row, quantizing the hidden state after every step, so rounding error gets
20 chances to compound instead of 3.

RNN trained to 99.95% float32 accuracy. Result
(`results/mlp_vs_rnn_precision.png`, `results/rnn_precision_sweep.csv`): at
moderate precision the RNN is *as* robust as the MLP (both flat near 100%
down to 4-bit mantissas), but at the most extreme setting tested (2-bit
mantissa) the RNN's accuracy drops to 89.9% (-10.1pp from its own ceiling)
while the MLP only drops 0.2pp from its own ceiling at the same mantissa
width — a **~50× larger relative accuracy drop**, consistent with
compounding error across 20 recurrent steps rather than 3 independent
layers. (Exact parity — even/odd count, rather than majority — was tried
first and is a well-known hard case for a vanilla RNN: it needs precise
mod-2 counting through a tanh nonlinearity and doesn't get much above chance
accuracy even in float32, so it couldn't serve as a quantization baseline.)

### 3. Per-layer precision allocation — a negative result

`precision/sensitivity.py` computes, per FC layer, `mean(|dLoss/d(layer
output)|)` (first-order loss sensitivity, from one real forward+backward
pass) and each layer's own observed weight/activation range.
`evaluate_mixed_precision.py` uses these to route a fixed total bit budget
unevenly across layers — more fractional bits to sensitive layers — instead
of splitting it evenly, and compares both **at the same actual total bit
count** (not just the same nominal request, since the allocator's own
integer-bit floor can silently use more bits than requested at tight
budgets — the first version of this experiment made exactly that mistake,
comparing mismatched budgets and reporting an apparent win worth ~6pp that
had no real budget being held constant).

Corrected result (`results/mixed_precision.png`,
`results/mixed_precision_sweep.csv`): **mixed precision never beat uniform
at a matched bit count** — it tied everywhere except one point, where it was
0.2pp worse. Measured layer sensitivities did vary (`0.00283`, `0.00197`,
`0.00139` for layers 1-3, roughly 2× spread), but the *output* layer -- the
least sensitive one -- has by far the largest activation range (raw logits,
±93 vs. ±25 for layer 1), so its integer-bit floor alone consumes most of
any extra budget regardless of the sensitivity weighting; there was rarely
enough leftover budget left to route toward the sensitive-but-narrow-range
layers. Reporting this as it actually measured, rather than dressing up the
earlier budget-mismatch artifact as a win, since a heuristic that doesn't
survive a fair comparison isn't a real result.

## Possible further extensions

- Parameterize the RTL multiplier/adder by mantissa width to get a real
  multi-point O(mantissa²) validation instead of one calibration point.
- A per-layer allocator that reserves each layer's integer-bit floor *before*
  splitting the sensitivity-weighted remainder, but jointly optimizes
  int/frac split per layer (e.g. small search) rather than deriving int_bits
  from range alone — might recover a real benefit where extension 3 didn't.
