#pragma once

// Entry points of the sr3_viewer sub-commands (dispatched from main() in viewer_main.cpp).

int runClear(int argc, char** argv);
int runListTextures(int argc, char** argv);
int runTexture(int argc, char** argv);
int runView(int argc, char** argv);
int runMesh(int argc, char** argv);
int runProbeMatlib(int argc, char** argv);
int runPose(int argc, char** argv);
int runAnimPose(int argc, char** argv);
int runScene(int argc, char** argv);
int runVehicle(int argc, char** argv);
int runZoneTile(int argc, char** argv);
int runZoneTileComposite(int argc, char** argv);
int runClmesh(int argc, char** argv);
int runClmeshTile(int argc, char** argv);
int runClmeshBatch(int argc, char** argv);
int runCity(int argc, char** argv);
int runWalkDemo(int argc, char** argv);
int runDriveDemo(int argc, char** argv);
int runTrafficLaneDemo(int argc, char** argv);
int runVintDoc(int argc, char** argv);
int runHudLiveDemo(int argc, char** argv);
int runMinimapDemo(int argc, char** argv);
int runPlaytest(int argc, char** argv);
