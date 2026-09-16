# BLE range and data reliability

## Current result

An indoor range test was completed in a narrow, unobstructed apartment corridor.
NanoStat remained fixed while the laptop was moved away, and distance was
measured with a tape measure at `2 m` intervals. Connection and ping-pong checks
were performed at each point. Beyond `16 m`, the connection was attempted three
times and three ping-pong exchanges were requested at each distance.

The BLE link was stable through `18 m`. A connection could still be established
at approximately `28 m`, which was the observed limit in this corridor. An
earlier `6 m` test also completed `10/10` ping-pong exchanges and `3/3` complete
dummy-cell measurements, providing evidence for the full measurement data path.

This supports the following limited conclusion:

> Under unobstructed corridor conditions, NanoStat maintained stable BLE
> connection and command exchange through 18 m, with a connection observed at
> approximately 28 m. Complete dummy-cell measurement transfer was separately
> verified at 6 m.

The `28 m` value is an observed connection boundary for one favourable corridor,
not a guaranteed product range. The result depends on the client device,
antenna orientation, line of sight, multipath and radio environment. Packet-level
loss cannot be claimed without sequence numbers; the evidence is successful
connection and command exchange, plus completed measurement transfers at 6 m.

The result record is in
`data/ble_range_reliability_result.md`.

## Purpose

Quantify the wireless operating range and whether complete SWV datasets are transferred without missing, duplicated, or malformed records.

## Minimal protocol: no webapp change required

Use the existing measurement webapp and the same NanoStat on battery power throughout. Keep the NanoStat orientation, antenna orientation, room, and phone/tablet orientation fixed. Only change the line-of-sight distance between NanoStat and the client device, for example `1, 3, 5, 10, and 15 m`, or the maximum available indoor distance.

At each distance:

1. Connect once and record whether connection succeeds;
2. Perform ten existing `PING` commands and record responses/timeouts;
3. Perform ten existing `STATUS` commands and record responses/timeouts;
4. Run three complete dummy-cell SWV scans using the same profile and export each CSV;
5. Record reconnects, scan completion, received row count, expected row count, and elapsed time.

No firmware or webapp modification is required. The RTT menu can be used as a secondary check, but the webapp is preferable because it exercises the actual user data path and CSV export.

If time is limited, use `1 m`, `5 m`, and the maximum reliable distance first. The Android phone and tablet can be compared as two separate client conditions, but they should not be mixed in one summary table.

Record connection success, command timeout count, reconnect count, received sample count, CSV row count, scan completion status, and elapsed transfer time. The expected sample count must be taken from the selected SWV profile, for example 85 points for the 5.37 mV profile.

```text
command success rate = successful commands / attempted commands
scan completion rate = complete CSV scans / attempted scans
sample completeness = received data rows / expected data rows
```

Do not infer packet-level loss if the current protocol has no packet sequence number. Report CSV completeness and scan completion instead. A practical pass criterion is 100% successful `PING`/`STATUS` responses and complete SWV CSVs at the intended operating distance.

## Report placement

Methods: BLE test procedure and acceptance criteria. Results: system-level performance and data path validation. Report the tested environment and do not generalize beyond it.

## Required files

Add BLE logs, exported CSVs, distance table, analysis code, and reliability plot here. Keep phone/tablet, distance, orientation, and environment in the metadata. The current result is a preliminary pass record; add the client model, exact orientation, timeout count, reconnect count, CSV row counts and timestamps if another run is performed.
