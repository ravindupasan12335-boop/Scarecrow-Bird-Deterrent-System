# 🐦 Scarecrow – Edge AI Bird Deterrent System

An intelligent **ESP32-based bird deterrent system** that combines **Edge AI audio classification, radar detection, PIR motion sensing, automated audio playback, and a Wi-Fi control dashboard**.

The project was developed as an engineering prototype to explore how **Edge AI, embedded systems, IoT, sensor fusion, and intelligent automation** can be used to reduce unwanted bird activity around areas such as hotel swimming pools, outdoor dining areas, and other open spaces.

---

## 📌 Project Overview

Birds such as crows and ravens can become a nuisance around swimming pools, outdoor dining areas, and hotel environments.

Conventional deterrent systems often rely on fixed sounds or simple motion sensors. Our approach combines multiple sensing methods with an **AI-based audio classifier** to make the system more intelligent and selective.

The system uses an **INMP441 I2S microphone** to capture environmental audio. The audio is processed using an **Edge Impulse machine-learning model**, while radar and PIR sensors provide additional motion information. The ESP32 then uses these inputs to determine when the deterrent system should be activated.

---

# ⚙️ System Architecture

```text
                 ┌──────────────────┐
                 │   INMP441 Mic    │
                 │   Audio Input    │
                 └────────┬─────────┘
                          │
                          ▼
                 ┌──────────────────┐
                 │   Edge Impulse  │
                 │  Audio Classifier│
                 └────────┬─────────┘
                          │
                          ▼
┌────────────┐     ┌──────────────┐     ┌────────────┐
│   Radar    │ ──▶ │              │ ◀── │    PIR     │
└────────────┘     │     ESP32    │     └────────────┘
                   │              │
                   └──────┬───────┘
                          │
                          ▼
                   ┌──────────────┐
                   │   DFPlayer   │
                   │  + Speaker   │
                   └──────┬───────┘
                          │
                          ▼
                   Deterrent Sound

                          │
                          ▼
                   ┌──────────────┐
                   │ Wi-Fi Web UI │
                   └──────────────┘
```

---

# 🤖 Edge AI

The AI component of the project was developed using **Edge Impulse**.

The microphone captures environmental audio and provides the input for the machine-learning model. The model was trained to identify bird-related sounds.

### Current Classes

- 🐦 Crow
- 🐦 Raven
- 🐦 Rook

The trained model is deployed to the ESP32, allowing inference to be performed **locally on the device** rather than continuously relying on cloud processing.

### AI Pipeline

```text
Environmental Sound
        ↓
INMP441 Microphone
        ↓
Audio Sampling
        ↓
Signal Processing
        ↓
Edge Impulse Model
        ↓
AI Prediction
        ↓
Crow / Raven / Rook
        ↓
ESP32 Decision Logic
        ↓
Deterrent Audio
```

---

# 🔗 Edge Impulse Project

The machine-learning model was developed, trained, tested, and deployed using **Edge Impulse**.

### 👉 [View Our Edge Impulse Project](https://studio.edgeimpulse.com/public/1146080/live)

The Edge Impulse project contains the AI development process, including:

- Dataset
- Audio samples
- Labels
- Impulse design
- Feature extraction
- Model training
- Model testing
- Classification results
- Deployment

---

# 🧠 Model Deployment

After training the model in Edge Impulse, it was deployed as an embedded inference library for the ESP32.

The ESP32 firmware uses the generated Edge Impulse library:

```cpp
#include <ravindupasan12335-boop-project-1_inferencing.h>
```

This allows the trained machine-learning model to run directly on the ESP32.

The generated Edge Impulse deployment files are included with the project.

---

# 📊 AI Model Results

The AI model is evaluated using Edge Impulse's training and testing tools.

| Parameter | Details |
|---|---|
| AI Platform | Edge Impulse |
| Input | Audio |
| Microphone | INMP441 |
| Target Device | ESP32 |
| Inference | On-device |
| Classes | Crow, Raven, Rook |
| Detection Threshold | 95% |

### Confusion Matrix

