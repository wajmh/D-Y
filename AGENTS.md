# Repository Guidelines

Firmware for the quadruped robot **Power Management Board (PMB)**. Target: STM32G474RBT6 (Cortex-M4F, 170 MHz, 128 KB Flash / 128 KB SRAM), hardware V1.3, firmware v1.4.2 single-battery. The repo is documentation-heavy and safety-critical; this file is the fast path for AI assistants. When code and docs disagree, **trust the code** and flag the doc.

## Project Overview

- Bare-metal C11 + STM32 HAL. **No RTOS, no watchdog** (IWDG/WWDG disabled), no heap-dependent framework.
- Responsibilities: battery-1 BMS gateway (FDCAN1), RK3588 gateway (FDCAN2), 400 ms precharge + 200 ms overlap power sequencing, VBUS disconnect protection, TIM8 complementary back-EMF bleed, LEDs/buzzer, CAN IAP with warm handover.
- **Single battery only.** Battery 2 pins (PC10/PC11/PC12) are forced low, charging is stubbed, FDCAN3 is intentionally never initialized. Do not reintroduce dual-battery, charger-handshake, or FDCAN3 logic without explicit hardware approval.
- Application version: `APP_FW_VERSION_*` in `Core/Inc/fdcan.h` (1.4.2). Bootloader: 1.0.0 in `Bootloader/Core/Inc/boot_can.h`.
- Project docs are Chinese; code identifiers and this guide are English. `README.md` is the main manual but has stale values (see Testing & QA).

## Architecture & Data Flow

### Memory layout (hard contract)

| Region | Address | Size |
|---|---|---|
| Bootloader (pages 0-4) | `0x08000000` | 20 KB |
| App metadata (page 5) | `0x08005000` | 4 KB |
| Application (pages 6-31) | `0x08006000` | 104 KB |

`0x08006000` is duplicated in `Core/Src/main.c` (VTOR), `STM32G474xx_FLASH.ld`, `Bootloader/Core/Inc/boot_flash.h`, `boot_jump.c`, and `fdcan.c` (IAP jump validation). Change all atomically. `DBANK=0` (single-bank, 4 KB pages) is required.

### Application structure

There are **no `power.c` / `can_app.c` / `app/` modules**. Business logic lives inside STM32CubeMX files, in `USER CODE` blocks:

- `Power_*` state machine, VBUS protection, indicators: `Core/Src/gpio.c` (`USER CODE 2`).
- `FDCAN_*` BMS gateway, telemetry, Bus-Off recovery, IAP handshake: `Core/Src/fdcan.c` (`USER CODE 0/1`).
- ADC sampling/calibration/filtering: `Core/Src/adc.c`.
- `main.c` only does init and the super-loop. Do not create a parallel `Power_*` or `FDCAN_*` implementation elsewhere.

### Boot path

Bootloader `boot_main.c` reads the upgrade flag (`TAMP->BKP0R`, fallback SRAM `0x2001FC00`), validates the app (`BootAppInfo_t` magic/size/CRC32), and either stays in IAP or jumps to the app. App `main()` sets `SCB->VTOR = 0x08006000U` **before** `HAL_Init()`, then handles warm boot (`Power_CheckAndHandleHotBoot()`), clock (HSE 24 MHz -> PLL -> 170 MHz), peripheral init, and enters the loop.

### Runtime data flow

Super-loop order in `Core/Src/main.c`:

```text
ADC_UpdateCurrents -> ADC2_UpdateVbusVoltage -> FDCAN_BatteryCanTask
  -> Power_UpdateGpioDebugStates -> Power_ModeTask
  -> Power_UpdateStatusIndicators -> FDCAN_CheckAndRecoverAllBusOff
```

