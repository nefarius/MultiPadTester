# Rendering diagnostics

Use this capture when a display flickers or changes refresh while MultiPad Tester is open. It records when the application calls DirectX `Present`. It does not record the refresh rate the monitor actually uses. Pair it with PresentMon to see that second half.

## Capture a session

1. Open **Preferences** from the window's system menu.
2. Check **Capture rendering diagnostics** and click **OK**. Capture starts immediately and stays off until you do this. The choice is saved for the next launch.
3. Leave the window idle for about ten seconds.
4. Move the mouse over the window for about ten seconds. If a controller is available, use it for another ten seconds.
5. Clear the checkbox and click **OK**, or exit the application. The files are written at that point. Closing the process in Task Manager can leave the frame file unwritten.
6. Click **Open diagnostics folder** if you need the location. The files are also under `%APPDATA%\MultiPadTester\diagnostics`.

Send the matching `render-<timestamp>.txt` and `render-<timestamp>.csv` files, plus the PresentMon file from the same interval.

## Files

`render-<timestamp>.txt` describes the session: executable version, configured refresh rate, VSync, monitor, GPU, and the DXGI swap-chain mode (`FLIP_DISCARD`, `FLIP_SEQUENTIAL`, or `DISCARD`). A device reset appends a new swap-chain description.

`render-<timestamp>.csv` has one row per displayed frame (`kind` is `frame`) and one row for lifecycle changes (`kind` is `event`). Times in the `*_us` columns, except `qpc_us` and `stats_sync_qpc`, are microseconds from the start of that capture on a monotonic clock. `qpc_us` is sampled immediately after `Present` returns.

These columns explain the application cadence:

| Column | Meaning |
| --- | --- |
| `inter_present_us` | Time since the previous `Present` started. This is the application frame interval. |
| `present_block_us` | Time spent inside `Present`. A large value means the driver or compositor blocked the call. |
| `deadline_lateness_us` | How late the call was relative to the scheduled deadline. |
| `early_wakeups` | Times the message loop woke before the deadline since the previous frame. |
| `present_hresult` | `Present` result as eight hexadecimal digits. `00000000` is success. |
| `stats_valid` | `1` when DXGI returned frame statistics for that row. |

`dropped_frames` in the text file counts frames that were not stored after the in-memory cap. A normal ten-second capture does not reach that cap.

## PresentMon

Install [PresentMon](https://github.com/GameTechDev/PresentMon/releases) and record the same period. Current builds use:

```text
PresentMon.exe --process_name MultiPadTester.exe --output presentmon.csv --timed 30
```

If that build rejects those options, run `PresentMon.exe --help` and use its process-name and output options. Start it just before clicking **OK** with diagnostics enabled, and stop it after the application files are written.

Compare these PresentMon columns with the application CSV:

- `msBetweenPresents` against `inter_present_us / 1000`
- `msInPresentAPI`, when present, against `present_block_us / 1000`
- `PresentMode` against `swap_effect` in the text file

Steady `inter_present_us` with varying `msBetweenPresents` means the compositor or display is changing the visible cadence. The scheduler is not the source of that flicker. Varying `inter_present_us` means the application's own frame pacing is uneven.
