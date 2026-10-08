# Crash reports

Diagnostics are stored in `%TEMP%\MCBE-ImGui-Tess\`.

## Files

- `session.log`: startup, signatures, hooks, material/texture initialization.
- `last-stage.log`: last throttled backend checkpoint.
- `crash-last.log`: exception, registers, stack addresses, and render pointers.
- `crash-last.dmp`: Windows minidump.

For an issue, include the Bedrock build, commit SHA, reproduction steps, and the text logs above.

A minidump can contain process memory and local paths. Review it before posting it publicly.
