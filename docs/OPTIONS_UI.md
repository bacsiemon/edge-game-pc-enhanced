# Options UI Controls & Layout

Specification and mapping for every setting currently in `mod_menu_camera_options.cpp` (shared across Main Options and Pause Options).

---

## Summary Table

| Category        | Option                 | Current / Target Values                                                          | Control           | Notes                                                                                                              |
| --------------- | ---------------------- | -------------------------------------------------------------------------------- | ----------------- | ------------------------------------------------------------------------------------------------------------------ |
| **Display**     | `fullscreen`           | `off`, `on`                                                                      | toggle            | Instant 2-state switch                                                                                             |
| **Display**     | `ui scale`             | `0.5`..`2.5` presets (clamped `0.1..2.5`)                                        | slider            | Preview value while dragging/stepping; call `ApplyUiScale` + save on release so mouse coords don't jump mid-drag   |
| **Display**     | `auto scale`           | `off`, `on`                                                                      | toggle            | Instant 2-state switch                                                                                             |
| **Graphics**    | `anti-aliasing`        | `off` + supported `2x`, `4x`, `8x`, `16x`                                        | popup             | Dynamic list from `g_aaLevels[]`; dynamic default from `GetLegacyDefaultAa(legacyFx)`                              |
| **Graphics**    | `chromatic aberration` | `off`, `on`                                                                      | toggle            | Instant 2-state switch                                                                                             |
| **Graphics**    | `prism vfx`            | `0` (`off`), `1` (`short`), `2` (`medium`), `3` (`long`)                         | slider            | 4 discrete stops (`0..3`); default is `1` (`short`)                                                                |
| **Graphics**    | `color theme`          | `pc`, `mobile`                                                                   | toggle            | Level color scheme, cube lighting & full-height wall gradients (`pc` default, live switch)                       |
| **Graphics**    | `finish effect`        | `pc`, `mobile`, `java`                                                           | popup             | Level-complete cube finish animation style (`pc` default)                                                          |
| **Graphics**    | `finish platform`      | `pc`, `mobile`                                                                   | toggle            | Level finish platform colors (`pc` default)                                                                        |
| **Audio**       | `sounds`               | `0`..`100`                                                                       | slider            | Live volume preview while dragging (default `100`)                                                                 |
| **Audio**       | `music`                | `0`..`100`                                                                       | slider            | Live volume preview while dragging (default `100`)                                                                 |
| **Audio**       | `music start`          | `land`, `fall`                                                                   | toggle            | Instant 2-state switch                                                                                             |
| **Audio**       | `music playlist`       | `pc`, `java`                                                                     | toggle            | Live toggle backed by `music/playlist.xml` (`pc` default, saved in `musicPlaylist`)                                |
| **UI**          | `time format`          | `def`, `new`                                                                     | toggle            | Instant 2-state switch                                                                                             |
| **UI**          | `edge timer`           | `2D`, `2D scaled`, `3D`                                                          | popup             | `2D` (default), `2D scaled` (scales to camera zoom level), or `3D` (Android world-space 3D edge timer)             |
| **UI**          | `timer`                | *(opens **Timer** submenu)*                                                      | submenu (`>`)     | Nested 3rd-level category                                                                                          |
| **UI -> Timer** | `type`                 | `off`, `level`, `rank`, `both`                                                   | popup             | 4 named choices; live corner timer in Pause, sample timer in Main Options (default `both`)                         |
| **UI -> Timer** | `size`                 | `0.25`, `0.5`, `0.75`, `1`, `1.25`, `1.5`, `1.75`, `2`                           | slider            | Live corner timer resize while dragging (sample timer in Main Options; default `1`)                                |
| **UI -> Timer** | `position`             | `TL`, `TR`, `BL`, `BR`                                                           | popup             | 4 named choices; live/sample corner timer preview behind popup (default `TR`)                                      |
| **Gameplay**    | `up key`               | 2 localized key modes                                                            | toggle            | Instant 2-state switch                                                                                             |
| **Gameplay**    | `skip intro`           | `off`, `on`                                                                      | toggle            | Instant 2-state switch                                                                                             |
| **Gameplay**    | `cam distance`         | `mobile` (`-1`), `0.5`, `0.65`, `0.75`, `1`, `1.25`, `1.5`, `2`                  | popup             | Named `mobile` mode + numeric multipliers (default `1`)                                                            |
| **Gameplay**    | `dolly zoom`           | `pc` (`-1`), `mobile` (`-2`), `0`, `5`, `10`, `15`, `20`, `25`, `30`, `40`, `50` | popup             | Two named presets (`pc`, `mobile`) + numeric FOV offsets (adds `40`, `50` to `g_fovPresets`; default `pc`)         |
| **Gameplay**    | `reset score`          | *(Main Options only)*                                                            | **confirm popup** | In-place `no` / `yes` popup (defaults to `no`, no row dots, no Left/Right trigger)                                 |

