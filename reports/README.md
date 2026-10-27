# Reports

- [`NanoStat_dissertation.pdf`](NanoStat_dissertation.pdf): final project dissertation.
- [`NanoStat_poster.pdf`](NanoStat_poster.pdf): submitted project poster.

The poster was frozen before several final measurements and analyses were completed. Where a statement differs, the dissertation and the structured experiment documentation in this repository provide the later evidence. In particular, the poster's early one-week battery estimate was replaced by a usage-dependent model, and final NI myDAQ waveform measurements were completed later.

The full LaTeX authoring project was stored outside the archived NanoStat source folder and is not included here.


## Submitted Manuscript (Unpublished)

- **Title:** SereneStat: A Handheld Potentiostat for SWV and Demonstration with a Cortisol Aptasensor
- **Authors:** Chengyu Yang, A. Nisha Naeem and Sara S. Ghoreishizadeh
- **File:** [SereneStat_ISCAS2027_Submitted_Unpublished_Manuscript.pdf](SereneStat_ISCAS2027_Submitted_Unpublished_Manuscript.pdf)
- **Status (27 October 2026):** submitted to ISCAS 2027; decision pending. Unpublished author manuscript, not an accepted paper or a publisher-issued version of record.
- **File provenance:** supplied by the author as `nanostat_iscas_latex (4).pdf`; renamed for clarity. PDF contents have not been edited.
- **Project version:** SereneStat is the later name of NanoStat. This manuscript describes later project work; inclusion does not imply that all later features are present in the archived firmware or datasets in this repository.

### Known specification correction

Page 1, Section II-A describes the MX25R6435F as a **64 MB** flash. The specified device has a capacity of **64 Mbit (8 MiB)**. This is a bit/byte unit error in the submitted manuscript. The original PDF is preserved unchanged; this accompanying note records the correction. See the [Macronix datasheet](https://www.macronix.com/Lists/Datasheet/Attachments/8868/MX25R6435F%2C%20Wide%20Range%2C%2064Mb%2C%20v1.6.pdf).

The reported **41-day battery life is an estimate**, based on a 190 mAh battery and the manuscript's 24-measurement-per-day duty cycle, including daily BLE configuration and upload. It is not a completed 41-day discharge test or continuous acquisition claim.

**PDF SHA-256:** `397c86f23c3023fb8e5c3fd738e2c0fb71093a6720ca6e83fe8c3429070d0ed4`
