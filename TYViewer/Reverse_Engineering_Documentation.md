# TYViewer format notes

This describes the formats the viewer loads, and what the loader actually does with them.

## Status

* **TY 1 models** are complete: mesh, `global.mad` materials, and textures. A viewed character does not play its `.anm`. Skeletal playback is used by level critters.
* **TY 2 PC models** have finished meshes, UVs, and normals. Materials and textures are the remaining work, so this is about 85% overall. The geometry fix landed on 3 October 2026 (`48f61ff` for the UV shift, `4af66e6` for the vertex-block anchor).
* **TY 1 levels** are about 95% there for rooms, placed objects, and collision. Grass, animated water, and a first critter simulation are in.
* **TY 2 levels** are listed and inspected. Placement records are not decoded, so a chunk is not drawn.

The aim is an all-in-one model tool and TY level viewer and editor. Editing is still ahead.

## TY 2 PC models (`MDL3` / `MDG3`)

A model is treated as TY 2 when a `.mdg` sits beside the `.mdl` in the archive. `loadTY2MDL3` reads the MDL3 header and validates the count fields. It does not check the four-byte magic, though the PC files begin with `MDL3` and `MDG3`. Mesh geometry comes from the MDG. The MDL supplies component names, bounds, and the texture-slot list.

`mdg.cpp` still contains a PS2 parser, `parseStrip`, taken from a reference converter. It runs only when the VIF marker `00 80 02 6C` appears in the first 1000 bytes. That parser was reference material while the PC format was being worked out. PC `Data_PC.rkv` files do not take that path. The PC format below is a different file.

### MDL3 header

`loadTY2MDL3` reads these fields. Offsets are from the start of the MDL.

| Offset | Field |
| --- | --- |
| +0x04 | Component count (uint16) |
| +0x06 | Texture count (uint16) |
| +0x08 | Anim-node count (uint16) |
| +0x0A | Ref-point count (uint16) |
| +0x0E | Mesh count (uint16) |
| +0x1E | Strip count (uint16) |
| +0x30 | Bounds origin (3 floats) |
| +0x40 | Bounds size (3 floats) |
| +0x50 | Component-descriptions offset (uint16) |
| +0x54 | Texture-list offset (uint32) |
| +0x58 | Ref-point offsets (uint32) |
| +0x5C | Anim-node data offset (uint16) |
| +0x64 | Anim-node lists offset (uint32) |
| +0x68 | Object-lookup table (uint32) |

Texture names are a pointer table at the texture-list offset, one uint32 per slot. Components are 0x40 bytes apart from the component-descriptions offset. The name pointer is at +0x30 inside each component.

The object-lookup table is a texture-by-component grid of int32 mesh references:

```
lookup = ObjectLookupTable + (textureIndex * 4 * ComponentCount) + (componentIndex * 4)
```

A reference of 0 means that pair has no mesh. Otherwise the value is a file offset in the MDG, and +0x0C in that mesh header is the next mesh in the linked list.

### MDG3 layout

```
+0x00  "MDG3"

Then mesh headers, reached through the object-lookup table.
Then one vertex block holding every mesh, including collision meshes.
Then a 12-byte tail: the last vertex's color dword and UV.
```

The vertex block is not scanned for. Its start is:

```
fileSize - 12 - (sum of every mesh's base + duplicate) * 48
```

The sum includes collision meshes. The fixed part of a mesh header ends at +0x10. Counting the strip-descriptor bytes as part of the header would start the block inside vertex 0.

### Mesh header

The loader treats the fixed header as 16 bytes and reads strip descriptors at +0x10. A comment in `parseMDGPC` still says the descriptors start at +0x14. That comment is stale. The read at +0x10 is what the code does, and the meshes built from it are correct.

