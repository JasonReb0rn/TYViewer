# TYViewer

A viewer for the PC archives of Ty the Tasmanian Tiger and Ty the Tasmanian Tiger 2. The aim is an all-in-one model tool and TY level viewer and editor. Loading and viewing are in place. Editing is still ahead.

![preview](preview.png)

## Status

* **TY 1 models** are complete: mesh, materials from `global.mad`, and textures. A viewed character does not play its `.anm`. Skeletal playback is used by level critters.
* **TY 2 models** have finished meshes, UVs, and normals. Materials and textures are the remaining work, so this is about 85% overall.
* **TY 1 levels** are about 95% there for rooms, placed objects, and collision, with smaller gaps left. Grass, animated water, and a first critter simulation are in. Critter movement follows the TY 1 decomp, and the speeds and timers are still guesses.
* **TY 2 levels** are listed and inspected. They are not drawn yet.

Format notes are in [TYViewer/Reverse_Engineering_Documentation.md](TYViewer/Reverse_Engineering_Documentation.md).

## Using

Start the program. If `config.cfg` is missing from the program directory, one is created and the program exits. Fill it in and launch again.

```
Model=
TY1_Archive=C:\Path\To\TY\Data_PC.rkv
TY2_Archive=C:\Path\To\TY2\Data_PC.rkv
WindowResolutionX=1280
WindowResolutionY=720
Background=0.2 0.2 0.2
```

`TY1_Archive` and `TY2_Archive` are the game `Data_PC.rkv` files. Either one can be left empty. `Model` is an optional file to open on startup. Day to day, pick assets from the header menu. It has four lists, each with a search box: **TY 1 Models**, **TY 2 Models**, **TY 1 Levels**, and **TY 2 Levels**.

A model opens with its parts on the side. Show or hide each part from that list. A TY 1 level opens with **LEVEL PARTS** (the room meshes) and **OBJECTS** (one row per instance, with a kind filter, search, show/hide, and an info panel). Double-click an object to frame the camera on it. A TY 2 level logs the `.bni` header and a short sample of prop names. It does not draw the chunk.

The top bar has **Export**, **Export Raw**, **Recenter**, **Bounds**, **Collision**, and **Critters**. Export writes the open model as OBJ (with an MTL) or dumps the raw `.mdl` / `.mdg`. A loaded level is not exported.

## Controls

**WASD** moves the camera\
**MIDDLE MOUSE** rotates the camera\
**LEFT SHIFT** speeds movement up\
**LEFT CTRL** slows movement down

**1** toggles the grid\
**2** toggles model bounds\
**3** toggles model collider spheres. The lists those spheres come from are empty on the models and levels loaded today, so the key currently shows nothing\
**4** toggles bone spheres on models that have bones\
**C** toggles level collision meshes. Collision is hidden after a level loads\
**F** toggles wireframe\
**V** toggles vertex index labels\
**G** toggles grass cards. Grass is drawn when a level loads\
**P** pauses and resumes critters, when the level has any\
**T** writes the camera position and rotation to the log\
**NUMPAD + / -** changes the field of view

Typing in a search box takes the keyboard, so these shortcuts wait until that box is idle.

## Requirements

OpenGL 3.3.

To build: Visual Studio 2022, then build `TYViewer.sln` as Debug|x64. The libraries are [GLAD](https://glad.dav1d.de/), [GLFW](https://www.glfw.org/), [GLM](https://glm.g-truc.net/0.9.9/index.html), and [SOIL 2](https://bitbucket.org/SpartanJ/soil2).

## Still to do

* Finish TY 2 material and texture binding, including blend and overlay state.
* Decode TY 2 level placement and draw the chunks.
* Finish critter movement so the guessed speeds and timers match the game.
* Turn the viewer into an editor.
