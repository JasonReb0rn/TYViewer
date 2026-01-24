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
  +0x10: Strip descriptors array (2 bytes each, stripCount entries)
  
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
  
Per-Vertex Layout (48 bytes):
    +0-3:   Unknown/flag (often 0xFFFFFFFF)
    +4-11:  UV coordinates (2 floats) - this is the real UV location
    +12-23: Position (3 floats, XYZ - confirmed)
    +24-27: Weight/Modifier (1 float, typically 1.0 - purpose unclear)
    +28-35: Unknown field (2 floats, often constant per mesh like 27.0, 27.0)
    +36-47: Normal (3 floats, XYZ normalized - confirmed)
    
  Note: The vertex data offset must be found by searching for valid vertex
  patterns, as it doesn't have a fixed location after the mesh headers.
```

## Notes

1. **Format Detection**: Checks for PS2 VIF packet markers (`0x00 0x80 0x02 0x6C`) to determine format
   - If markers found → Use PS2 parser (`parseStrip`)
   - If no markers → Use PC parser (`parseMDGPC`)

2. **PC Parser (`parseMDGPC`)**:
   - Finds the vertex block by validating Position (+12) and Normal (+36)
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

6. **Vertex block offset search works**
   - Validate Position (+12) and Normal (+36).
   - Skip header regions that can contain false positives.

7. **UVs are float32 at +4/+8 with a +1 vertex shift**
   - Raw UVs are stored one vertex ahead of their positions.
   - Applying a +1 shift fixes the “every other face” UV warping.
   - V still needs flipping (`v = 1.0 - rawV`).

### OBSERVED / INFERRED (Material naming conventions)
The TY2 PC pipeline appears to separate **mesh geometry** (MDG) from **material/texture slots** (MDL3 metadata). The strings that show up as “materials” in TYViewer are currently sourced from the MDL3 texture-name list and should be treated as **material slot identifiers**, not strictly as unique diffuse texture filenames.

In practice, multiple “material names” often reuse the *same* underlying texture atlas, and the suffix encodes **render-state or variation**, not a different image.

- **Trailing digits**: e.g. `A049_Elle01`
  - **Status**: **Not “tint”** (prior assumption was incorrect).
  - **Current best guess**: some kind of **variant/pass identifier** (render-state, layering, or slot reuse), but **unknown**.
- **`Glass` suffix**: e.g. `A049_ElleGlass`
  - **Likely meaning**: semi-transparent rendering (alpha blend / alpha test / depth-write differences).
  - **Evidence**: still uses the same atlas, but should render with transparency semantics.
- **`Spec` suffix**: e.g. `...Spec`
  - **Likely meaning**: specular/shinier material state (lighting/specular parameters), not necessarily a different diffuse map.
- **`Overlay` suffix**: e.g. `...Overlay`
  - **Observed behavior**: a black/white overlay texture rendered on top of the model on separate geometry.
  - **Likely meaning**: clipped “lightmap” / baked lighting overlay pass (alpha-tested / masked overlay rather than a full transparency blend).
  - **Note**: TYViewer currently does not implement special overlay compositing; this should be handled as a separate render pass later.

**Important**: These are naming conventions observed in real model sets, and are not yet backed by a fully decoded “material definition” structure. Long term, we should map these suffixes to real render-state fields (likely from MDL3/MDG header bits), rather than relying on string heuristics.

### UNKNOWN/UNCLEAR:
1. **Vertex Colors**: Not found in the 48-byte vertex data
   - Currently defaults to white (1,1,1,1)
   - May be stored separately, derived from textures, or not used
   - Models render fine without them

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
1. **Validate +1 UV shift across more TY 2 PC models**
   - Confirm it holds when connector duplicates are sparse or absent.

### MEDIUM PRIORITY
2. **Understand the +28..+35 floats**
   - Often constant per mesh; may be scale or bounds data.
3. **Investigate vertex colors**
   - Not found in the 48-byte vertex stream yet.

### LOW PRIORITY
4. **Clarify the +24 weight/modifier field**
   - Always 1.0 in static meshes; likely skinning-related.
5. **Analyze strip descriptor flags (high byte)**
   - 0xA0, 0x20, 0xB0, 0x60 appear; purpose unknown.
6. **Cache vertex data offset**
   - Avoid repeated searches during load.

## Testing Results

### Test Model: `P0623_MovieLight.mdl/mdg` (TY 2 PC)
- **Status**: UVs fully correct with +1 UV shift
- Mesh has 610 vertices; strip descriptors do not sum to base+dup
- Adjacent duplicate positions are connector vertices; UVs align after +1 shift
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
- Search algorithm works reliably using position + normal validation
- Collision meshes use `CM_` textures and are skipped for rendering
- **UVs resolved**: float32 UVs require +1 vertex shift (plus V flip)
