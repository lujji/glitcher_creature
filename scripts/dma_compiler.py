import struct

def compile_dma_sequence(pairs, threshold_bits=1024):
    # pack patterns into words
    nonzero = {}
    pos = 0
    min_words = max(1, (threshold_bits + 31) // 32)
    for gap, pattern in pairs:
        if gap < 0:
            raise ValueError(f'negative gap: {gap}')
        if pattern < 0:
            raise ValueError(f'negative pattern: {pattern}')
        n = max(1, (pattern.bit_length() + 31) // 32)
        pos += gap
        idx, shift = divmod(pos, 32)
        pos += 32 * n
        spread = (pattern << (32 - shift)).to_bytes(4 * (n + 1), 'big')
        for i, (w,) in enumerate(struct.iter_unpack('>I', spread)):
            if w:
                nonzero[idx + i] = nonzero.get(idx + i, 0) | w

    # split into blocks
    durations, patterns = [], []
    zeros_first = len(nonzero) > 0 and next(iter(nonzero)) >= min_words
    block = 0 # words in the current PATTERN block
    end_idx = 0 # index after block
    for idx, w in nonzero.items():
        gap = idx - end_idx
        top_up = max(0, min_words - block) if block else 0
        if gap - top_up >= min_words:
            if block:
                patterns += [0] * top_up
                durations.append(block + top_up)
            durations.append(gap - top_up)
            block = 0
        else:
            patterns += [0] * gap
            block += gap
        patterns.append(w)
        block += 1
        end_idx = idx + 1

    tail = max(1, min_words - block)
    patterns += [0] * tail
    durations.append(block + tail)
    return zeros_first, durations, patterns
