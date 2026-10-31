"""Hard Mask-only composition used by restoration candidates."""


def compose(corrected, candidate, accepted_mask):
    try:
        import torch
        return torch.where(accepted_mask.bool(), candidate, corrected)
    except ImportError as error:
        raise RuntimeError("PyTorch is required for restoration training") from error
