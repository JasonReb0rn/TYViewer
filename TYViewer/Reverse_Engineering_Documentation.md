# TY 2 PC MDG Format - Reverse Engineering Documentation

## Overview
This document describes the reverse-engineered PC TY 2 MDG format implementation in TYViewer.
The PC format is significantly different from the PS2 format documented in reference converters.

## Format Differences

### PS2 vs PC MDG Formats

**PS2 Format (from reference converter):**
- Uses VIF packets (GPU microcode markers: `0x00 0x80 0x02 0x6C`)
- Fixed-point UVs and packed normals
- Vertex data interleaved per strip with complex structure
- Requires searching for VIF packet markers

**PC Format (reverse engineered):**
- No VIF packets (different GPU architecture)
- All vertex attributes are floats (no fixed-point or packed formats)
- Interleaved vertex data with fixed 48-byte stride per vertex
- All mesh headers stored first, followed by sequential vertex data block

## PC MDG File Structure

```
Offset 0x00: "MDG3" signature (4 bytes)

File Layout:
1. Mesh headers section (variable size, linked via ObjectLookupTable in MDL)
2. Global vertex data block (all vertices from all meshes stored sequentially)

Mesh Header Structure (referenced by ObjectLookupTable in MDL file):
  +0x00: Base vertex count (uint16)
  +0x02: Unknown (uint16)
  +0x04: Duplicate vertex count (uint16, degenerate vertices between strips)
  +0x06: Strip count (uint16, informational)
  +0x08: Unknown data (likely animation node index: uint16)
  +0x0A: Unknown/padding
  +0x0C: Next mesh pointer (int32, 0 if no next mesh in linked list)
  +0x10: 4 bytes, zero on render meshes. Not the descriptor array.
  +0x14: Strip descriptors array (2 bytes each, stripCount entries)
         The loader still reads these from +0x10, so the low-byte sum is
         short by the last two strips and index generation stays one strip.
         Do not switch to per-strip indexing until the drum 0xB0 stitch
         (duplicate count is 1 higher than (stripCount-1)*2) is understood.
  
Strip Descriptor Format (uint16):
  Low byte (bits 0-7):   Vertex count for this strip (informational only)
  High byte (bits 8-15): Format flags (purpose unclear)
  
  Note: PC meshes are stored as a single triangle strip with degenerate
  vertices between strips. The authoritative vertex count is:
  BaseVertexCount (+0x00) + DuplicateVertexCount (+0x04).

Global Vertex Data Block (starts after all mesh headers):
  All vertices are stored sequentially in 48-byte interleaved format.
  Vertices for each mesh are contiguous, following the order meshes appear
  in the ObjectLookupTable traversal.
  
Per-Vertex Layout (48 bytes). Color and UV belong to the *previous* vertex;
position, weight, and normal belong to *this* vertex. Mesh boundaries do not
reset the shift. After the last record, a 12-byte tail holds the final
vertex's color dword and UV (no position or normal).

    +0-3:   Vertex color of vertex i-1 (D3D 0xAARRGGBB). Dummy on record 0.
            Not applied yet; renderer still uses white.
    +4-11:  UV of vertex i-1 (2 floats). V is flipped: v = 1.0 - rawV.
    +12-23: Position of vertex i (3 floats, XYZ - confirmed)
    +24-27: Weight/Modifier of vertex i (1 float, typically 1.0 - purpose unclear)
    +28-35: Unknown field of vertex i (2 floats, often constant per mesh like 27.0, 27.0)
    +36-47: Normal of vertex i (3 floats, XYZ normalized - confirmed)
    
  Vertex block start (do not scan for it):
    start = fileSize - 12 - (sum of every mesh's base+duplicate) * 48
    The sum includes collision meshes. The block must begin at or after the
    last mesh's fixed header (+0x10). A forward scan from header+stripBytes
    lands inside vertex 0 and locks onto vertex 1.
```

## Notes

