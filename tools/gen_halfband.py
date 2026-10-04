#!/usr/bin/env python3
"""Design the 2x oversampling half-band used by the distortion block.

A half-band IIR lowpass can be built from two allpass branches:  H(z) = (A0(z^2) + z^-1 A1(z^2)) / 2,
each A a cascade of first-order sections (a + z^-2) / (1 + a z^-2). It has no lookahead, so the plugin
adds no latency. The elliptic filter that meets a half-band spec has its poles on the imaginary axis, at
+-j*sqrt(a_k), which gives the coefficients directly. For each order there is exactly one passband edge
at which that holds; this finds it, then keeps the lowest order whose edge reaches the one asked for.

    tools/gen_halfband.py [stopband_dB] [passband_edge_as_fraction_of_the_base_rate]
"""
import sys
import numpy as np
from scipy import signal
from scipy.optimize import minimize_scalar

stop_db = float(sys.argv[1]) if len(sys.argv) > 1 else 80.0
want_edge = float(sys.argv[2]) if len(sys.argv) > 2 else 0.4125        # 19.8 kHz at 48 kHz

# Half-band symmetry ties the passband ripple to the rejection: |H(w)|^2 + |H(pi-w)|^2 = 1.
pass_db = -10 * np.log10(1 - 10 ** (-stop_db / 10))

def poles_for(order, edge):
    return signal.ellip(order, pass_db, stop_db, edge, output="zpk")[1]

for order in range(3, 21, 2):
    # Wn is relative to the oversampled Nyquist, which is the base rate: so it is the passband edge as a
    # fraction of the base rate, and the half-band mirror (the stopband edge) is 1 - edge.
    fit = minimize_scalar(lambda e: np.max(np.abs(poles_for(order, e).real)), bounds=(0.2, 0.499),
                          method="bounded", options={"xatol": 1e-13})
    edge, off_axis = fit.x, fit.fun
    if off_axis > 1e-6:
        continue
    if edge >= want_edge:
        break
else:
    sys.exit("no order reached the requested edge")

p = poles_for(order, edge)
radii = np.sort(np.abs(p.imag[p.imag > 1e-9]))
coeffs = radii ** 2
a0, a1 = coeffs[0::2], coeffs[1::2]                                   # alternate between the two branches

# Verify the structure, not the prototype: run the two branches as the plugin will.
def branch(cs, x):
    for c in cs:
        y = np.zeros_like(x)
        for n in range(len(x)):
            # a section of the half-rate allpass: y[n] = c*x[n] + x[n-1] - c*y[n-1]
            y[n] = c * x[n] + (x[n - 1] if n else 0) - c * (y[n - 1] if n else 0)
        x = y
    return x
n = 1 << 14
imp = np.zeros(n); imp[0] = 1
even, odd = branch(a0, imp[0::2].copy()), branch(a1, imp[0::2].copy())
h = np.zeros(n); h[0::2] += 0.5 * even; h[1::2] += 0.5 * odd
f = np.fft.rfftfreq(n, 1.0)                                           # cycles per oversampled sample
mag = 20 * np.log10(np.abs(np.fft.rfft(h)) + 1e-30)
base = f * 2                                                          # as a fraction of the base rate
stop = mag[base >= 1 - edge].max()
ripple = np.abs(mag[base <= edge]).max()
print(f"order {order} ({len(a0)}+{len(a1)} sections): passband to {edge:.4f} of the base rate "
      f"(ripple {ripple:.5f} dB), stopband from {1-edge:.4f} down to {stop:.1f} dB")
print("branch0 =", ", ".join(f"{c:.17g}" for c in a0))
print("branch1 =", ", ".join(f"{c:.17g}" for c in a1))
