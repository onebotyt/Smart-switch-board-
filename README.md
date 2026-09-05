

# 🧠 SmartSwitch – Smart Classroom Control UI
(https://onebotyt.github.io/Smart-switch-board-/)

A modern, animated and feature-rich web dashboard for controlling an **ESP32-based Smart Classroom System**.
This UI works with **Blynk REST API**, **ESP32 Wi-Fi manager**, **presence detection**, **smart modes**, **schedules**, **timers**, and **advanced configuration tools**.

Built with 💙 using pure **HTML + CSS + JavaScript** — no frameworks required.



## 🚀 Features

### 🌟 1. Beautiful Modern UI

* Glass-morphism design
* Animated icons (Wi-Fi pulse, bulb glow, logo entrance animation)
* Mobile-friendly responsive layout
* Instagram-style animations

### 💡 2. Light Control (8 Relays)

* Tap to toggle
* Long-press/right-click rename
* Real-time sync with Blynk V0–V7
* Manual override support

### 🎭 3. Smart Modes

Modes instantly apply selected lights:

* **Teaching Mode** – All lights ON
* **Projector Mode** – Custom selection
* **Energy Saver Mode** – Custom selection

Each mode updates **Blynk V20** for automation.

### ⏱ 4. Schedules & Timers

* Add automatic Schedules (Start → End → Scene)
* Add countdown Timers per light
* Stored locally in browser
* Ultra-smooth UI with cards & animations

### 👤 5. Presence Detection

* PIR & RCWL Radar settings
* Shows live "Someone / No one" status
* Auto-off timer (V33)
* Fully synced with ESP32

### 📡 6. Wi-Fi Configuration Manager

Includes a complete Wi-Fi setup system:

#### Features:

* Add new WiFi networks
* Edit/Delete saved networks
* Scan nearby networks (ESP32 scanned list)
* Apply selected WiFi to device
* Set default WiFi
* Export WiFi JSON


### 🔔 7. Advanced Notification System

* In-app popup notifications
* Slide-down detailed notification modal
* ESP status, sensors, logs, connection info
* Logs auto-updating

### 🔧 8. Tools Panel

Includes:

* Refresh device
* Restart device
* Clear logs
* Export config
* Blynk connection setup
* Encrypted token storage

---

## 📡 Blynk (REST API) Integration

The UI uses the official Blynk Cloud REST endpoints:

| Purpose          | Virtual Pin |
| ---------------- | ----------- |
| Light Control    | V0–V7       |
| Presence         | V10         |
| PIR toggle       | V11         |
| RCWL toggle      | V12         |
| Mode indicator   | V20         |
| Restart          | V21         |
| Terminal send    | V25         |
| Auto-off timeout | V33         |
| Heartbeat        | V8          |

The UI automatically encrypts and stores tokens securely using a device fingerprint.

---

## 🧠 File Structure

```
index.html      → (Main UI
All animations, UI theming
Logic, Blynk API, WiFi Manager, Modals)
README.md       → Documentation
```

---

## 🛠 Requirements

### Hardware:

* ESP32 Dev Module
* Relay board (8-channel)
* PIR sensor
* RCWL radar presence sensor
* Optional: OLED, I²S mic, multiple PIR sensors

### Software:

* Web browser (Chrome recommended)
* Blynk IoT account (Cloud)
* ESP32 firmware with correct V-pin mapping

---

## 📥 Installation

1. Clone the repo:

```
git clone https://github.com/DemonAquarius/smartlight.git
```

2. Host locally or open directly:

```
open index.html
```

3. Enter your **Blynk Auth Token** in Settings → Tools

4. UI will auto-connect and start polling every 6 seconds.

---

## 🔧 ESP32 Firmware Requirements

Your firmware must:

✔ Publish relay states to V0–V7
✔ Publish presence states to V10
✔ Accept Wi-Fi setup via V30 (SSID), V31 (PASS), V32 (apply)
✔ Send logs on V25
✔ Respond to refresh & restart commands

> A full firmware template can be added if needed — ask me anytime.

---

## 🔒 Security Features

* XOR encrypted Blynk token saved with device fingerprint
* No third-party tracking
* No external scripts except Google Fonts

---

## 🧩 Customization

You can easily modify:

* Theme colors (CSS variables at the top)
* Light names (Rename tab)
* Functional pins (script.js)
* Mode behavior
* Sensor logic

---

## 🐞 Troubleshooting

### ❌ UI not connecting?

* Check Blynk token
* Ensure ESP32 is online
* Firewall must allow HTTPS

### ❌ No lights displayed?

* LocalStorage may be corrupted → Reset names in Settings

### ❌ WiFi Scan empty?

* ESP32 must send scanned networks to a V-pin

---

## 📝 Roadmap for future 

* ESP32 OTA update from UI
* Direct local WebSocket control without Blynk
* Dark/Light theme toggle
* Cloud backup & restore config
* QR setup for onboarding (AP, STA modes)
* Wi-Fi passwords stored encrypted
* Encrypted password storage with XOR + device fingerprint
---

## 🤝 Contributing

Pull requests are welcome!
You may open issues for bugs, feature requests, or improvements.

---

## 📄 License

MIT License

Copyright (c) 2025 Government Polytechnic Nainital College
Permission is hereby granted, free of charge, to any person obtaining a copy
...


Just tell me!