1. **Format Detection**: Checks for PS2 VIF packet markers (`0x00 0x80 0x02 0x6C`) to determine format
   - If markers found → Use PS2 parser (`parseStrip`)
   - If no markers → Use PC parser (`parseMDGPC`)

2. **PC Parser (`parseMDGPC`)**:
   - Places the vertex block at `fileSize - 12 - totalVertices * 48`
   - Uses the ObjectLookupTable in the MDL3 file to walk meshes
   - Uses base+duplicate counts from the mesh header to size each mesh
   - Vertex data is interleaved; no separate UV buffer found
   - Collision materials (`CM_`) are skipped for rendering for now (future render layer)

3. **MDL3 Metadata Integration**:
   - MDL3 file provides:
     * ComponentCount and TextureCount for ObjectLookupTable dimensions
     * ObjectLookupTable offset for finding mesh references
     * Texture names for proper material assignment
   - MDG parser uses metadata to:
     * Traverse all meshes via ObjectLookupTable
     * Associate each mesh with correct texture and component
     * Handle linked lists of meshes (via next mesh pointers)

## Usage

Models are loaded automatically when a .mdl file has a corresponding .mdg file:

1. Load MDL file → Detects MDL3 format → Extracts metadata
2. Detect .mdg file exists → Load MDG with MDL3 metadata
3. Parse PC format → Create meshes with texture associations
4. Render model

## Testing

Test with: `P0486_B1FlowerPot.mdl` / `P0486_B1FlowerPot.mdg`

Expected output:
```
Loading model from config: P0486_B1FlowerPot.mdl
loadTY2MDL3: Successfully parsed MDL3 format
Detected TY 2 format, loading MDG file: P0486_B1FlowerPot.mdg
MDG: No PS2 markers found, assuming PC format
MDG PC: Parsing mesh at offset X with Y strips
MDG PC: Parsed Z vertices
Successfully created model: P0486_B1FlowerPot.mdl with N meshes
```

## Reverse Engineering Findings

### CONFIRMED
1. **Vertex stride is 48 bytes**
   - Verified across multiple models.

2. **Position data at offset +12 is correct**
   - Offset: +12 to +23 (3 floats, XYZ).
   - Geometry renders correctly; bounds line up.

3. **Normals at offset +36 are correct**
   - Offset: +36 to +47 (3 floats, XYZ).
   - Values are normalized; lighting looks reasonable.

4. **File layout is consistent**
   - Mesh headers first, then one contiguous vertex block.
   - Vertices are interleaved (array-of-structs), not split by attribute.

5. **Mesh vertex counts come from the header**
   - Base count (+0x00) + duplicate count (+0x04) = total vertices.
   - Duplicates act as strip connectors.

6. **Vertex block start is `fileSize - 12 - totalVertices * 48`**
   - `totalVertices` is the sum of base+duplicate over every mesh, including collision.
   - Scanning forward from the last header skips vertex 0. Each mesh then ends on the next mesh's first vertex (one rogue triangle) and loses its opening triangle. On the street lamp that hole is the face through viewer verts (0, 1, 9), because vert 9 repeats the dropped vertex.

7. **UVs are float32, stored one record ahead of their position**
   - The UV at record `i` (+4/+8) belongs to vertex `i-1`.
   - The shift is global across the vertex block. It crosses mesh boundaries, including into collision meshes that are not drawn.
   - The last vertex in the file reads its UV from the 12-byte tail (`+4/+8` after a color dword).
   - Do not clamp the shift to the current mesh, and do not gate it on duplicate-position heuristics. Meshes with no strip-restart duplicates still need the shift; clamping drops the last vertex's UV (the speed-sign face warp).
   - V still needs flipping (`v = 1.0 - rawV`).

### OBSERVED / INFERRED (Material naming conventions)
The TY2 PC pipeline separates **mesh geometry** (MDG) from **material slots** (MDL3 texture-name list). The ObjectLookupTable already pairs each mesh with the right slot. A slot name is not always the DDS filename, and a suffix is not always “the same atlas.”

Texture resolution, in order, only uses a candidate when that file exists:

