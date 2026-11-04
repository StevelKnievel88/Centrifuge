"""Reference separation with the official demucs package, to check OrbitDJ's C++ pipeline.

usage:
  python stem_reference.py make-test <out.wav>                 # synthetic 26 s test track
  python stem_reference.py separate <in.wav> <out_dir> [model]  # official demucs (htdemucs, or e.g. htdemucs_ft)
  python stem_reference.py compare <ref_dir> <orbit_dir>       # SDR per stem, fails if too low
  python stem_reference.py sanity <mix.wav> <orbit_dir>        # stems are finite, not silent, and add up to the mix
"""
import sys
import numpy as np
import soundfile as sf


def make_test(path):
    sr = 44100
    n = int(sr * 26.3)
    t = np.arange(n) / sr
    rng = np.random.default_rng(1)
    beat = 60 / 140
    kick = np.zeros(n)
    hat = np.zeros(n)
    for i in range(int(t[-1] / beat)):
        s = int(i * beat * sr)
        k = np.arange(min(6000, n - s))
        kick[s:s + len(k)] += np.sin(2 * np.pi * (50 + 120 * np.exp(-k / 600)) * k / sr) * np.exp(-k / 4000)
        h = int((i + 0.5) * beat * sr)
        if h + 2000 < n:
            hat[h:h + 2000] += rng.normal(0, 0.3, 2000) * np.exp(-np.arange(2000) / 300)
    bass = 0.35 * np.sign(np.sin(2 * np.pi * 55 * t)) * (0.5 + 0.5 * np.sin(2 * np.pi * t / (beat / 2)) ** 2)
    pad = 0.15 * (np.sin(2 * np.pi * 220 * t) + np.sin(2 * np.pi * 277.2 * t) + np.sin(2 * np.pi * 329.6 * t))
    vox = 0.25 * np.sin(2 * np.pi * (440 + 30 * np.sin(2 * np.pi * 5 * t)) * t) * (np.sin(2 * np.pi * t / 4) > 0)
    mix = 0.6 * kick + 0.4 * hat + bass + pad + vox
    stereo = np.stack([mix, np.roll(mix, 37)], axis=1) * 0.5
    sf.write(path, stereo.astype(np.float32), sr, subtype="FLOAT")


def separate(inp, out_dir, model_name="htdemucs"):
    import os
    import torch
    from demucs.pretrained import get_model
    from demucs.apply import apply_model

    wav, sr = sf.read(inp, dtype="float32", always_2d=True)
    assert sr == 44100
    wav = torch.from_numpy(wav.T.copy())
    model = get_model(model_name)
    model.eval()
    ref = wav.mean(0)
    wav = (wav - ref.mean()) / ref.std()
    with torch.no_grad():
        sources = apply_model(model, wav[None], shifts=0, split=True, overlap=0.25, progress=False)[0]
    sources = sources * ref.std() + ref.mean()
    os.makedirs(out_dir, exist_ok=True)
    for name, s in zip(model.sources, sources):
        sf.write(f"{out_dir}/{name}.wav", s.numpy().T, sr, subtype="FLOAT")


def sdr(ref, est):
    n = min(len(ref), len(est))
    ref, est = ref[:n], est[:n]
    return 10 * np.log10(np.sum(ref ** 2) / max(np.sum((ref - est) ** 2), 1e-20))


def compare(ref_dir, est_dir):
    ok = True
    mix = sum(sf.read(f"{ref_dir}/{n}.wav", dtype="float64")[0] for n in ("drums", "bass", "vocals", "other"))
    for name in ("drums", "bass", "vocals", "other"):
        r, _ = sf.read(f"{ref_dir}/{name}.wav", dtype="float64")
        e, _ = sf.read(f"{est_dir}/{name}.wav", dtype="float64")
        value = sdr(r, e)
        # "other" in OrbitDJ also carries the model's residual, so it's only roughly comparable.
        if name == "other":
            print(f"{name:7s} SDR vs official demucs: {value:6.1f} dB  (informational: includes the residual)")
            continue
        n = min(len(r), len(e), len(mix))
        rel = 10 * np.log10(np.sum(mix[:n] ** 2) / max(np.sum((r[:n] - e[:n]) ** 2), 1e-20))
        print(f"{name:7s} SDR vs official demucs: {value:6.1f} dB, error {rel:6.1f} dB below the mix  (need SDR > 30 or error > 50 dB down)")
        ok &= value > 30.0 or rel > 50.0
    if not ok:
        sys.exit("C++ stem separation does not match the reference")
    print("C++ stem separation matches the official demucs output")


def sanity(mix_path, est_dir):
    mix, _ = sf.read(mix_path, dtype="float64", always_2d=True)
    total = 0
    for name in ("drums", "bass", "other", "vocals"):
        e, _ = sf.read(f"{est_dir}/{name}.wav", dtype="float64", always_2d=True)
        if not np.all(np.isfinite(e)):
            sys.exit(f"{name} has NaN/inf samples")
        print(f"{name:7s} rms {np.sqrt(np.mean(e ** 2)):.4f}")
        total = total + e[: len(mix)]
    err = 10 * np.log10(np.sum(mix ** 2) / max(np.sum((mix - total) ** 2), 1e-20))
    print(f"stems add up to the mix within {err:.1f} dB")
    if err < 40:
        sys.exit("stems don't add up to the mix")


def similar(ref_dir, est_dir, name, min_corr):
    """The same stem from two different separators should broadly agree (e.g. RoFormer vs htdemucs_ft vocals)."""
    r, _ = sf.read(f"{ref_dir}/{name}.wav", dtype="float64", always_2d=True)
    e, _ = sf.read(f"{est_dir}/{name}.wav", dtype="float64", always_2d=True)
    n = min(len(r), len(e))
    r, e = r[:n].ravel(), e[:n].ravel()
    corr = float(np.corrcoef(r, e)[0, 1])
    print(f"{name}: rms ref {np.sqrt(np.mean(r ** 2)):.4f}, ours {np.sqrt(np.mean(e ** 2)):.4f}, correlation {corr:.3f}")
    if not corr > float(min_corr):
        sys.exit(f"{name} doesn't look like the reference separation")


if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "make-test":
        make_test(sys.argv[2])
    elif cmd == "separate":
        separate(sys.argv[2], sys.argv[3], *(sys.argv[4:5]))
    elif cmd == "compare":
        compare(sys.argv[2], sys.argv[3])
    elif cmd == "similar":
        similar(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif cmd == "sanity":
        sanity(sys.argv[2], sys.argv[3])