| Offset | Field | Use |
| --- | --- | --- |
| +0x00 | uint16 base vertex count | Mesh size |
| +0x02 | uint16 | Unread on the PC path |
| +0x04 | uint16 duplicate vertex count | Mesh size. Duplicates connect strips |
| +0x06 | uint16 strip count | How many descriptors to read |
| +0x08 | | Unread on the PC path |
| +0x0C | int32 next mesh | Linked list. 0 ends it |
| +0x10 | uint16 descriptors, `stripCount` of them | Low byte is that strip's vertex count. High byte is unused |

Mesh vertex count is base + duplicate. The low bytes are used for indexing only when their sum matches that count (see below).

### Vertex record (48 bytes)

Color and UV belong to the previous vertex. Position, weight, and normal belong to this vertex. The shift does not reset at a mesh boundary, and it continues through collision meshes that are not drawn. After the last record, the 12-byte tail holds the final vertex's color dword and UV.

```
+0     uint32   Color of vertex i-1 (D3D 0xAARRGGBB). Dummy on record 0.
+4     float2   UV of vertex i-1. V is stored flipped: v = 1 - rawV.
+12    float3   Position of vertex i.
+24    float    Weight of vertex i. Stored in skin[0]. Often 1.0 on static props.
+28    float2   Unread. Often constant per mesh (27.0, 27.0).
+36    float3   Normal of vertex i. +44 is the Z component, not a separate pad.
```

The viewer still writes white for every vertex. The shifted color dword is in the file and is not applied yet.

### Strips

PC meshes are one run of vertices with degenerate connectors between strips. Index generation in `content.inl` works like this:

1. Read each descriptor's low byte.
2. If the sum of those counts equals the mesh vertex count, or that sum plus one or two degenerates per gap between strips equals the vertex count, emit one triangle strip per descriptor and step the start index by the matching gap.
3. Otherwise split the mesh on duplicate-position connectors and strip those ranges.

That is why a descriptor sum that does not match the header still produces a complete mesh. High bytes such as `0xA0`, `0x20`, `0xB0`, and `0x60` are logged only. Their meaning is still open.

A mesh with fewer than three vertices off the origin is skipped. A mesh with at most eight unique positions and at most two unique values on each axis is skipped as bounds debug geometry.

### Collision meshes

A texture slot whose name starts with `CM_` or `cm_` is collision. The parser consumes its vertices so the following mesh stays aligned, and then drops the mesh. Those shells are absent from the model, so the level collision toggle cannot show them.

### Textures the viewer binds

The material string on a mesh is the MDL3 slot name. The DDS is chosen by `loadTextureWithFallbacks`, and a candidate is used only when that file loads:

1. `SlotName.dds`
2. The same name with trailing digits removed (`A049_Elle01` to `A049_Elle`)
3. The same name with a trailing `Glass` or `_Glass` removed
4. The model file's base name
5. The last texture that loaded for an earlier slot of this model

Step 5 will reuse another slot's texture when the name does not resolve. The part list tags names ending in Glass, Spec, or Overlay. Those tags are labels only. TY 2 meshes do not read a material database, so there is no blend, alpha test, or overlay pass.

### Still open

* Apply the shifted vertex color, including any alpha it carries.
* The float pair at +28. It is often constant per mesh and may be scale or bounds.
* The weight at +24 on anything other than a static prop. It is stored and unused.
* Strip-descriptor high bytes.
* Expose `CM_` meshes as a collision layer the viewer can toggle.
* Render state for glass, spec, and overlay. No binary fields for blend or depth are decoded.
* A few naming patterns are visible in the archive and are not what the loader does. `_Z` plus a word (`A120_ZBlueSpec` to `a120_bluespec.dds`) is its own image. Some slots share another model's DDS (`A120_AworkerKoala` to `a110_worker_koala.dds`). A small UV window with no DDS of its own, such as Elle's eyes, sits inside the model atlas. Orchid eye and shine slots span almost the whole square and have no DDS in `Data_PC.rkv`. The loader's step 5 can bind a neighbouring texture in those cases. A stricter rule would leave the slot missing.

