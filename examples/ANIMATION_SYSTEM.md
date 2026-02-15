# Animation System Examples

This directory contains examples demonstrating the integrated timeline and curve editor animation system in Plotix.

## Overview

The animation system consists of three main components:

1. **KeyframeInterpolator** - Core animation engine managing multiple channels and property bindings
2. **TimelineEditor** - UI for playback control, keyframe management, and time scrubbing  
3. **AnimationCurveEditor** - Visual editor for creating and editing animation curves

## Examples

### timeline_curve_demo.cpp
A basic demonstration showing the UI integration of the timeline and curve editor panels.

**Features:**
- Simple sine wave plot
- Timeline panel with transport controls
- Curve editor window for visual editing
- Keyboard shortcuts for panel toggling

**Controls:**
- `Space` - Toggle Play/Pause
- `[` / `]` - Step Back/Forward  
- `T` - Toggle Timeline Panel
- `Ctrl+P` - Toggle Curve Editor Panel

### advanced_animation_demo.cpp
A comprehensive demonstration showing the full animation system capabilities.

**Features:**
- Multiple animation channels (phase, amplitude, frequency, y-offset)
- Real-time property binding concepts
- Detailed workflow documentation
- Professional UI with comprehensive help text

**Controls:**
- All timeline controls from basic demo
- `Home`/`End` - Go to Start/End
- Extended curve editor features

## Usage Workflow

1. **Show Timeline**: Press `T` to display the timeline panel at the bottom
2. **Open Curve Editor**: Press `Ctrl+P` to open the floating curve editor window
3. **Start Playback**: Press `Space` to begin animation playback
4. **Edit Curves**: Use the curve editor to add and adjust keyframes
5. **Scrub Timeline**: Click and drag on the timeline ruler to scrub to specific times

## Animation Concepts

### Keyframe Types
- **Step** - Instant value changes
- **Linear** - Smooth linear interpolation
- **Cubic Bezier** - Custom curves with tangent handles
- **Spring** - Physics-based spring animation
- **Ease In/Out** - Various easing functions

### Property Binding
In a real application, you would bind animation channels to actual plot properties:

```cpp
// Conceptual example (not implemented in these demos)
keyframe_interpolator.bind_property("amplitude", &plot_data.amplitude);
keyframe_interpolator.bind_property("phase", &plot_data.phase);
```

### Timeline Features
- **Playback Controls**: Play, Pause, Stop, Step Forward/Backward
- **Loop Modes**: None, Loop, Ping-Pong
- **Time Display**: Current time and duration
- **Scrubbing**: Click and drag to jump to specific times

## Integration Notes

These examples demonstrate the UI integration of the animation system. In a production application:

1. Access the app's internal `KeyframeInterpolator` instance
2. Create animation channels for your properties
3. Bind those channels to your plot data or visual properties
4. The timeline will automatically drive the interpolator during playback
5. The curve editor provides visual editing of all animation channels

## Building

```bash
cd build
cmake --build . --target timeline_curve_demo
cmake --build . --target advanced_animation_demo
```

## Running

```bash
./build/examples/timeline_curve_demo
./build/examples/advanced_animation_demo
```