Add the exported Edge Impulse confusion matrix here:

```text
edge-impulse/results/confusion-matrix.png
```

### Model Testing

Add the Edge Impulse model testing result here:

```text
edge-impulse/results/model-testing.png
```

### Feature Explorer

Add the Edge Impulse feature explorer result here:

```text
edge-impulse/results/feature-explorer.png
```

> **Note:** Actual model accuracy and performance values should be added based on the results obtained from the Edge Impulse testing dataset.

---

# 🔌 Hardware

The prototype uses the following components:

| Component | Purpose |
|---|---|
| ESP32 | Main controller |
| INMP441 | I2S microphone / audio input |
| RCWL Radar | Motion detection |
| PIR Sensor | Motion detection |
| DFPlayer Mini | Audio playback |
| Speaker | Acoustic deterrent |
| Push Button | System enable/disable |
| LED | System status |
| MicroSD Card | Stores deterrent audio |

These components provide the sensing, processing, and deterrent functions required by the system.

---

# 📍 Pin Configuration

## INMP441 Microphone

| INMP441 Pin | ESP32 GPIO |
|---|---:|
| WS | GPIO 25 |
| SCK | GPIO 33 |
| SD | GPIO 32 |

## Sensors

| Sensor | ESP32 GPIO |
|---|---:|
| Radar | GPIO 4 |
| PIR | GPIO 26 |

## DFPlayer Mini

| DFPlayer Pin | ESP32 GPIO |
|---|---:|
| TX | GPIO 16 |
| RX | GPIO 17 |

## Other

| Component | ESP32 GPIO |
|---|---:|
| Push Button | GPIO 27 |
| Status LED | GPIO 21 |

---

# 🔊 Audio Deterrent

The system uses a **DFPlayer Mini and speaker** to play deterrent sounds.

Audio files are stored on a microSD card.

Example:

```text
SD Card
│
├── 0001.mp3
├── 0002.mp3
└── 0003.mp3
```

The ESP32 can select deterrent sounds according to the programmed system logic.

---

# 📡 Sensor Fusion

The system does not rely on a single sensor.

It combines:

```text
Radar
  +
PIR
  +
AI Audio Classification
  ↓
ESP32 Decision Logic
  ↓
Deterrent Activation
```

Using multiple inputs is intended to reduce unnecessary activations caused by individual sensor detections.

---

# 🌐 Wi-Fi Dashboard

The ESP32 includes a local web-based control dashboard.

The dashboard provides information and controls for areas such as:

- System status
- Radar status
- PIR status
- AI microphone status
- Speaker status
- Audio settings
- Timer configuration
- Wi-Fi configuration
- Detection settings

The system can operate as an ESP32 access point and can also connect to a local Wi-Fi network.

---

# ⏱️ System Logic

The simplified detection process is:

```text
START
  │
  ▼
Read Radar
  │
  ▼
Read PIR
  │
  ▼
Capture Audio
  │
  ▼
Run Edge AI
  │
  ▼
Bird detected?
  │
 ┌┴───────────────┐
NO                YES
 │                  │
 ▼                  ▼
Continue       Check sensor
monitoring     conditions
                    │
                    ▼
              Activate DFPlayer
                    │
                    ▼
              Play deterrent
                    │
                    ▼
                  Cooldown
                    │
                    ▼
                Monitor again
```

---

# 🛠️ Software & Technologies

## Hardware

- ESP32
- INMP441
- RCWL Radar
- PIR Sensor
- DFPlayer Mini
- Speaker
- MicroSD Card

## Software

- Arduino IDE
- C/C++
- Edge Impulse
- HTML
- CSS
- JavaScript
- Wi-Fi / HTTP

## Technologies & Concepts

- Edge AI
- TinyML
- Audio Classification
- Embedded Systems
- IoT
- Sensor Fusion
- Digital Signal Processing

---

# 🚀 Installation

## 1. Clone the Repository

```bash
git clone https://github.com/YOUR_USERNAME/scarecrow-edge-ai-bird-deterrent.git
```

