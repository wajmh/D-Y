# Repository Guidelines

## Project Structure & Module Organization

This repository contains STM32G474RBT6 dual-battery power-management firmware.
Application code and headers live in `Core/Src/` and `Core/Inc/`; the main modules
are `gpio` (MOSFET/power state machine), `adc` (DMA sampling), `fdcan` (BMS
gateway and telemetry), plus `i2c`, `usart`, and startup/interrupt support. STM32
HAL and CMSIS dependencies are vendored under `Drivers/`.
The CMake integration is in `cmake/`, the linker script is
`STM32G474xx_FLASH.ld`, and alternative IDE projects are under `EWARM/` and
`MDK-ARM/`. Design and protocol notes are kept in the root-level Markdown files.

## Build, Test, and Development Commands

The supported command-line build uses CMake presets, Ninja, and an
`arm-none-eabi-*` toolchain on `PATH`:

```sh
cmake --preset Debug
cmake --build --preset Debug
cmake --preset Release
cmake --build --preset Release
```

Each build produces `D-Y.elf` plus `.hex` and `.bin` images in `build/<preset>/`.
Use `cmake --build --preset Debug --target clean` to remove generated outputs.
Flash and debug through the board’s supported probe/IDE; no host-side run target
is provided.

## Coding Style & Naming Conventions

Use C11 with two-space indentation and the brace style already used in `Core/`.
Match existing names:
`PascalCase` module APIs (for example, `Power_DischargeModeTask`),
`UPPER_SNAKE_CASE` macros/constants, and `camelCase` for file-local state.
Preserve STM32CubeMX `USER CODE BEGIN/END` blocks and put hand-written logic in
those regions so regeneration does not erase it. Check return values from HAL
calls and use fixed-width types (`uint32_t`, `uint16_t`, etc.) for wire/register
data.

## Testing Guidelines

There is no automated unit-test framework in this repository. At minimum, run a
Debug build and inspect compiler warnings. Hardware changes should be validated
on an STM32G474 board, including ADC offset calibration, dual-battery
pre-discharge/MOS sequencing, emergency-stop behavior, and FDCAN1/2/3 traffic.
Record relevant CAN IDs, timing, and measured safety behavior in the PR.

## Commit & Pull Request Guidelines

Recent commits use short summaries in either Chinese or English
(for example, `更改预充时间为400ms` or `Add initial content to README.md`). Keep
the subject focused; add a body when the hardware or protocol rationale is not
obvious. Pull requests should explain the behavioral and hardware impact, list
the exact build/flash checks performed, link an issue when available, and attach
scope traces, CAN captures, or photos for safety- or wiring-related changes.

## Generated Files & Configuration

Do not commit contents of `build/` or other compiler output. Keep board pin,
clock, linker, and CAN protocol changes synchronized across the C sources,
headers, CMake configuration, and the relevant IDE project.
