"""Decode padded GPU capture planes without altering captured buffers."""
import argparse
import json
from pathlib import Path
import numpy as np

def read_plane(folder, p):
    formats = {10: ('<f2', 4), 2: ('<f4', 4), 41: ('<f4', 1),
               16: ('<f4', 2), 54: ('<f2', 1), 34: ('<f2', 2)}
    if p['format'] not in formats:
        raise ValueError(f"Unsupported DXGI format {p['format']}; raw bytes preserved")
    dtype, channels = formats[p['format']]
    raw = (folder / (p['name'] + '.bin')).read_bytes()
    if len(raw) != p['bytes']:
        raise ValueError('Truncated capture plane')
    size = np.dtype(dtype).itemsize
    return np.ndarray((p['height'], p['width'], channels), dtype=dtype, buffer=raw,
                      strides=(p['row_pitch'], channels*size, size)).astype(np.float32)

def difference(a, b):
    if a.shape != b.shape:
        return {'comparable': False, 'reason': 'different dimensions/channels'}
    a, b = a[..., :3], b[..., :3]
    valid = np.isfinite(a) & np.isfinite(b)
    if not valid.any():
        return {'comparable': False, 'reason': 'no finite samples'}
    delta = np.abs(a[valid].astype(np.float64) - b[valid])
    return {'comparable': True, 'mean_absolute': float(delta.mean()),
            'max_absolute': float(delta.max()), 'p99_absolute': float(np.percentile(delta, 99)),
            'changed_fraction': float((delta > 1e-4).mean()),
            'nonfinite_components': int(valid.size - valid.sum())}

def analyze(root):
    report = {'boundary': 'same invocation per pass; views not guaranteed simultaneous', 'passes': [], 'chains': []}
    decoded = {}
    for path in sorted(root.glob('view-*/capture.json')):
        meta = json.loads(path.read_text())
        planes = {p['name']: read_plane(path.parent, p) for p in meta['planes']}
        key = (meta['base_view'], meta['pass'])
        decoded[key] = planes
        entry = {'directory': path.parent.name, 'metadata': meta,
                 'model_signal_difference': difference(planes['proxy'], planes['neural']),
                 'restoration_difference': difference(planes['original'], planes['restored'])}
        if 'exposure' in planes:
            entry['exposure_sample'] = float(planes['exposure'][0, 0, 0])
        report['passes'].append(entry)
    for (view, number), planes in decoded.items():
        if (view, number+1) in decoded:
            report['chains'].append({'view': view, 'from_pass': number, 'to_pass': number+1,
                'restored_to_next_original': difference(planes['restored'], decoded[(view,number+1)]['original'])})
    return report

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('capture', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    report = analyze(args.capture)
    if not report['passes']:
        raise SystemExit('No completed pass metadata found')
    text = json.dumps(report, indent=2, allow_nan=False)
    if args.output:
        args.output.write_text(text)
    print(text)