## TY 1 models

A `.mdl` with no sibling `.mdg` is TY 1. Geometry is the subobjects and segments inside the MDL. Positions and packed normals come from the segment. UVs are 4.12 fixed point (`int16 / 4096`). U is the raw value. V is `1 - raw/4096`, including values outside 0–1. `abs(raw/4096 - 1)` matches that flip only while stored V is at most 1. Above that it negates V, so a strip that crosses 1 runs the texture backward. The loader does not use `abs`.

`global.mad` supplies the draw state: texture alias, blend, `invisible`, `effect = grass`, masked cutout and `aref`, animated UVs, and indirect water. A material pointer of `0xFFFFFFFF` is an empty name. The strings that follow it are real materials.

Bones and collider spheres are parsed from the MDL header. **4** draws the bone spheres on a model that has them. **3** is the collider-sphere toggle. The collider lists on the models and levels in use are empty, so that key currently shows nothing. Character `.anm` playback on a viewed model is not implemented. Critters are the path that skins a skeleton.

## TY 1 levels (`.lv2`)

`Data_PC.rkv` contains 33 `.lv2` files (`a1.lv2` through `e4.lv2`, `z1.lv2`, `z2.lv2`, plus `*ex.lv2`). A main level is a Krome MapEd script, "Data File version 2". The `name setup` block names the terrain models:

* `ground = room_a1_01.mdl`
* `envcube = env_a1.mdl`
* `overlay_* = room_a1_02.mdl, ...` (extra comma-separated tokens are flags or material names)

Those `.mdl` files are in the same archive, already in world space, and are the level mesh. Later `name TY` / `name STATICBRIDGEFLAT1` / `name OPAL` blocks are placed instances. A blank line separates instances of the same type. Each instance reads the first `pos`, `rot`, and `scale`. `rot` is pitch, yaw, roll in radians (a missing comma is still three numbers). A missing `rot` is zero and a missing `scale` is 1. A `scale` whose largest component is above 64 is a flock roam box (`count` is how many animals the game scatters there), so that mesh stays at 1. `bVisible = 0` starts that instance hidden.

The type name is not a filename (`staticbridgeflat1.mdl` does not exist). The game strips a leading `STATIC` and looks the rest up, case-insensitively, in `global.model` (`StaticProps`, `StaticFXProps`, and the other prop lists). `BridgeFlat1 prop_0111_bridge_flat_01` is the mesh for `STATICBRIDGEFLAT1`. Names that are not in those lists use the descriptor the game registers in code, or, when that is unique, the `.mdl` whose name ends in the type. `TY` maps to `Act_01_ty` and is drawn when that model is in the archive.

`OPAL` and `THUNDEREGG` use the mesh for that level's element: fire on a1–a4 (`Prop_0270_FireOpal`, red `Prop_0084_ThunderEgg`), ice on b1–b4 and d4 (`prop_0380_IceOpal`, `prop_0573_bluethunderegg`), air (green) on c1–c4 and d1–d3 (`prop_0382_AirOpal`, `prop_0571_greenthunderegg`), earth (yellow) on e1–e4 (`Prop_0381_EarthOpal`, `prop_0572_ylwthunderegg`), and rainbow scales on z1–z4 (`Prop_0218_RainbowScale`; thunder eggs stay red). An unrecognized level keeps the fire pair.

`DIALOG`, `TRIGGERBOX`, `TRIGGERSPHERE`, `SOUNDPROP`, `SCRIPT`, `PATH`, `WATERVOLUME`, and names that start with `CAMERA` stay in the object list and have no mesh. `WATERVOLUME` is still a water volume: a large `scale` becomes its roam box, and selecting it draws that box. `RESTART` is the checkpoint. It places `Prop_0018_Thunderbox` (the outhouse) and `Prop_0092_DunnyRoll` (the ground piece) at the same `pos` and `rot`. Selecting one draws the 500-unit approach sphere the game uses to pop the dunny. That radius is not stored in the level.

