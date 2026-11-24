# Host changes the display is waiting for

The display firmware already reads the fields below. The ESP-Miner host
(`ESP-miner-bonanza`) does not send them yet, so the display shows a fallback
for each. Nothing here has been changed in the host repository.

Checked against `ESP-miner-bonanza` branch `bonanza-display-support` at
`8c15195` (2026-08-03).

## What is missing

| Field on the wire | Display shows it as | Fallback until the host sends it |
| --- | --- | --- |
| `hostname` | Name on the `HERO` device line | Device family, e.g. `BONANZA` |
| `networkDifficulty` | Globe line on `HERO`; `NETWORK DIFF` metric | `--` |
| `frequency` (optional) | `FREQUENCY` metric | `-- MHz` |
| `networkTarget` | `TARGET` row on the `TARGET` page | Worked out from `networkDifficulty`; first 4 digits, then dots |
| `bestShareHash` | `BEST` row on the `TARGET` page | Worked out from `best_difficulty`; first 2 digits, then dots |
| `lastShareHash` | `LAST` row on the `TARGET` page | `WAITING FOR HOST` |
| `asicTemp`, `asicHashrate`, `asicFrequency`, `asicVoltage` | Whole-chip columns of the `ASIC` page | `WAITING FOR HOST` notice in place of the page |
| `domainHashrate`, `domainVoltage`, `domainFrequency` | Per-domain block of the `ASIC` page | `NO DOMAIN DATA` beside the chip columns |

## The change: host name and network difficulty

Both required fields go in the `systemInfo` reply, which the display requests
every ten seconds. In `main/bap/bap_handlers.c`, `BAP_send_request()`,
`case BAP_PARAM_SYSTEM_INFO`, next to the existing `pool` lines:

```c
BAP_send_message(BAP_CMD_RES, "networkDifficulty", state->network_diff_string);

char *hostname = nvs_config_get_string(NVS_CONFIG_HOSTNAME);
if (hostname) {
    BAP_send_message(BAP_CMD_RES, "hostname", hostname);
    free(hostname);
}
```

Notes:

- `network_diff_string` is already kept up to date by the stratum tasks
  (`main/tasks/stratum_v1_task.c`, `stratum_v2_task.c`). It is empty until the
  first job arrives; the display ignores an empty value and keeps showing `--`.
- `nvs_config_get_string()` returns an allocated string, as its other callers
  in `main/http_server/http_server.c` show, so it must be freed.
- `nvs_config.h` is already included by `bap_handlers.c`.
- The display limits are 15 characters for the difficulty and 32 for the host
  name; longer values are cut.

## Optional: frequency

The display accepts `RES,frequency,<MHz>` but the host never sends one: its
subscription loop in `main/bap/bap_subscription.c` has no `BAP_PARAM_FREQUENCY`
case, and the display does not subscribe to it. The simplest fix is one more
line in the same `systemInfo` reply, sending the current ASIC frequency in MHz.

## The change: target and share hashes

The `TARGET` page shows three 256-bit numbers in hex. Each is sent as exactly
64 hex digits, most significant digit first (the way a block explorer prints a
block hash), in a `RES` message with the names above. The display accepts them
whenever they arrive, so they can ride on an existing subscription, for example
beside `best_difficulty`, which is sent about every three seconds when it
changes.

The host does not keep any of the three today; it keeps difficulties only:

- **`networkTarget`**: expand the job's compact `nBits` (`target` in the
  mining notify, already passed to `SYSTEM_notify_found_nonce()`): the low
  three bytes are the mantissa, the top byte is the length in bytes, so the
  mantissa's first byte is byte `32 - length` of the 32-byte big-endian value.
- **`lastShareHash`**: `mining_test_nonce_value()` in
  `components/stratum/mining.c` computes `hash_result` and returns only its
  difficulty. It needs to hand the hash back as well, and `account_share()` in
  `main/tasks/asic_result_task.c` needs to keep the latest one in the global
  state. The SHA output is the number least significant byte first, so reverse
  the 32 bytes before printing.
- **`bestShareHash`**: keep the same hash whenever
  `SYSTEM_notify_found_nonce()` in `main/system.c` raises `best_nonce_diff`.
  The all-time best survives a reboot only as a difficulty in NVS
  (`NVS_CONFIG_BEST_DIFF`), so after a restart the hash is unknown until it is
  beaten, unless the hash is stored in NVS too. Sending nothing in that case is
  fine: the display falls back to the difficulty.

## The change: per-chip detail for the ASIC page

Seven lists, each a `RES` message. An entry per chip, separated by commas; in
the `domain...` lists each entry is that chip's domains separated by `/`.
Numbers only, no units; decimals are rounded.

```text
$BAP,RES,asicTemp,58,57,60,59*HH              degrees C
$BAP,RES,asicHashrate,752,748,760,741*HH      GH/s
$BAP,RES,asicFrequency,800,800,800,800*HH     MHz
$BAP,RES,asicVoltage,2801,2795,2810,2799*HH   mV across the chip
$BAP,RES,domainHashrate,376/376,374/374,380/380,370/371*HH
$BAP,RES,domainVoltage,1401/1400,1398/1397,1405/1405,1399/1400*HH
$BAP,RES,domainFrequency,800/800,800/800,800/787,800/800*HH
```

The display takes up to four chips of four domains and shows whatever counts
arrive, so any subset of the lists works; a missing value shows as `--`. They
change slowly, so sending them every few seconds is enough, for example from
the subscription loop in `main/bap/bap_subscription.c` under the existing
`temperature` subscription.

Where the host has each value today, for the BZM chips on Bonanza:

- **`asicTemp`**: `temperature_c` in each `bzm_telemetry_sample_t`
  (`BZM_get_telemetry_snapshot()`, `components/asic/include/bzm_telemetry.h`).
- **`domainVoltage`**: `ch0_mv` and `ch1_mv` in the same sample, the bottom
  and top engine stack. **`asicVoltage`** is then the two added together, if
  the stacks are in series as the 2800 mV default suggests; check that.
- **`asicFrequency`, `domainFrequency`**: the per-PLL clocks,
  `domain_clock_mhz[asic][pll]` in `components/asic/include/bzm_bringup.h`.
- **`asicHashrate`**: `HASHRATE_MONITOR_MODULE.domain_measurements[i][0].hashrate`,
  which the web API already reports per chip (`main/http_server/system_api_json.c`).
  BZM has `hash_domains = 1`, so this is a whole chip.
- **`domainHashrate`**: there is no per-domain rate yet.
  `BZM_frequency_domain_stats_snapshot()` counts valid and rejected results per
  chip and stack, which a rate could be worked out from.

One thing to settle first: this source describes a BZM chip as two engine
stacks with two PLLs (`BZM_ENGINE_STACK_COUNT = 2`, `BZM_BRINGUP_PLL_COUNT 2`),
not four domains. The display does not care which, but the simulator's default
of four domains per chip is a guess at what was asked for, not something read
from the host.

## After changing the host

1. Rebuild and flash the ESP32 host firmware.
2. No display change is needed: the fields appear on the next `systemInfo`
   reply.
3. To preview the result without a host, the simulator's pretend host already
   sends all of these (`tools/sim.sh`; `hostname:off`, `netdiff:off`,
   `hashes:off` and `asics:off` turn them off, and `asics:4x2` sets the chip
   and domain counts).

## Open question

On this branch only board 1002 is defined, with BAP switched off in favour of
the RP2040 bridge, yet the display on the bench is talking BAP to its host.
Confirm which branch the 1004x host firmware is built from before making the
change there.
