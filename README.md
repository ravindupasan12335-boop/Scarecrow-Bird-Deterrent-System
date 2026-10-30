# 🐦 Scarecrow – Edge AI Bird Deterrent System

An intelligent bird-deterrent system designed to detect unwanted birds around areas such as hotel swimming pools and outdoor dining spaces.

The system combines **Edge AI audio classification, radar detection, PIR motion sensing, and automated audio deterrence** using an ESP32.

## 🚀 Project Overview

Traditional bird deterrent systems often rely on simple motion sensors or fixed sound playback.

This project aims to create a more intelligent system by combining multiple sensors and an Edge AI model to identify bird sounds before activating the deterrent system.

### Main Features

- 🎙️ INMP441 I2S microphone
- 🤖 Edge Impulse audio classification
- 📡 Radar-based motion detection
- 🔥 PIR motion detection
- 🔊 DFPlayer Mini audio playback
- 📶 ESP32 Wi-Fi connectivity
- 🌐 Web-based control dashboard
- ⏱️ Configurable activation duration and cooldown
- 🔘 Physical enable/disable button
- 💡 System status LED

## 🧠 Artificial Intelligence

The audio classification model was developed using **Edge Impulse**.

The model was trained to identify bird-related sounds, including classes such as:

- Crow
- Raven
- Rook

The trained model is deployed locally on the ESP32, allowing inference to be performed directly on the device.

No cloud connection is required for the actual audio classification.

## ⚙️ System Architecture

```text
              ┌─────────────────┐
              │    INMP441      │
              │   Microphone    │
              └────────┬────────┘
                       │
                       ▼
              ┌─────────────────┐
              │  Edge Impulse   │
              │ Audio Classifier│
              └────────┬────────┘
                       │
                       ▼
┌───────────┐   ┌───────────────┐   ┌──────────────┐
│   Radar   │──▶│               │──▶│   DFPlayer   │
└───────────┘   │     ESP32     │   │    + Speaker │
                │               │   └──────────────┘
┌───────────┐   │               │
│    PIR    │──▶│               │
└───────────┘   └───────┬───────┘
                        │
                        ▼
                ┌──────────────┐
                │ Wi-Fi Web UI │
                └──────────────┘
```

## 🔌 Hardware

| Component | Purpose |
|---|---|
| ESP32 | Main controller |
| INMP441 | Audio input |
| RCWL Radar | Motion detection |
| PIR Sensor | Motion confirmation |
| DFPlayer Mini | Audio playback |
| Speaker | Bird deterrent sound |
| Push Button | System control |
| LED | Status indication |

## 📌 Current Pin Configuration

### INMP441

| INMP441 | ESP32 |
|---|---|
| WS | GPIO 25 |
| SCK | GPIO 33 |
| SD | GPIO 32 |

### Sensors

| Sensor | ESP32 |
|---|---|
| Radar | GPIO 4 |
| PIR | GPIO 26 |

### DFPlayer Mini

| DFPlayer | ESP32 |
|---|---|
| TX | GPIO 16 |
| RX | GPIO 17 |

### Other

| Component | ESP32 |
|---|---|
| Button | GPIO 27 |
| LED | GPIO 21 |

## 🤖 Edge AI Pipeline

```text
Bird Sound
    ↓
INMP441 Microphone
    ↓
Audio Sampling
    ↓
Signal Processing
    ↓
Edge Impulse Model
    ↓
Prediction
    ↓
Crow / Raven / Rook
    ↓
ESP32 Decision Logic
    ↓
Deterrent Audio
```

## 📊 AI Results

Model performance and testing results will be documented here.

### Model

- Platform: Edge Impulse
- Input: Audio
- Device: ESP32
- Inference: Local / On-device
- Detection threshold: 95%

### Results

Add screenshots from Edge Impulse here:

- Confusion matrix
- Accuracy
- Validation results
- Model testing results
- Feature explorer

## 🌐 Web Dashboard

The ESP32 hosts a local web dashboard for monitoring and controlling the system.

The dashboard provides information such as:

- System status
- Radar status
- PIR status
- AI microphone status
- Speaker status
- Timer configuration
- Detection settings
- Wi-Fi configuration

## 📁 Repository Structure

```text
firmware/       → ESP32 firmware
edge-impulse/   → AI model and deployment information
hardware/       → Wiring and components
dashboard/      → Dashboard screenshots/resources
documentation/  → Project documentation
```

## 🔮 Future Improvements

- Improve bird classification accuracy
- Add more bird species
- Improve false-positive rejection
- Add ESP32 camera-based verification
- Develop adaptive deterrent sounds
- Add detection statistics
- Improve outdoor weather protection
- Test the system in real hotel environments

## 👨‍💻 Project

Developed as an engineering prototype exploring **Edge AI, IoT, embedded systems, and intelligent environmental monitoring**.