The object list is one row per instance, with search, a kind filter, and show/hide, and sits on the left. LEVEL PARTS stays the room meshes. Several instances share one cached model. The instance matrix is applied at draw time, so hiding a room part does not move a prop, and hiding one prop does not hide the others that use its model. Props are instanced, and both room parts and props are frustum culled. `a1ex.lv2` is the companion object list for `a1.lv2` (the same `{id}ex.lv2` pattern for the other zone levels). Opening either name loads the base level's rooms and both object lists. Companion instances are tagged `ex` in the object list. **EXTRAS** shows or hides that set without changing each object's own checkbox. Those files are often instance lists with no setup block; any setup they do contain is not used for rooms. `.scn` and `.cam` are not levels.

Portals draw a billboard. The vertical seat (`drawLiftY` / `seatBottom`) is a guess.

After a level loads, collision shells are hidden. **C** and the Collision button show them. Grass cards are built from room triangles whose material has `effect = grass` in `global.mad`, using `grass_types.ini`, and they are drawn. **G** toggles them.

### Collision shells

White level collision is more than the parts named `Collide_*`. The same shells are also named `Collision`, `Collsion`, `Colide`, or `collde`. Parts whose names contain `invis` are collision even when the material has a DDS: B2 snow on `TY_B2_001` (`invis_snowmountain`, `invis_sidewall1`, stairs, cave, and rock steps) and blue ice on `TY_B2_005` / `TY_C2_018` (`invis_iceplane02`, `invis_ice_isle01`, `invis_riverbed01`). Those shells sit on the real terrain. An `invis` part stays collision even when its material has `effect = grass` and is not `invisible 1`. `ty_b2_029` and `ty_b2_031` are grass emitters and are also the drawn dirt on sector parts (`grassbottom02`, `icehill`, `cave02`, `clump_trees*`). The flag spawns grass on those surfaces. It does not keep `invis_grass*`, `invis_walls03`, `invis_icewall02`, `invis_snowgrass01`, or `invis_grass_sec08` visible. Z1 `invis_edgesection*` is the same kind of shell. `snow_path03` on `Room_b2_01` (`TY_B2_001` / `TY_B2_005`) is a collision ribbon; the drawn path is the sector meshes in `Room_b2_02`. Untextured parts whose names start with `C_` / `C ` are collision too. A3 `C_Geom_Quicksand` is the quicksand gameplay volume, the only one in the game. It uses the surrounding ground textures (`TY_A3_004`, `TY_A3_003`, `TY_A3_006`, `TY_A3_014`, `TY_A3_015`, `TY_A3_031`, `TY_A1_003`), so it is collision even though those materials have a DDS. The drawn pit is the sector part `003 000 006 Geom_Quicksand_02` and stays visible. B1 `Object01` and `Object03` on `TY_A1_024` are collision shells left with default Max names. They draw white. `Object02` is already hidden because it uses `T0103_01_c`. `TY_A1_024` also textures the real cavern in `Room_b1_02`, and other `Object01` / `Object03` parts are scenery, so only that part-and-material pair is collision. A `global.mad` material with `invisible 1` is collision: C3 `TY_C3_realgrasstrim` (the white island skirts) and B3 `ty_b3_001_FX15` / `ty_b3_002_FX15`. Meshes on `T0103_01` or `T0103_01_*` (any suffix), a material named `Collision`, or `Material #*` are collision. Suffixed slots have no DDS and draw the white fallback. The unsuffixed name binds `t0103_01.dds`, a red/yellow collision bitmap (a2 `Collied_Bridge`, z1 perimeter `Line04`, and the same shells on other levels).