---

## Layout & Consistency

- **Submenu Navigation & `>` Indicators:** All rows opening nested submenus display a right-aligned `>` arrow (`Main Menu` rows `edge challenge`, `scores`, `options`; `edge challenge` rows `normal`, `extended levels`, `bonus levels`; `scores` rows `normal`, `extended`, `bonus`; Pause root row `options` at index `3`; Options root rows `display`, `graphics`, `audio`, `ui`, `gameplay`; `UI` row `timer`). `instructions` sits on `Main Menu` below `options` (removed from `edge challenge`).
- **Uniform Row Sizing & Widened Menus:** Widens menus to `368px` (`0x170`) with Pause root slots (`0x26` stride) matched to Pause Options, and shifts row text slightly up so under-value dots and slider bars fit cleanly above dividers. `ComputeMainOptLayout` and `ComputePauseOptRowH` dynamically compact row height when a category has many rows so rows never clip even at small virtual heights (`virtH = 512`).
- **Sample Corner Timer (Main Options):** When `type`, `size`, or `position` under **UI -> Timer** is focused or its popup is open in Main Options, render a sample corner HUD timer preview (also shown dimmed/sampled if `type` is `off` while adjusting `size` or `position`), matching the live corner timer visible behind Pause Options.

---

## Control Behavior Guidelines

### 1. Toggles (10 items)

- **Display:** 2 dots under the value. Lighter dot indicates the selected option.
- **Interaction:** Click / Enter / Left / Right toggles immediately and saves.

### 2. Popups (7 setting popups + 1 confirm popup)

- **Display:**
  - Setting popups show multiple horizontal dots under the value (lighter dot indicates the selected option). Action confirm popups (`reset score`) show no dots under the row.
  - Popup anchors **below** the option row by default to preserve visual flow, flipping **above** the row if there is insufficient vertical space below.
  - Shows all valid choices, marking the active choice and **highlighting the default value with a dot** (or `no` / `yes` with `no` focused by default for `reset score`).
- **Interaction:**
  - **Click / Enter** on the row opens the anchored popup.
  - **Up / Down / Mouse Hover + Enter / Click** picks a choice (or confirms/cancels for `reset score`), closes the popup, and saves.
  - **Esc / Cancel / Click outside** closes without changing the value / without resetting.
  - **Left / Right** on the row (when popup is closed) quick-cycles setting popups as a shortcut (disabled on `reset score`).

### 3. Sliders (5 items)

- **Display:**
  - A bar under the value. Lighter portion indicates progress.
  - A **dot/tick marker** on the track highlights the default value.
- **Interaction:**
  - **Mouse:** Click anywhere on the track to jump, drag thumb to scrub (pointer stays captured even if cursor leaves the row vertically), and release to save. Value changes during scrubbing play hover SFX (`2`).
  - **Keyboard / Gamepad:** Left / Right steps by preset or fixed increment (`5` for `sounds`/`music`, preset index for `prism vfx`/`ui scale`/`timer size`), playing hover SFX (`2`).
  - **Reset to default:** Right-click on the slider row or press `R` / `Backspace` / `Delete` (or gamepad reset button) to snap back to the default value.
  - **Special case (`ui scale`):** Update the displayed slider value while dragging/stepping, and call `ApplyUiScale` + `Game_SaveUserSettings` on mouse-up / Left-Right release (or cursor/focus change) without reverting `auto scale` to `off`.
