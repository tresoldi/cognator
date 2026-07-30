# C linguistic smoke fixtures

These are compact, pinned fixtures for the C rewrite's milestone checks.
They are not replacements for full CoreCog benchmarking.

- `walworth_polynesian_smoke.tsv` keeps a small Polynesian-style subset with
  regular near-identical cognate forms across Hawaiian, Samoan, Tongan, and
  Maori labels.
- `latin_romance_triplets.tsv` checks Latin-Romance reflexes such as
  `nokte`/`notte`/`note`, `okto`/`otto`/`oto`, and a non-cognate fire split.

The CTest `linguistic_smoke` test runs the C CLI `detect` command on both
fixtures, then scores the sidecars with the C CLI `eval` command. These checks
guard the end-to-end path and catch obvious linguistic regressions before the
larger Walworth CoreCog benchmark is available.
