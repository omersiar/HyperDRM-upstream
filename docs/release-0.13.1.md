# HyperDRM 0.13.1

Suppress near-black histogram residuals that can appear green on SK6812 LEDs. Colors whose maximum channel is at most 12 become black; values between 12 and 24 fade in proportionally; brighter colors retain their original values. Raw histogram diagnostics and calibration samples remain available unchanged. PicCap frames are not filtered.

Validation: regression tests cover black, the measured green residual, transition values and unchanged brighter colors. The release workflow runs the existing native and UI suites and verifies the packaged version and checksum.