1. **Exact DDS**: `A120_FireBunyip` → `a120_firebunyip.dds`, `a039_Orchid` → `a039_orchid.dds`, `A049_Elle` → `a049_elle.dds`.
2. **`Z` sort prefix**: one token of the form `_Z` + an uppercase word. `A120_ZBlueSpec` → `a120_bluespec.dds` (64×64, its own image). This is the only such material name in the PC archive. The mesh UVs cover the full 0–1 square, so it is not a window of the bunyip atlas. The DDS is DXT1 (no alpha); blend state for that glass is still unknown.
3. **Trailing digits, only if that file exists**: `A049_Elle01` → `A049_Elle`. This is not a tint. Digits are not always the same image: `A001_Ty01.dds` and `A001_Ty02.dds` are separate files, so the stripped name is used only when the exact name is missing and the stripped file is present.
4. **`Glass` suffix, only if that file exists**: `A049_ElleGlass` → `A049_Elle`. Elle glass UVs are a small window of that atlas.
5. **Unique shared DDS**: strip the leading model id, and a leading `A` when the next letter is lowercase. `A120_AworkerKoala` → `a110_worker_koala.dds` (the worker koala diffuse; the bunyip mesh is that character, UVs cover 0–1). Accepted only when exactly one DDS contains the normalized token, and the DDS does not add a `spec` / `env` / `skymap` suffix the material name lacks.
6. **Atlas window**: if both UV spans are under 0.5, bind the model diffuse. Elle eyes sit in about u 0.25–0.29, v 0.72–0.75 of `a049_elle.dds`, and there is no `A049_ElleEye.dds`.
7. **Unresolved**: do not substitute the model diffuse or the previous texture. Orchid eyes and shine cards span almost the whole square, and `Data_PC.rkv` has no `a039_OrchidEye.dds` or `a039_Orchidshine.dds` (only `a039_orchid.dds`). Those slots stay on the missing texture.

- **`Spec`**: sometimes its own image (`a120_bluespec.dds`), sometimes only a name with no file. Not automatically the body atlas.
- **`Overlay`**: a black/white overlay on separate geometry, likely a masked lightmap pass. TYViewer does not composite overlays yet.

**Important**: Suffix meaning is not one rule. A small UV window with no DDS shares the model atlas (Elle eye/glass). A full-range unwrap needs its own file (blue spec, koala) or stays unresolved (Orchid eye/shine). Render-state fields for glass/spec blending are still not decoded.

### UNKNOWN/UNCLEAR:
1. **Vertex Colors**: Stored with the same +1 shift as UVs
   - The `+0` dword of record `i` is the color of vertex `i-1` (D3D `0xAARRGGBB`).
   - The file tail's first 4 bytes are the last vertex's color (often `0xFFFFFFFF`).
   - Parser still writes white so the UV fix can be checked on its own.
   - Duplicate positions match this shifted dword on static props. Character meshes are often a constant color, so both shifts match.

2. **Weight/Modifier Field (+24-27)**: Always 1.0 in static meshes
   - 4 bytes (1 float)
   - Purpose unclear - skinning weight? LOD factor? scale?
   - Currently stored in skin[0]

3. **BBox/Unknown Field (+28-35)**: Two floats, often constant per mesh (27.0, 27.0)
   - 8 bytes (2 floats)
   - May be bounding box max, scale factors, or UV-related
   - Values are mesh-specific but constant across vertices in same mesh

4. **Padding (+44-47)**: 4 bytes, often 0xFFFFFFFF
   - Used as sentinel/padding
   - Sometimes contains other values
   - Purpose unclear

5. **Strip Descriptor Flags (high byte)**: 0xA0, 0x20, 0xB0, 0x60, etc.
   - May indicate rendering mode, vertex features, or material properties
   - Not required for vertex counts in PC format

6. **Mesh Header Unknown Fields**:
   - +0x02: Unknown (uint16)
   - +0x08-0x0B: Possibly animation node index + padding
   - May contain material IDs or LOD flags

## Next Steps