## 2. Open the Firmware

Open the ESP32 firmware in Arduino IDE.

```text
firmware/scarecrow_v5/scarecrow_v5.ino
```

## 3. Install Required Libraries

Install the libraries required by the project, including:

- DFRobotDFPlayerMini
- Edge Impulse generated inference library
- ESP32 board support

## 4. Configure Wi-Fi

Configure your local Wi-Fi credentials when testing the system.

**Do not upload real Wi-Fi passwords, API keys, or other private credentials to GitHub.**

## 5. Upload to ESP32

Connect the ESP32 to your computer and upload the firmware.

---

# ⚠️ Important Notes

This project is currently an **engineering prototype** and has not yet been validated as a commercial bird-control system.

Outdoor environmental conditions such as:

- Wind
- Rain
- Human voices
- Vehicles
- Music
- Other birds
- Pool equipment
- Background noise

can affect sensor and AI performance.

Further real-world testing is required.

---

# 🔮 Future Improvements

Planned improvements include:

- [ ] Increase audio dataset size
- [ ] Improve bird classification accuracy
- [ ] Reduce false positives
- [ ] Add more bird species
- [ ] Improve sensor fusion
- [ ] Add ESP32 camera verification
- [ ] Add real-time detection statistics
- [ ] Improve outdoor enclosure
- [ ] Add weather protection
- [ ] Optimize power consumption
- [ ] Conduct longer real-world tests
- [ ] Develop adaptive deterrent sounds

---

# 👥 Team

This project was developed collaboratively by a **10-member engineering team**, with members contributing across hardware development, research, software, AI, dashboard development, assembly, and system integration.

## 🔧 Hardware, Assembly & Fabrication — 3 Members

Three team members were responsible for the physical construction and fabrication of the prototype, including:

- Hardware assembly
- Soldering
- 3D-printed component fabrication
- Physical component arrangement
- Prototype construction
- Mechanical integration

## 🔬 Research & Component Selection — 2 Members

Two team members focused on research and technical evaluation, including:

- Researching the reliability of the proposed system
- Investigating suitable sensors
- Comparing necessary hardware components
- Researching bird-deterrent approaches
- Evaluating component suitability for the project requirements

## 🌐 Dashboard Development — 2 Members

Two team members were responsible for the web-based control dashboard, including:

- Dashboard UI design
- HTML/CSS development
- JavaScript functionality
- ESP32 web-server integration
- System monitoring interface
- User controls and configuration

## 💻 Programming, AI & System Integration — 3 Members

Three team members worked on the core software and system integration, including:

- ESP32 programming
- Edge Impulse AI development
- Finding and preparing AI training data
- Audio classification
- Sensor integration
- Wiring and electrical connections
- Integrating the microphone, radar, PIR, DFPlayer, and ESP32
- Connecting the different system components
- Debugging and testing the complete system
- Integrating the AI model with the final firmware

---

# 🤝 Team Collaboration

The project involved collaboration across hardware, software, research, AI, fabrication, and system integration.

The different areas of contribution were combined to create a single working prototype:

```text
Research
   │
   ▼
Component Selection
   │
   ▼
Hardware + 3D Fabrication
   │
   ▼
AI Data + Model
   │
   ▼
ESP32 Programming
   │
   ▼
Dashboard Development
   │
   ▼
System Integration
   │
   ▼
Testing
   │
   ▼
Scarecrow Prototype
```

---

# 📜 License

This project is intended for educational and research purposes.

A suitable open-source license can be added to this repository depending on the team's requirements.

---

# ⭐ Acknowledgements

- [Edge Impulse](https://www.edgeimpulse.com/)
- [Espressif / ESP32](https://www.espressif.com/)
- [Arduino](https://www.arduino.cc/)

---

# 🔗 Project Links

### GitHub

**Scarecrow – Edge AI Bird Deterrent**

[(https://github.com/ravindupasan12335-boop/Scarecrow-Bird-Deterrent-System.git)]

### Edge Impulse

**View the AI Project**

https://studio.edgeimpulse.com/public/1146080/live
