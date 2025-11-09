# black-hole

Black Hole simulation project.

This is a fork of the original project with several enhancements.

## Original README

I'm writing this as I'm beginning this project (hopefully I complete it ;D) here is what I plan to do:

1. Ray-tracing : add ray tracing to the gravity simulation to simulate gravitational lensing

2. Accretion disk : simulate accreciate disk using the ray tracing + the halos

3. Spacetime curvature : demonstrate visually the "trapdoor in spacetime" that is black holes using spacetime grid

4. [optional] try to make it run realtime ;D

I hope it works :/

Edit: After completion of project -

Thank you everyone for checking out the video, if you haven't it explains code in detail: https://www.youtube.com/watch?v=8-B6ryuBkCM

**How the code works:**

black_hole.cpp and geodesic.comp work together to run the simuation faster using GPU, essentially it sends over a UBO and geodesic.comp runs heavy calculations using that data.

should work with nessesary dependencies installed, however I have only run it on windows with my GPU so am not sure!

LMK if you would like an in-depth explanation of how the code works aswell :)

## My Changes

- **Build System**: Replaced `cmake` & `vcpkg` with `xmake` for a simpler and more unified build experience.
- **Performance**:
  - Enabled high-performance GPU selection by default to ensure better performance.
  - Added a display for FPS and camera information to monitor performance.
- **Code Modernization**: Refactored the entire project to use modern C++20 features.
- **User Experience**:
  - Initialized an appropriate camera view point for a better out-of-the-box experience.
  - Added more stars to the simulation for a richer visual.
- **Code Quality**: Performed code formatting and cleanup for better readability and maintenance.

## Build Instructions

0. Ensure you have a C++20-compatible compiler and [xmake](https://xmake.io/) installed
1. Clone the repository: `git clone https://github.com/chen-qingyu/black-hole.git`
2. Into the newly cloned directory: `cd ./black-hole`
3. Build & Run the project: `xmake run`

Cross-platform, one-click operation, very convenient, and then you will see the beautiful black hole~