### HIGH PRIORITY
1. **Apply the shifted vertex color**
   - Decode the `+0` dword of the next record (and the tail) as D3D `0xAARRGGBB` once the UV fix is confirmed in the viewer.

### MEDIUM PRIORITY
2. **Understand the +28..+35 floats**
   - Often constant per mesh; may be scale or bounds data.

### LOW PRIORITY
3. **Clarify the +24 weight/modifier field**
   - Always 1.0 in static meshes; likely skinning-related.
4. **Analyze strip descriptor flags (high byte)**
   - 0xA0, 0x20, 0xB0, 0x60 appear; purpose unknown.
5. **Cache vertex data offset**
   - Avoid repeated searches during load.

## Testing Results

### Test Model: `P0788_SpeedSign.mdl/mdg` (TY 2 PC)
- **Status**: Sign-face UV warp fixed by reading UV from the next record, across meshes
- Two 5-vertex sign faces. The last triangle of each (globals 166–168 and 171–173) used to collapse because the last vertex kept the previous vertex's UV
- Correct sign corners (V flipped): TL `(0.00427, 0.99526)`, TR `(0.43097, 0.99526)`, BL `(0.00427, 0.52098)`, BR `(0.43097, 0.52098)`
- BR's UV lives in the next mesh's first record (the other sign, then the skipped `CM_METAL` mesh)

### Test Model: `P0623_MovieLight.mdl/mdg` (TY 2 PC)
- **Status**: UVs align with a global +1 shift, including meshes that have no duplicate positions
- Strip descriptors do not sum to base+dup
- Adjacent duplicate positions are connector vertices
- Vertex data starts at offset 244

## Methodology

### Reverse Engineering Methodology:
1. **Binary analysis**: Hex dumps and pattern recognition in test files
2. **Stride testing**: Tested multiple stride values (32, 36, 40, 44, 48 bytes)
3. **Layout discovery**: Examined different attribute orderings and offsets
4. **Validation**: Verified positions, UVs, and normals produce reasonable values
5. **Cross-reference**: Used MDL3 metadata to validate mesh counts and structure

