# Resize Stuck Analysis

## Problem Identified
From the logs, we can see that resize events are being triggered every single frame:
```
frame_counter=1, target=1280x721
frame_counter=1, target=1281x721  
frame_counter=1, target=1281x721
```

The frame counter never reaches `RESIZE_SKIP_FRAMES` (2) because new resize events keep arriving.

## Root Cause
The window manager is sending continuous resize events during window drag, preventing the debounce logic from ever completing.

## Current Behavior
1. GLFW resize callback fires → sets `needs_resize = true`
2. Main loop processes resize → increments counter to 1
3. Before counter reaches 2, another resize event arrives
4. Counter stays at 1, never reaches debounce threshold
5. `begin_frame()` fails → fallback recreation happens every frame

## Solution Options
1. **Increase debounce sensitivity**: Lower `RESIZE_SKIP_FRAMES` to 1
2. **Time-based debounce**: Use timestamps instead of frame counting
3. **Skip counter reset**: Don't reset counter on new resize events if already processing

The current approach with frame-based debouncing is too aggressive for continuous resize events.
