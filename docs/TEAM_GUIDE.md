# Team Development Guide

## Branching

Use small feature branches:

```text
main
 ├── feature/lcd-ui
 ├── feature/mpu6050
 ├── feature/buttons
 ├── feature/esp-now
 └── feature/usb-hid
```

## Commit style

Prefer commits that describe one change:

```text
feat: add joystick calibration
fix: correct B4 GPIO mapping
docs: add ESP32-S3 flashing guide
test: add button verification procedure
```

## Before opening a pull request

- Verify the sketch compiles with the documented ESP32 core version.
- Do not change GPIO assignments without updating `docs/WIRING.md`.
- Do not change packet fields without updating `docs/ESP_NOW_PROTOCOL.md`.
- Do not change button behavior without updating `docs/CONTROLS.md`.
- Record any board/core/library version change.
- Test hardware after firmware changes.

## Hardware change rule

A wiring change is a software change too if a GPIO assignment changes. Update all three:

1. source code pin constants;
2. wiring documentation;
3. test checklist.

## Source-of-truth rule

The code is the source of truth for actual GPIO constants. Documentation should match it. If documentation and code disagree, stop and reconcile them before building another hardware revision.

## Uploaded-source traceability

The original uploaded files are kept in `firmware/uploaded-source/` so the team can compare the repository implementation with the files received from the project owner.
