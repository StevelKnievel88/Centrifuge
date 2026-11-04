"""Export HTDemucs (4 stems) to ONNX for OrbitDJ.

The STFT / iSTFT are moved out of the network (ONNX can't export them); OrbitDJ does them in C++
exactly as demucs does. Uses Sevag Hanssian's demucs fork prepared for this (demucs.onnx).

usage: python export_htdemucs_onnx.py <path-to-demucs-for-onnx> <output.onnx> [model name] [sub-model index]
  e.g. ... htdemucs_ft.onnx htdemucs_ft 0   (htdemucs_ft is 4 specialist models: 0 drums, 1 bass, 2 other, 3 vocals)
"""
import sys
import numpy as np
import torch

sys.path.insert(0, sys.argv[1])
from demucs.pretrained import get_model  # noqa: E402
from demucs.htdemucs import HTDemucs, standalone_spec, standalone_magnitude  # noqa: E402

out_path = sys.argv[2]

name = sys.argv[3] if len(sys.argv) > 3 else "htdemucs"
index = int(sys.argv[4]) if len(sys.argv) > 4 else 0
model = get_model(name)
core = model if isinstance(model, HTDemucs) else model.models[index]
core.eval()

length = int(core.segment * core.samplerate)
print("segment samples:", length, "sources:", core.sources)

torch.manual_seed(0)
mix = torch.randn(1, 2, length) * 0.3
spec = standalone_magnitude(standalone_spec(mix))

kwargs = dict(
    export_params=True,
    opset_version=17,
    do_constant_folding=True,
    input_names=["mix", "spec"],
    output_names=["spec_out", "time_out"],
)
try:
    torch.onnx.export(core, (mix, spec), out_path, dynamo=False, **kwargs)
except TypeError:
    torch.onnx.export(core, (mix, spec), out_path, **kwargs)
print("exported", out_path)

# Check the ONNX model reproduces PyTorch
import onnxruntime as ort  # noqa: E402

sess = ort.InferenceSession(out_path, providers=["CPUExecutionProvider"])
o_spec, o_time = sess.run(None, {"mix": mix.numpy(), "spec": spec.numpy()})
with torch.no_grad():
    r_spec, r_time = core(mix, spec)
for name, a, b in (("spec", o_spec, r_spec.numpy()), ("time", o_time, r_time.numpy())):
    err = np.max(np.abs(a - b))
    print(f"{name}: shape {a.shape}, max abs diff {err:.3e}, max {np.max(np.abs(b)):.3e}")
    assert err < 1e-2 * max(1.0, np.max(np.abs(b))), "ONNX output differs from PyTorch"
print("ONNX matches PyTorch")
