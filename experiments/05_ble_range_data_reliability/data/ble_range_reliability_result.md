# BLE Range and Data Reliability: Indoor Corridor Result

## Test conditions

| Item | Record |
|---|---|
| Environment | Narrow, unobstructed apartment corridor |
| Distance measurement | Tape measure; test point every `2 m` |
| Device movement | NanoStat fixed; laptop moved away |
| Stable operating distance | Through `18 m` |
| Observed connection limit | Approximately `28 m` |
| Antenna | Ceramic antenna |
| Antenna layout | Reasonable keep-out / clearance maintained |
| Link type | BLE connection to the measurement client |
| Up to 16 m | Connection/handshake and ping-pong at each point |
| Beyond 16 m | Three connection attempts and three ping-pong exchanges per point |

## Outcome

- Connection and ping-pong operation was stable through `18 m`.
- A BLE connection could still be established at approximately `28 m`.
- In the earlier full-data-path check at `6 m`, ping-pong transactions completed:
  `10/10`; dummy-cell measurements completed: `3/3`.

## Interpretation

The experiment demonstrates a stable BLE control link over 18 m in the tested
corridor, substantially exceeding the short separation expected between the
user's phone or laptop and NanoStat during a cortisol measurement. The observed
28 m connection limit is plausible for a clear corridor with favourable
multipath, but should not be generalised to rooms separated by walls or occupied
spaces. Complete SWV data transfer was verified at 6 m; the long-distance part
of the test assessed connection and ping-pong operation.

The protocol does not include packet sequence numbers, so packet-level loss was
not measured. RSSI, throughput and latency were also not logged.

## Suggested report wording

> In a narrow, unobstructed apartment corridor, the ceramic-antenna NanoStat
> maintained stable BLE connection and ping-pong operation through 18 m. A
> connection was observed at approximately 28 m, while ten ping-pong exchanges
> and three complete dummy-cell SWV transfers were verified separately at 6 m.
> The 28 m result is specific to the favourable corridor environment.

## Limitations

The test record does not currently include the client model, exact device
orientation, RSSI, timestamps, timeout count, reconnect count or long-range SWV
CSV transfers. These should be recorded in a repeat test if quantitative radio
characterisation is required.
