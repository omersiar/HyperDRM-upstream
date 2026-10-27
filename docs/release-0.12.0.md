# HyperDRM 0.12.0

- Process buffered receiver updates even when no additional network data arrives.
- Keep action errors visible across background status polling.
- Name connection, transition and calibration states; ignore stale calibration callbacks.
- Split and format native service modules and application JavaScript.
- Generate versions from VERSION and add a tested, reproducible release workflow.

Validation: all portable native, JavaScript and Python version tests pass; the
ARM IPK's embedded versions, architecture, size and SHA-256 match its manifest.
This release has not yet been installed or validated on the TV.
