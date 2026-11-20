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

## The change

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

## After changing the host

1. Rebuild and flash the ESP32 host firmware.
2. No display change is needed: the fields appear on the next `systemInfo`
   reply.
3. To preview the result without a host, the simulator's pretend host already
   sends both (`tools/sim.sh`; `hostname:off` and `netdiff:off` turn them off).

## Open question

On this branch only board 1002 is defined, with BAP switched off in favour of
the RP2040 bridge, yet the display on the bench is talking BAP to its host.
Confirm which branch the 1004x host firmware is built from before making the
change there.
