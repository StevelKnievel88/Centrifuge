# Stub: htdemucs weights aren't quantized, so diffq is never used. Avoids building diffq from source.
class DiffQuantizer:  # noqa: D101
    def __init__(self, *a, **k): raise RuntimeError("diffq not available")
UniformQuantizer = DiffQuantizer
def restore_quantized_state(*a, **k): raise RuntimeError("diffq not available")
