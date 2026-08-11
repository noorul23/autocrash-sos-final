# AutoCrash SOS — Technical Project Report

A detailed technical report is generated from the supplied `autocrash.ino`, `code.docx`, `README.md`, architecture/flow/circuit/schematic/PCB diagrams, prototype photographs, serial-monitor outputs, and ambulance application screenshots.

**Local downloadable report:** `AutoCrash_SOS_Technical_Report.docx`

## Scope

- ESP32-centered crash detection and control
- YL-99 impact trigger
- Airbag-inspired pump and circuit-break confirmation sequence
- 10-second driver cancellation window with buzzer and push button
- Neo-6M GPS location acquisition
- SIM800L GSM call/SMS notification
- Firebase Firestore and Realtime Database logging
- Ambulance response application integration
- Prototype, schematic, PCB layout and 3D PCB documentation
- Test evidence for alert and cancellation paths

## Firmware-derived control sequence

`IDLE -> PUMPING -> ALERT_PENDING -> IDLE`

- A new debounced HIGH transition on GPIO27 starts the pump through GPIO14.
- A debounced HIGH on GPIO33 during pumping confirms the circuit-break event, stops the pump, activates the buzzer and starts the alert window.
- A button press on GPIO23 during the 10-second window cancels the alert.
- On timeout, the firmware sends a GSM call/SMS, writes a Firestore accident document and updates `/liveData` in Firebase Realtime Database.

## Key firmware pins

| GPIO | Signal | Role |
|---|---|---|
| 16 | GPS_RX | GPS UART receive |
| 17 | GPS_TX | GPS UART transmit |
| 4 | SIM800L_RX | GSM UART receive |
| 5 | SIM800L_TX | GSM UART transmit |
| 22 | BUZZER_PIN | Buzzer output |
| 23 | BUTTON_PIN | Driver cancel button, pressed LOW |
| 14 | MOTOR_PIN | Air-pump driver output |
| 27 | SWITCH_PIN | YL-99 impact input, active HIGH |
| 33 | LOOP_PIN | Circuit-break loop, broken HIGH |

## Key timing parameters

- Pump duration: 6000 ms
- Cancellation window: 10000 ms
- Digital input debounce: 50 ms
- GPS baud: 9600
- SIM800L baud: 9600
- USB serial diagnostics: 115200

See the full DOCX report for the complete technical narrative and embedded figures.