Other white patches on B2, C1, E2, Z1, and Z2 are level art. The material is a variant of a DDS that is in the archive: `TY_C2_010_a` to `ty_c2_010.dds`, `TY_C1_008a` to `ty_c1_008.dds`, and the same for `_nograss`, `_no_grass`, `_reed`, `_overlay`, and a trailing `_` plus a letter. The loader uses that base texture. Names ending in `_grass` or `_lessgrass` are drawn ground (`TY_a2_022_grass` on A2 `Ground01`, `TY_Z2_003_grass`, `TY_A2_005_lessgrass`). `global.mad` aliases them to the real texture, and `effect = grass` only spawns grass on that surface. `invisible 1` is what hides a guide: `TY_B2_001_grass`, and the first `Ty_C2_033_grass` block (a later block with the same name drops the flag; the loader keeps the first, so those C2 Grass Strip meshes stay collision). Sector parts whose names end in ` trees` (`000 000 000 trees` and `001 000 000 trees` on B2, and A2 `Outer Trees`) are level geometry. `clump_trees*` stays visible. Hiding ordinary part names (`icehill`, `cave02`, `mountainside2`) removes the terrain. `TY_B2_001`, `TY_B2_005`, and `TY_C2_010_a` are drawn materials. `T0103_01_*` is left on the white fallback, so collision stays white when the toggle is on.

B2 `tree_walls` is the tall vertical collision shell around the level. It uses `TY_B2_001` (opaque snow) and writes depth in front of the trees, so it is hidden with the other collision meshes. The tree wall that stays visible is the tall cards on `clump_trees*` using `Prop_B2_Page_treepage`, `ty_b2_020`, and `ty_b2_021`. Those three materials are `masked` with an `aref` in `global.mad`. Pixels below that alpha are discarded, so the empty part of the card does not hide the trees and ground behind it. E1 `A_TreeWall` is those cards, and stays visible.

Terrain segment UVs follow the same 4.12 rule as models. Terrain tiles go past 1 (B3 `Room_B3_04` cliff strip stores V `4, 3, 2, 1, 0, -1, …` along the wall). Props stay inside 0–1.

### Water

Animated UVs and indirect water come from `global.mad` (scroll, ripple, and clamp). That ripple is the GameCube indirect texture. The PC port (and the Xbox original) also move the vertices. A `WATERVOLUME` instance marks the gameplay volume and is drawn as a box when selected.

The drawn surface is `Room_<level>_water.wmh` in `Data_PC.rkv`. `.wml` is the same file with fewer vertices; the viewer uses `.wmh` and falls back to `.wml`. The coarse water parts in the room `.mdl` (A3 `003 000 004 ocean`, E2 `ocean`, A1 `A1_Water`) are hidden while the `.wmh` is loaded. They stay in the part list. Collision shells (`C_Water`, `invis_waterplane`) are not those parts, so critter floors are unchanged.

`water_types.ini` is the parameter file. Each `[section]` starts as a copy of `[default]` (`TY.exe` `0x5becc0`). A chunk's type string is matched to a section, case-insensitive (`0x5bccd0`). E2's mesh is section `Z2_water` even though the room part is named `ocean`. Two waves, each with `waveNAnimSpeed`, `waveNDir` (x, z), `waveNHeight`, and `waveNFreq`. `envMapAnimSpeed` is stored doubled; a later `animSpeed` line replaces it (`0x5be880`, `0x5bea38`).

A3 `[ocean]`, C3 `[c3_water]`, and D4 `[D4_Water]` set `wave1Height=-10` and `wave1Freq=0.0001`. That wavelength is about 63,000 units, so the whole surface rises and falls together by about ±10, once every 6.3 s (`animSpeed` 1). Wave 0 (`height` 2, `freq` 0.01, `animSpeed` 3) is the smaller ripple along X. Other levels use heights of ±1. A few types use 0 and stay flat.

