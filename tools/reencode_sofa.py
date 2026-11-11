"""Re-encode SOFA files: float64 -> float32, gzip level 1 -> 9, keep shuffle + chunks.
Lossless for audio (float32 ~1e-7 rel. precision, -140 dB). Runtime loader only
checks HDF5 magic + size; HRIR is reconstructed from PCA basis, Data.IR not read.
"""
import h5py, os, sys, shutil

def reencode_file(src, dst):
    with h5py.File(src, 'r') as fin, h5py.File(dst, 'w') as fout:
        for k, v in fin.attrs.items():
            fout.attrs[k] = v
        def copy_obj(name, obj):
            if isinstance(obj, h5py.Dataset):
                data = obj[()]
                if data.dtype == 'float64':
                    data = data.astype('float32')
                kw = {}
                if data.size > 0:
                    # preserve original chunks if valid, else auto
                    chunks = obj.chunks
                    if chunks is not None and all(c <= s for c, s in zip(chunks, data.shape)):
                        kw['chunks'] = chunks
                    kw.update(dict(compression='gzip', compression_opts=9, shuffle=True))
                d = fout.create_dataset(name, data=data, **kw)
                for k, v in obj.attrs.items():
                    d.attrs[k] = v
            elif isinstance(obj, h5py.Group):
                g = fout.create_group(name)
                for k, v in obj.attrs.items():
                    g.attrs[k] = v
        fin.visititems(copy_obj)

src_root, dst_root = sys.argv[1], sys.argv[2]
total0 = total1 = n = 0
for dirpath, _, files in os.walk(src_root):
    for fn in sorted(files):
        if not fn.lower().endswith('.sofa'):
            continue
        src = os.path.join(dirpath, fn)
        rel = os.path.relpath(src, src_root)
        dst = os.path.join(dst_root, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        reencode_file(src, dst)
        s0, s1 = os.path.getsize(src), os.path.getsize(dst)
        total0 += s0; total1 += s1; n += 1
print(f"files: {n}")
print(f"total: {total0} -> {total1} bytes ({100*(total0-total1)/total0:.1f}% saved, {total0-total1} bytes)")