- Scheduling is cooperative: unsigned `HAL_GetTick()` deltas (`now - last >= PERIOD`). Never add blocking waits to the loop.
- CAN RX is **polled from FIFO0**, not an ISR queue. The only application ISR logic is the FDCAN Bus-Off notification callback; recovery is an asynchronous per-bus state machine.
- FDCAN1: parses BMS status and MOS frames, forwards status/info/fault/temperature to FDCAN2 (battery index ORed into the low byte). BMS discharge-MOS state is debounced over 3 consecutive frames; >2 s stale resets to UNKNOWN.
- FDCAN2: dispatches exact IDs `0x04500000` (charge), `0x04600000` (indicators), `0x04700000` (IAP). All RK TX is gated on the bus not being Bus-Off and TX FIFO space.
- TX cadence: `0x04400000` alarm 100 ms/edge, `0x04200000` telemetry 200 ms, `0x04600001` indicator status 500 ms/edge, BMS wake `0x0400FF80` every 2 s on FDCAN1.

### Power state machine (`Core/Src/gpio.c`)

`OFF -> PRE_DISCHARGE -> DISCHARGE`

1. Enter precharge: peripheral power `PA5` + precharge MOS `PB9` on, wait **400 ms** (`POWER_PRE_DISCHARGE_DELAY_MS`).
2. Close main discharge `PB6` + recharge `PB7`, enable 12 V/24 V DCDC, wait **200 ms** overlap (`POWER_PRE_DISCHARGE_OVERLAP_MS`), then open `PB9`.
3. Fault (VBUS < 30 V for 10 ms, or recovery inside that 10 ms window) -> all power off, OFF state. Restart requires **BMS CAN alive AND BMS discharge allowed**; never restart precharge on voltage recovery alone.
4. Warm boot (IAP handover) deliberately skips precharge and takes over the already-conducting path.

### Safety invariants (read before editing)

- Never bypass or shorten the 400 ms + 200 ms precharge/overlap. Never close `PB6` onto an uncharged bus.
- In `DISCHARGE`, CAN timeout must **not** cut local power; only report alarm `0x01` so RK can land the robot.
- Keep the 30 V / 10 ms latch (`vbusLowVoltageDetected`), including the "recovered during debounce" trip. Removing it reintroduces the hard-short bug.
- Polarity is not uniform: MOS/peripheral power are active-high (`POWER_SWITCH_ON = SET`); 12 V/24 V DCDC enables are active-low (`POWER_DCDC_ON = RESET`).
- IAP must keep the main discharge MOS and 12 V/24 V DCDC on; turn off charge/recharge/precharge/release/peripheral supply. Never cold-reset while RK3588 is powered from the board.
- Back-EMF bleed is currently **disabled**: thresholds are `999.0f/999.0f`, duty 0. Standard values are 85 V open / 84 V close / 50% duty; do not enable without hardware verification.
- `Power_GetActiveDischargeMask()` defaults to `0x01` when the state machine and pins both read "off". This is an anti-brick fallback; do not "fix" it to 0.
- Treat charge mode as dead code: `Power_EnterChargeMode()` is a no-op and `charge_mode_active` is never set. Do not build features on it.

## Key Directories

| Path | Purpose |
|---|---|
| `Core/Src/`, `Core/Inc/` | Application firmware. Most logic is hand-written inside CubeMX `USER CODE` blocks in `gpio.c`, `fdcan.c`, `adc.c`. |
| `Bootloader/Core/{Src,Inc}/` | Independent IAP bootloader (`boot_main`, `boot_can`, `boot_flash`, `boot_jump`, `boot_it`). Separate HAL config and linker script. |
| `Drivers/` | Vendored STM32G4 HAL + CMSIS. Never edit by hand; CubeMX may replace it. |
| `cmake/stm32cubemx/` | CubeMX-generated source/include lists. `cmake/gcc-arm-none-eabi.cmake` is the GCC toolchain; `cmake/starm-clang.cmake` is unused. |
| `MDK-ARM/`, `EWARM/` | Legacy Keil/IAR projects. Stale and not valid for the IAP layout; do not use for builds. |
| `scripts/` | Host-side `can_iap_tool.py` + `requirements.txt` (`python-can>=4.0.0`). |
| Root markdown/PDF files | Protocol and hardware design notes in Chinese; see Important Files. |
| `build/`, `Bootloader/build/` | Ignored build outputs (`D-Y.*`, `Bootloader.*`). |

