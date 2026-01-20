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

## Reverse Engineering Findings - CONFIRMED WORKING

### FULLY CONFIRMED (95%+ accuracy):
1. **Vertex stride is EXACTLY 48 bytes**
   - Tested with multiple models
   - All vertices parse correctly with this stride
   
2. **Position data at offset +12 is 100% CORRECT**
   - Offset: +12 to +23 (12 bytes, 3 floats: XYZ)
   - Models render with correct geometry
   - Bounding boxes match perfectly
   - This is the most critical finding!

3. **Bounding boxes are CORRECT**
   - Parsed from MDL3 component descriptions
   - Match the actual rendered geometry
   
4. **File structure is CONFIRMED**
   - All mesh headers come first
   - Followed by one continuous vertex data block
   - Vertices are interleaved (NOT structure-of-arrays)
   - Position/normal are floats; UVs are floats too (with V flipped)
   
5. **Vertex data offset search algorithm WORKS**
   - Validate Position (+12) and Normal (+36)
   - Mesh headers can contain valid positions but invalid normals
   - Offset is after the last mesh header, aligned to 4 bytes
   
6. **Normal data appears correct**
7. **Mesh vertex counts are stored in the header**
   - Base count at +0x00, duplicate count at +0x04
   - Total vertices = base + duplicate
   - Duplicate vertices are degenerate strip connectors
   - Offset: +36 to +47 (12 bytes, 3 floats: XYZ)
   - Values are normalized (-1 to +1 range)
   - Lighting appears reasonable on models

### PARTIALLY WORKING (Needs Investigation):

1. **UVs are at +4/+8 as floats, but still not 100% correct**
   - Breakthrough: UVs now land in the correct regions ~50% of the time
   - Formula in code right now:
     * `u = float at +4`
     * `v = 1.0 - float at +8`
   - Stretching/warping still happens on some faces (often “every other face”)
   - This looks like strip/degenerate handling rather than UV storage

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

## Next Steps (Priority Order)

### CRITICAL - Fix remaining UV stretching:
1. **Degenerate/strip handling**
   - UVs are mostly right now, but some faces stretch badly
   - Feels like strip boundaries or degenerate handling is wrong
   - Need to verify how strip descriptors are meant to be used (low-byte counts don’t sum to total vertices in some files)
   - Consider deriving strip boundaries from degenerate runs instead of descriptor counts

### MEDIUM Priority:
3. **Understand BBox field (+24-31)**: May be related to UV coordinate space
4. **Test with more models**: Verify 48-byte format is universal
5. **Investigate vertex colors**: Find where they're stored (if anywhere)

### LOW Priority:
6. **Optimize vertex search**: Cache offset for faster loading
7. **Clarify weight field**: Test with animated/skinned meshes
8. **Understand strip flags**: Analyze flag patterns across models

## Testing Results

### Test Model: `P0486_B1FlowerPot.mdl/mdg` (TY 2 PC)
- **Status**: Geometry WORKING, UVs ~50% correct
- File sizes: MDL 1,028 bytes, MDG 14,048 bytes
- Components: 8, Textures: 4, Meshes: 9, Strips: 32
- Total vertices: 292 across all meshes (base + duplicate)
- Vertex data starts at offset 264 (0x108)
- **Result**: Lots of faces now land in the right texture regions, but some faces are stretched/warped

### What Works:
- Mesh geometry is correct (positions are accurate)
- Normals appear correct (lighting looks reasonable)
- Model structure and topology is correct
- Missing faces resolved by treating meshes as a single strip with degenerate connectors

### What Doesn't Work:
- Some faces are still stretched or rotated unexpectedly
- The “every other face” warp pattern points at strip/degenerate handling

### Test Model: `P0623_MovieLight.mdl/mdg` (TY 2 PC)
- **Status**: UVs almost correct, still stretched or warped
- Mesh has 610 vertices, strip descriptors present but sum doesn’t match base+dup
- Same symptoms: correct regions, wrong stretching on alternating faces

### Performance:
- Vertex search algorithm finds correct offset (264)
- All 292 vertices parse successfully
- No crashes or errors during parsing

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
- **BUT**: UVs might still use packed format (needs investigation)
- PC format is simpler to parse once the layout is understood!

### Current Status:
- **MAJOR BREAKTHROUGH**: Geometry renders correctly with full face coverage
- 48-byte stride confirmed
- Header vertex counts (base + duplicate) are correct for mesh sizing
- Search algorithm works reliably using position + normal validation
- Collision meshes use `CM_` textures and should be skipped for rendering
- **NEXT CHALLENGE**: UV coordinates need investigation
