# Piano Visualizer

A real-time piano AR visualizer and VST3 instrument host for Windows.

![Piano Visualizer](resources/demo.png)

Piano Visualizer combines your piano's MIDI input, a live camera feed, visual effects, and VST3 instruments into one application. Calibrate your piano in the camera view and see the visualization follow your performance in real time.

## Features

- 🎹 Real-time MIDI visualization
- 🎥 Live camera view with piano AR projection
- ✨ Animated notes, particles, flashes, waves, and effects
- 🎛️ VST3 plugin hosting
- 📂 Drag-and-drop VST3 loading
- 🔊 Real-time audio output
- 🎥 External visualizer window capture
- 💾 Persistent application and visualization settings


## Getting Started

### 1. Connect your devices

Connect a MIDI keyboard and a camera to your computer. Piano Visualizer will detect available devices automatically.  
 (You may need to open Camera Settings to select a device)

### 2. Calibrate your piano

Use **Choose Points** to mark the piano surface in the camera view. Adjust the projection settings to your liking.

### 3. Choose a visualizer

Use the built-in visualizer, or capture another visualizer application through **Capture Other Visualizer Window**.

### 4. Add a VST3 instrument

Drag a `.vst3` file or folder onto the application window. Your MIDI performance can then control both the visualizer and the VST3 instrument.

## External Visualizer Support

Piano Visualizer can capture another visualizer window and project it onto the physical piano through the camera/AR pipeline.

This makes it possible to use Piano Visualizer with visualizers other than its built-in one.

## Extensions

Piano Visualizer supports optional extensions that add additional functionality to the application.

### Virtual Camera

The [Virtual Camera Extension](https://github.com/Ominus-tch/VirtualCameraExtension) allows Piano Visualizer's output to be used as a camera in applications such as OBS Studio, Discord, web browsers, and other software that supports camera input.

[View the Virtual Camera Extension →](https://github.com/Ominus-tch/VirtualCameraExtension)

## VST3 Support

Piano Visualizer can load VST3 instruments, route MIDI to them, display plugin editors, and process their audio in real time.

Most compatible VST3 instruments should work normally, but some plugins may still crash under specific conditions. This is a known compatibility issue and is being investigated.

## Requirements

- Windows 10 or Windows 11
- A MIDI input device or MIDI software
- A compatible camera for AR functionality
- A VST3 instrument/plugin for audio output

## Download

Download the latest release from the [GitHub Releases](https://github.com/Ominus-tch/Piano-Visualizer/releases) page.

> **Note:** Piano Visualizer is currently unsigned, so Windows SmartScreen may display an "Unknown publisher" or similar warning when launching a release.

## Building

Clone the repository:

```bash
git clone https://github.com/Ominus-tch/Piano-Visualizer.git
```

Open the solution in Visual Studio and build the project using the included configuration.

## Development Status

Piano Visualizer is currently in **alpha**.

The core systems are functional, including MIDI input, camera capture, AR projection, the built-in visualizer, external visualizer capture, VST3 hosting, and real-time audio.

The project is actively being developed toward **v1.0.0**.

## Technologies

Built with:

- C++
- DirectX 11
- VST3 SDK
- Dear ImGui
- Windows Media Foundation
- WASAPI
- libremidi

The built-in visualizer is based on [MIDIVisualizer](https://github.com/kosua20/MIDIVisualizer) by [kosua20](https://github.com/kosua20).

## License

See [LICENSE](LICENSE) for the project's license.