## Development Commands

Requirements: CMake >= 3.22, Ninja, `arm-none-eabi-gcc`/`g++`/`objcopy` (>= 10.3; 13.2.1 and ST 14.3.1 both seen), Python 3 + `python-can` for IAP. C and C++ compilers are both detected even though no C++ sources exist.

### Application

```sh
cmake --preset Debug
cmake --build --preset Debug

cmake --preset Release          # use Release for IAP
cmake --build --preset Release

cmake --build --preset Debug --target clean
cmake --build --preset Release --target clean
```

Outputs: `build/<preset>/D-Y.elf`, `D-Y.hex`, `D-Y.bin`, `D-Y.map`. The post-build step produces HEX/BIN; the `.map` is a side effect of the link.

- **New `.c` files are not auto-discovered.** Add them to `target_sources` in the root `CMakeLists.txt` (regeneration-safe), not to `cmake/stm32cubemx/CMakeLists.txt` (CubeMX-generated).
- After editing `STM32G474xx_FLASH.ld`, force a relink; Ninja has no `LINK_DEPENDS` on the linker script.
- `.clangd` points at `build/Release`; run a Release configure for indexing.

### Bootloader (separate project)

```sh
cd Bootloader
cmake --preset Release
cmake --build --preset Release
# output: Bootloader/build/Release/Bootloader.{elf,hex,bin}
```

`Bootloader/CMakePresets.json` hardcodes a machine-specific Ninja path. Override if missing:

```sh
cmake --preset Release -DCMAKE_MAKE_PROGRAM="$(command -v ninja)"
```

### Flash and CAN IAP

First-time bootloader flash: ST-Link/STM32CubeProgrammer at `0x08000000` with `DBANK=0`. Normal app updates go through IAP; the app build already links at `0x08006000`.

```sh
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 250000
sudo ip link set can0 up

python3 scripts/can_iap_tool.py -c can0 -q
python3 scripts/can_iap_tool.py -c can0 -f build/Release/D-Y.bin --trigger
python3 scripts/can_iap_tool.py -c can0          # interactive
```

**Never flash `MDK-ARM/D-Y/D-Y.hex` or an IAR build to an IAP device.** Both map from `0x08000000` and overwrite the bootloader; the Keil project also references missing I2C sources and omits `tim.c`.

`--trigger` alone is not a dry-run: it enters batch mode and performs a full upgrade using the default `D-Y.bin` next to the script. Use `-q` to query only.

## Code Conventions & Common Patterns

- **Formatting:** C11 (`gnu11`), 2-space indent, Allman braces, Doxygen file banners, Chinese comments explaining hardware/safety rationale. No `.clang-format` or `.editorconfig`; never reformat wholesale.
- **Naming:**
  - CubeMX: `MX_<Periph>_Init`, `HAL_<Periph>_MspInit`, `SystemClock_Config`, `Error_Handler`.
  - Business prefixes, not filenames: `Power_*` (in `gpio.c`), `FDCAN_*`, `ADC_*`/`ADC2_*`, `Boot_*`.
  - Types `PascalCase_t` (`PowerBatteryControl_t`, `BootAppInfo_t`); enum members `UPPER_SNAKE`.
  - File-scope state `camelCase`; exported/protocol state `snake_case` (`battery1_can_sum_voltage`); macros `UPPER_SNAKE` with `U`/`f` suffixes.
