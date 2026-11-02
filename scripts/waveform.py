import matplotlib.pyplot as plt
import numpy as np
from matplotlib.ticker import EngFormatter

def parse_bitstream(dma_sequence):
    zeros_first, durations, patterns = dma_sequence
    edges, levels, pos = [], [], 0
    for i, n in enumerate(durations):
        if (i % 2 == 0) == zeros_first:
            edges.append([pos])
            levels.append([0])
        else:
            words, patterns = patterns[:n], patterns[n:]
            edges.append(pos + np.arange(32 * n))
            levels.append(np.unpackbits(np.array(words, '>u4').view(np.uint8)))
        pos += 32 * n
    return np.r_[np.concatenate(edges), pos], np.concatenate(levels)

def plot_waveforms(dma_sequence, switcher=(), freq=250e6, channels=4, ax=None, cycles=False):
    t_cyc = 2 if cycles else 1 / freq
    edges, levels = parse_bitstream(dma_sequence)
    masks, delays = zip(*(switcher or [(1, 0)]))
    t_sw = (np.cumsum([d + 3 for d in delays[:-1]]) - 1) * t_cyc
    end = max(edges[-1] / 2 * t_cyc, t_sw.max(initial=0) * 1.05)

    if ax is None:
        _, ax = plt.subplots(figsize=(14, 1 + 0.6 * (channels + 1)), layout='constrained')
    ax.stairs(levels + 1.5 * channels, edges / 2 * t_cyc, baseline=None, color='C0')
    for ch in range(channels):
        ax.stairs((np.array(masks) >> ch & 1) + 1.5 * (channels - 1 - ch),
                  np.r_[-0.02 * end, t_sw, end], baseline=None, color='C1')

    ax.axvline(0, color='0.6', lw=1)
    ax.set_yticks(1.5 * np.arange(channels, -1, -1) + 0.5, ['coil'] + [f'sw{i}' for i in range(channels)])
    if cycles:
        ax.set_xlabel('2*F_CPU cycles')
    else:
        ax.xaxis.set_major_formatter(EngFormatter(unit='s'))
    ax.grid(axis='x', alpha=0.3)
    return ax
