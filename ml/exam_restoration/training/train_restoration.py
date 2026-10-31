"""Restoration training gate.

A learned restoration model is intentionally disabled until deterministic
paper and line reconstruction fail the approved quality gate.
"""


def main() -> int:
    raise SystemExit("No learned restoration model is approved; use deterministic Mask-only restoration")


if __name__ == "__main__":
    raise SystemExit(main())
