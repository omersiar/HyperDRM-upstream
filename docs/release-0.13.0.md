# HyperDRM 0.13.0

- Keep receiver connect/auth/layout/color/clear operations nonblocking and coalesce unsent frames.
- Match transaction acknowledgements and bound work, buffers and deadlines.
- Share atomic preference persistence with interrupted-write handling and explicit commit outcomes.
- Add malformed-input, backpressure, disconnect, parser-mutation and simulated lifecycle tests.
- Group runtime state in capture, receiver/layout and calibration contexts.
- Detect app/service version mismatches and provide an explicit Reload service action.

Validation: host JavaScript, native and Python tests pass, including ASan/UBSan
checks. The ARM IPK's embedded versions, architecture, size and SHA-256 match its
manifest. This version has not been installed or validated on the TV.