- **Types/scaling:** use `stdint` fixed-width types for wire/register data. Wire scaling lives in headers and packers (e.g. current/VBUS x100, BMS voltage/current/SOC x0.1).
- **Error handling:** `Error_Handler()` disables IRQs and spins forever (also in the bootloader); `assert_param` is a no-op (`USE_FULL_ASSERT` off). HAL return values are checked at init but often `(void)`-cast or ignored at runtime. There is no logging/printf; diagnostics are counters, LEDs, and CAN frames.
- **Concurrency:** cross-context data is `volatile`. TX enqueue uses a `PRIMASK` save/disable/restore critical section and re-checks Bus-Off. Bus-Off recovery uses generation counters to reject stale events. Flash programming in the bootloader disables IRQs.
- **State/state machines:** file-scope static controllers (`battery1Control`, `rkIndicatorCtrl`) plus exported volatile status flags. Timed behavior is `HAL_GetTick()`-based and stateless where possible; Bus-Off and bootloader states use explicit enums.
- **Generated-code discipline:** hand-written code goes only inside `USER CODE BEGIN/END`. Do not edit `Drivers/`; CubeMX has `DeletePrevious=true`/`CoupleFile=true`. Root `CMakeLists.txt` survives regeneration; `cmake/stm32cubemx/CMakeLists.txt` does not.
- **Do not build on stubs:** charge mode, battery-1 SOC/voltage telemetry consumers, and Bus-Off diagnostic counters are written but unused/not transmitted.

## Important Files

| File | Why it matters |
|---|---|
| `Core/Src/main.c` | Boot order, VTOR relocation, clock, peripheral init, super-loop. |
| `Core/Src/gpio.c` | The real power manager: precharge/discharge state machine, VBUS trips, DCDC/peripheral control, LEDs/buzzer, warm-handover GPIO. |
| `Core/Inc/gpio.h` | Pin map, active-high/low polarity, timing/threshold macros, alarm codes, `Power_*` API. |
| `Core/Src/fdcan.c` | BMS parsing/forwarding, telemetry packing, Bus-Off recovery, indicator/charge/IAP command dispatch, App->Bootloader jump. |
| `Core/Inc/fdcan.h` | CAN IDs/periods, RGB function enum, IAP magic/flag address, app version. |
| `Core/Src/adc.c`, `Core/Inc/adc.h` | ADC2 DMA current/VBUS sampling, offset calibration, LPF + 5-sample median, scaling constants. |
| `Core/Src/tim.c` | TIM8 CH2/CH2N complementary PWM setup for back-EMF bleed. |
| `Bootloader/Core/Src/boot_main.c` | Flag handling, warm/cold GPIO init, boot decision and 100 ms host window. |
| `Bootloader/Core/Src/boot_can.c`, `boot_can.h` | IAP wire protocol: `0x04700000/1`, commands 0x01-0x06, ACKs, state machine. |
| `Bootloader/Core/Src/boot_flash.c`, `boot_flash.h` | 4 KB page erase/program, CRC32, `BootAppInfo_t`, validity rules. |
| `Bootloader/Core/Src/boot_jump.c` | MSP/reset-vector validation and jump contract. |
| `STM32G474xx_FLASH.ld`, `Bootloader/LinkerScript.ld` | App/bootloader memory map; app starts at `0x08006000`. |
| `CMakeLists.txt`, `CMakePresets.json`, `cmake/**` | Build targets, presets, toolchain flags, source lists. |
| `D-Y.ioc`, `.mxproject` | CubeMX source of truth for pins/clocks/peripherals. Regeneration affects `Core/` and `cmake/stm32cubemx/`. |
| `scripts/can_iap_tool.py` | Host reference implementation of the IAP protocol; CLI examples in README. |
| `README.md`, `CAN_IAP_GUIDE.md`, `CAN2_逻辑总结.md`, `三色灯控制逻辑与CAN通信协议.md`, `电源板V1.3_硬件变动与软件适配方案.md` | Protocol/pin/behavior references. V1.3 schematic PDF is the hardware authority. |

### CAN protocol quick reference

