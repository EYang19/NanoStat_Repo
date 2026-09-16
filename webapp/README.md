# NanoStat Web Application

`index.html` is the browser interface used to connect to NanoStat over BLE, configure measurements, display results, and export CSV data.

## Run Locally

Web Bluetooth requires a secure context. `localhost` is accepted by supported desktop browsers:

```bash
cd webapp
python3 -m http.server 8000
```

Open `http://localhost:8000` in desktop Chrome or Edge and use the connection control to select NanoStat.

Windows helper launchers are retained for the original laboratory workflow.

## Browser Support

Web Bluetooth availability is controlled by the browser and operating system. Chromium-based desktop browsers are the intended target. Chrome on iPhone and iPad uses the iOS WebKit engine and does not provide the Web Bluetooth API required by this application.

## Data Flow

The browser sends protocol parameters and start commands over a BLE UART-like service. NanoStat performs time-critical waveform generation locally and returns status plus measurement data for plotting and CSV export. Browser and BLE timing are therefore outside the pulse-generation loop.