Phases are not in the ini. Wave 0 starts at 0 and wave 1 at 13.3. Each frame (`0x5bf100`, `dt` = 1/60) does `phase += animSpeed * dt`. The viewer uses real seconds, so the speed matches a 60 Hz clock, and **P** freezes it with the critters.

The vertex formula is `water.shader` in `Override_PC.rkv`. Uniforms come from `0x5c0060`: `waterWaveCoeffs1a = (dirX, dirZ, freq, phase)` and `1b.x = -height`, and the same for wave 2. Height is added in world space:

```
dir = pos.x * dir0.x + pos.z * dir0.z
pos.y += sin(dir * freq0 - phase0) * -height0
pos.y += sin(dir * freq1 - phase1) * -height1
```

Wave 2 reuses wave 1's direction. That is what the PC shader does. The CPU height query at `0x5be5fc` uses wave 2's own direction; the viewer follows the shader.

`.wmh` / `.wml` is one or more groups. A group is `uint32 chunkCount`, then that many chunks:

```
uint32 length; char material[length]; pad to 4
uint32 length; char type[length];     pad to 4
uint32 indexCount
uint32 vertexCount
float  surfaceY, minX, minY, minZ, maxX, maxY, maxZ
vertexCount * 24 bytes: float3 position, float2 uv, uint8 rgba
indexCount * uint32     triangle list
```

The next group follows immediately. The file ends with 4 unused bytes. Vertex alpha is the shore fade, so the mesh is drawn alpha-blended. C3's file has two chunks, `c3_minigame_water` and `c3_water`.

### Critters

Critter fields come from instances the level marks as critters, with a large `scale` as the roam box. The species table and state names follow the TY 1 decomp (`CritterField2`, `BlitterCritter`, and the per-species states): ground animals, hoppers, flyers that land, hoverers, flocks, perchers, swimmers, turtles, surface skimmers, point idles, and sprites. The simulation runs at 30 Hz. Counts, speeds, turn rates, and timers are guesses. The game's values live in `global.model` descriptors that are not decompiled.

A species with a `.bad` script and a matching `.anm` is skinned, up to 64 bones. Fish shoals and similar entries have no script and draw a static mesh. Dragonflies and fireflies use sprite sheets. A plain fly with no sheet draws as a small marker. The walkable floor is the room meshes that are neither transparent nor named `env*`. **P** pauses the fields. Water dragons and synkers stay on their placed point and play idle.

## TY 2 levels (`*.lv3.bni`)

There is no bare `.lv3` in the PC archive. Levels are 145 `*.lv3.bni` chunks (`ra1_chunk_01.lv3.bni`, `ra1_chunk_env.lv3.bni`, and so on, plus `world.lv3.bni`), about 38 zone prefixes. `levels.ini.bni` is the level manifest (`startChunk`, `envChunk`, `ground`). Its name does not contain `.lv3.`, so it is not in the level list.

The `.bni` wrapper is not compression. The original file follows a 0x44-byte header, which `inspectTy2Level` reads:

```
+0x00  char path[32]     e.g. "Data\Levels\ra1_chunk_01.lv3"
+0x20  uint32            constant 100
+0x24  uint32            record count
                          rq2_chunk_env = 13, world = 231, ra1_chunk_01 = 2292
+0x28  uint32            payload size (fileSize - 0x44)
+0x2C  uint32            string-table offset within the payload
                          "HEADER" / "Data File version 3.0" begins here
+0x30  uint32            count * 16 (end of a 16-byte record table)
+0x44  payload           the .lv3 (or .ini) bytes
```

Editor strings in the payload are "TheEditor" or "Sire", "Data File version 3.0". Prop names are embedded as text (`P0156_CollideProp`, `P0022_Gumtree1`, `Prop_0092_DunnyRoll`) and match existing `.mdl` / `.mdg` models. The 16-byte records that would place them are not decoded. Selecting a chunk logs the header, the version string, and a short prop-name sample, and shows "Listed, not drawn".