| ID | Direction | Meaning |
|---|---|---|
| `0x04028000 + node` / `0x04068000 + node` | FDCAN1 RX | BMS status / MOS state |
| `0x04038000`, `0x040E8000`, `0x04078000` (+node) | FDCAN1 -> FDCAN2 | BMS info/fault/temperature forwarded |
| `0x0400FF80` | FDCAN1 TX | BMS wake, 2 s |
| `0x04200000` | FDCAN2 TX | current int16 LE x100, VBUS uint16 LE x100, bleed state |
| `0x04400000` | FDCAN2 TX | alarm, fixed `0x02` (battery 2 removed), discharge/recharge MOS |
| `0x04500000/1` | FDCAN2 RX/TX | charge command/status (bypassed) |
| `0x04600000/1` | FDCAN2 RX/TX | RGB/buzzer command/status |
| `0x04700000/1` | FDCAN2 RX/TX | IAP command/response |

All frames are classic CAN, 29-bit extended IDs, 250 kbps, BRS off, DLC <= 8.

## Runtime/Tooling Preferences

- **Runtime:** bare metal; newlib-nano (`--specs=nano.specs`), links `-lm`, `-Wl,--gc-sections`, `--print-memory-usage`. No LTO, no `-Werror`, no `-Wextra`.
- **CPU flags:** `-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard`.
- **Build types:** Debug `-O0 -g3` + `DEBUG`; Release `-Os -g0` (no `NDEBUG`).
- **IDE:** VS Code with the STM32Cube extension (`stlinkgdbtarget`) is the documented debug path. `.vscode/c_cpp_properties.json` is stale/Windows-specific; use clangd + `build/Release/compile_commands.json`. The bootloader has no compile database.
- **Toolchains:** CMake presets use GCC. `cmake/starm-clang.cmake` exists but no preset selects it. Keil (ARM Compiler 5) and IAR projects are stale; keep them untouched unless explicitly asked.
- **CubeMX:** `D-Y.ioc` targets CMake only. After regeneration, diff `STM32G474xx_FLASH.ld`, `cmake/stm32cubemx/CMakeLists.txt`, the root startup file, and the root post-build commands.
- **No package manager for firmware;** the only Python dependency is `python-can>=4.0.0` (SocketCAN bitrate must be configured with `ip link`, `-b` is ignored for SocketCAN).

## Testing & QA

**There is no automated test framework, CI, lint, format, or coverage setup.** No `test/`, `*_test.c`, Unity/CMock/Ceedling, `enable_testing()`, or `.github/` exists. The only "tests" in the tree are vendored CMSIS DSP/NN suites that are not built. Do not claim tests passed without hardware evidence.

Minimum validation for every change:

1. Build both presets; inspect warnings (`-Wall` only) and the linker memory report.
2. For power/CAN/IAP changes, validate on an STM32G474 board with a scope and CAN capture. State exactly what was measured.

Manual acceptance workflow (from README/`CAN_IAP_GUIDE.md`):

- **Power-up:** LED2 (PB1) 1 Hz heartbeat, LED3 (PB2) 2 Hz during precharge, solid after discharge/24 V ready. Probe VBUS, `PA5`, `PB9`, `PB6` to confirm 400 ms precharge and 200 ms overlap.
- **CAN:** `python3 scripts/can_iap_tool.py -c can0 -q` should report app v1.4.2; `candump can0,04400000:1FFFFFFF` should show `00 02 01 01 00 00 00 00` in discharge.
- **Safety regressions:** cold start, bus disconnect/recovery, 5x fast disconnect stress, 25 V/30 V threshold boundary, CAN-loss recovery. Never accept a run that closes `PB6` without precharge.
- **IAP:** build Release, confirm `DBANK=0`, flash the bootloader once, run `-f build/Release/D-Y.bin --trigger`, verify re-PING and that 12 V/24 V stay alive during the transfer.

Known doc drift to check before relying on prose: README says a 2000 ms boot grace period (code: 4000 ms), describes active 85/84 V bleed (code: 999/999, duty 0), and `CAN_IAP_GUIDE.md` says 2 KB/64 pages (code: 4 KB/32 pages). `PC14` is **yellow**, not blue; `O_RGB_B` is a legacy schematic label.