### References:
- PS2 MDL3-MDL2 converter (C# reference implementation) - for understanding PS2 format structure
- TY 2 PC game files from `Data_PC.rkv` archive
- MDL3 header documentation (from reference converter)

### Key Discoveries:
The PC format is **completely different** from PS2 format:
- PS2 uses VIF packets and complex per-strip parsing
- PC uses simple interleaved 48-byte vertices in a single data block
- PS2 has packed/fixed-point data, PC uses floats for positions/normals
- PC format is simpler to parse once the layout is understood!

### Current Status:
- Geometry renders correctly with full face coverage
- 48-byte stride confirmed
- Header vertex counts (base + duplicate) are correct for mesh sizing
- Vertex block start is `fileSize - 12 - totalVertices * 48` (no pattern scan)
- Collision meshes use `CM_` textures and are skipped for rendering
- **UVs**: float32 UVs are one record ahead of their position (plus V flip), including across meshes and the 12-byte tail

## Level files

The viewer can list both games' levels. A TY1 level draws its room meshes and the placed objects that resolve to a model. A TY2 chunk is logged and not drawn.

### TY 1: `.lv2` (plaintext MapEd)

`Data_PC.rkv` contains 33 `.lv2` files (`a1.lv2` … `e4.lv2`, `z1.lv2`, `z2.lv2`, plus `*ex.lv2`). A main level is a Krome MapEd script, "Data File version 2". The `name setup` block names the terrain models:

- `ground = room_a1_01.mdl`
- `envcube = env_a1.mdl`
- `overlay_* = room_a1_02.mdl, ...` (extra comma-separated tokens are flags or material names, not models)

Those `.mdl` files are in the same archive, already in world space, and are what the viewer loads as the level mesh. Later `name TY` / `name STATICBRIDGEFLAT1` / `name OPAL` blocks are placed instances. A blank line separates instances of the same type. Each instance reads the first `pos`, `rot`, and `scale`. `rot` is pitch, yaw, roll in radians (a missing comma is still three numbers). Missing `rot` is zero and missing `scale` is 1. A `scale` whose largest component is above 64 is the flock roam box (`count` is how many animals the game scatters there), so that mesh stays at 1. `bVisible = 0` starts that instance hidden. The type name is not a filename (`staticbridgeflat1.mdl` does not exist). The game strips a leading `STATIC` and looks the rest up, case-insensitively, in `global.model` catalog sections (`StaticProps`, `StaticFXProps`, and the other prop lists). `BridgeFlat1 prop_0111_bridge_flat_01` is the mesh for `STATICBRIDGEFLAT1`. Names that are not in those lists use the descriptor the game registers in code (`Opal` is `Prop_0270_FireOpal`, `Crate` is `Prop_0001_WoodenCrate_01`) or, when that is unique, the `.mdl` whose name ends in the type. Logic objects with no mesh (`TY`, `DIALOG`, `TRIGGERBOX`, `TRIGGERSPHERE`, `SOUNDPROP`, `CAMERA`, `SCRIPT`, `PATH`, `WATERVOLUME`) stay in the OBJECTS list and are not drawn. `RESTART` is the checkpoint. It places `Prop_0018_Thunderbox` (the outhouse) and `Prop_0092_DunnyRoll` (the ground piece) at the same `pos` and `rot`. Selecting one draws the 500-unit approach sphere the game uses to pop the dunny. That radius is not stored in the level. That list is one row per instance, with search and show/hide, and sits on the left. LEVEL PARTS stays the room meshes. Several instances share one cached model. The instance matrix is applied at draw time, so hiding a room part does not move a prop, and hiding one prop does not hide the others that use its model. `*ex.lv2` files are often instance lists with no setup block. They still place whatever objects resolve. `.scn` and `.cam` are not levels.

Segment UVs are 4.12 fixed point (`int16 / 4096`). U is the raw value. V is `1 - raw/4096`, including values outside 0–1. Terrain tiles past 1 (B3 `Room_B3_04` cliff strip stores V `4, 3, 2, 1, 0, -1, …` along the wall). `abs(raw/4096 - 1)` matches the flip only while stored V is at most 1. Above that it negates V, so a strip that crosses 1 runs the texture backward on one side. Props stay inside 0–1, so the mirror does not show on individual models. Do not put the `abs` back.

A subobject material pointer of `0xFFFFFFFF` means there is no material string. `Env_B1.mdl` (`Dome`, `Moon`) and `env_b2.mdl` (`Plane01`) use that sentinel. The mesh materials after it (`Ty_B1_Env_*`, `TY_B2_Env_*`) are real. Reading the sentinel as a string walks off the end of the file.

White level collision is not only `Collide_*`. The same shells are also named `Collision`, `Collsion`, `Colide`, or `collde`. Parts whose names contain `invis` are collision even when the material has a DDS: B2 snow on `TY_B2_001` (`invis_snowmountain`, `invis_sidewall1`, stairs, cave, and rock steps) and blue ice on `TY_B2_005` / `TY_C2_018` (`invis_iceplane02`, `invis_ice_isle01`, `invis_riverbed01`). Those shells sit on the real terrain. An `invis` part stays collision even when its material has `effect = grass` and is not `invisible 1`. `ty_b2_029` and `ty_b2_031` are grass emitters and are also the drawn dirt on sector parts (`grassbottom02`, `icehill`, `cave02`, `clump_trees*`). The flag spawns grass on those surfaces. It does not keep `invis_grass*`, `invis_walls03`, `invis_icewall02`, `invis_snowgrass01`, or `invis_grass_sec08` visible. Z1 `invis_edgesection*` is the same kind of shell. `snow_path03` on `Room_b2_01` (`TY_B2_001` / `TY_B2_005`) is a collision ribbon; the drawn path is the sector meshes in `Room_b2_02`. Untextured parts whose names start with `C_` / `C ` are collision too. A3 `C_Geom_Quicksand` is the quicksand gameplay volume, the only one in the game. It uses the surrounding ground textures (`TY_A3_004`, `TY_A3_003`, `TY_A3_006`, `TY_A3_014`, `TY_A3_015`, `TY_A3_031`, `TY_A1_003`), so it is collision even though those materials have a DDS. The drawn pit is the sector part `003 000 006 Geom_Quicksand_02` and stays visible. B1 `Object01` and `Object03` on `TY_A1_024` are collision shells left with default Max names. They draw white. `Object02` is already hidden because it uses `T0103_01_c`. `TY_A1_024` also textures the real cavern in `Room_b1_02`, and other `Object01` / `Object03` parts are scenery, so only that part-and-material pair is collision. A `global.mad` material with `invisible 1` is collision: C3 `TY_C3_realgrasstrim` (the white island skirts) and B3 `ty_b3_001_FX15` / `ty_b3_002_FX15`. Meshes on `T0103_01` or `T0103_01_*` (any suffix), a material named `Collision`, or `Material #*` are collision. Suffixed slots have no DDS and draw the white fallback. The unsuffixed name binds `t0103_01.dds`, a red/yellow collision bitmap (a2 `Collied_Bridge`, z1 perimeter `Line04`, and the same shells on other levels).

Other white patches on B2, C1, E2, Z1, and Z2 are level art, not a missed collision flag. The material is a variant of a DDS that is in the archive: `TY_C2_010_a` → `ty_c2_010.dds`, `TY_C1_008a` → `ty_c1_008.dds`, and the same for `_nograss`, `_no_grass`, `_reed`, `_overlay`, and a trailing `_` + letter. The loader uses that base texture. Names ending in `_grass` or `_lessgrass` are drawn ground (`TY_a2_022_grass` on A2 `Ground01`, `TY_Z2_003_grass`, `TY_A2_005_lessgrass`). `global.mad` aliases them to the real texture, and `effect = grass` only spawns grass on that surface. `invisible 1` is what hides a guide: `TY_B2_001_grass`, and the first `Ty_C2_033_grass` block (a later block with the same name drops the flag; the loader keeps the first, so those C2 Grass Strip meshes stay collision). Sector parts whose names end in ` trees` (`000 000 000 trees` and `001 000 000 trees` on B2, and A2 `Outer Trees`) are level geometry, not grass strips. `clump_trees*` stays visible. Hiding ordinary part names (`icehill`, `cave02`, `mountainside2`) removes the terrain. `TY_B2_001`, `TY_B2_005`, and `TY_C2_010_a` are not collision by material name. `T0103_01_*` is not remapped, so collision stays white when the toggle is on.

B2 `tree_walls` is the tall vertical collision shell around the level. It uses `TY_B2_001` (opaque snow) and writes depth in front of the trees, so it is hidden with the other collision meshes. The tree wall that stays visible is the tall cards on `clump_trees*` using `Prop_B2_Page_treepage`, `ty_b2_020`, and `ty_b2_021`. Those three materials are `masked` with an `aref` in `global.mad`. Pixels below that alpha are discarded, so the empty part of the card does not hide the trees and ground behind it. E1 `A_TreeWall` is those cards, not the B2 shell, and stays visible.

### TY 2: `*.lv3.bni` (binary, Data File version 3.0)

There is no bare `.lv3` in the PC archive. Levels are 145 `*.lv3.bni` chunks (`ra1_chunk_01.lv3.bni`, `ra1_chunk_env.lv3.bni`, …, plus `world.lv3.bni`), about 38 zone prefixes. `levels.ini.bni` is the level manifest (`startChunk`, `envChunk`, `ground`), not a level, and is not listed.

The `.bni` wrapper is not compression. The original file follows a 0x44-byte header:

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

Editor strings in the payload are "TheEditor" or "Sire", "Data File version 3.0". Prop names are embedded as text (`P0156_CollideProp`, `P0022_Gumtree1`, `Prop_0092_DunnyRoll`) and match existing `.mdl` / `.mdg` models, but the 16-byte records that would place them are not decoded. Selecting a chunk logs the header, the version string, and a short prop-name sample. It does not render.
