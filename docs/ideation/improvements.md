Here’s a clean spec you can give Claude.

## Procedural tileset specification for a 2D top-down terrain game

We need a terrain tileset with exactly **3 tile families**:

1. **Pure terrain texture tiles**
2. **Coastal transition tiles**
3. **Elevation transition tiles**

The purpose is to let the map generator assemble large-scale coastlines and elevation contours from small tiles in a way that still looks coherent when zoomed out.

---

# 1. Core visual model

## 1.1 Tile size and viewpoint

* Game is **2D top-down**.
* Each tile is a square.
* Coast and elevation tiles are not “blob” shapes or corner patches.
* Instead, each transition tile contains **one continuous transition line** that runs from **one tile edge to another tile edge**.
* That line divides the tile into two regions:

  * coast: land vs water
  * elevation: lower level vs higher level

This is the key rule:

* **Each coastal/elevation tile must contain exactly one edge-to-edge boundary line.**
* No isolated fragments.
* No dead ends.
* No T-junctions.
* No X-crossings.
* No multiple independent lines in one tile.

That guarantees tiles can chain together into larger macro shapes.

---

# 2. Tile family definitions

## 2.1 Pure terrain texture tiles

These are full-tile terrain fills with no boundary line.

Examples:

* flat rock type A
* flat rock type B
* flat rock type C
* dirt
* gravel
* mossy stone

Purpose:

* provide visual variety inside a biome/terrain class
* break repetition
* fill areas that are not on a coast or elevation step

Specification:

* no directional semantics required
* seamless on all 4 edges with same terrain class
* can have multiple variants per terrain type

Recommended:

* at least **3–8 variants per terrain material**

---

## 2.2 Coastal transition tiles

These represent a coastline boundary.

A coastal tile contains:

* one continuous **coast line** from one edge to another
* one side is **water**
* one side is **land**
* coastline style has **3 steepness/width variants**:

  1. **shallow**
  2. **medium**
  3. **steep**

Interpretation in top-down art:

* **shallow coast**: broad, gradual shoreline band, more beach / shallows visible
* **medium coast**: moderate shoreline transition
* **steep coast**: abrupt edge, cliff-like or sharp shoreline

Important:

* these are still top-down tiles, so “steepness” is communicated by width, contrast, foam/beach width, rock edge sharpness, shadow hints, etc.
* the geometry class stays the same; only the visual transition character changes

---

## 2.3 Elevation transition tiles

These represent a single elevation contour/step between two adjacent height bands.

An elevation tile contains:

* one continuous **elevation line** from one edge to another
* one side is **lower terrain**
* one side is **higher terrain**
* the step has **3 variants**:

  1. **shallow incline**
  2. **medium incline**
  3. **steep incline**

Interpretation:

* shallow: broad ramp, subtle shading, wide transition band
* medium: clearer rise
* steep: narrow abrupt escarpment/cliff edge

Important:

* as with coast, each elevation tile has exactly one edge-to-edge boundary
* this lets contour lines chain together cleanly

---

# 3. Allowed boundary shapes

Because the line must go from one edge to another, the valid topology is defined by which two edges it connects.

Tile edges:

* N = top
* E = right
* S = bottom
* W = left

A single line can connect any unordered pair of distinct edges:

1. N–S
2. E–W
3. N–E
4. E–S
5. S–W
6. W–N

These are the **6 fundamental edge-pair classes**.

Interpretation:

* N–S: roughly vertical traversal
* E–W: roughly horizontal traversal
* N–E, E–S, S–W, W–N: corner-turning traversals

These 6 are enough to build arbitrary large coastlines and contour lines.

---

# 4. Shape variants inside each edge-pair class

Within each edge-pair class, the line should not be perfectly straight every time.

Each class should support controlled geometric variants, while preserving:

* same entry edge
* same exit edge
* one connected edge-to-edge line
* no branching

Recommended variants per class:

1. **centered**
2. **biased one way**
3. **biased the other way**
4. **gentle curvature**
5. **stronger curvature**

For example:

* N–S may be:

  * straight centered
  * slightly left
  * slightly right
  * S-curve left
  * S-curve right

* N–E may be:

  * quarter-turn tight
  * quarter-turn wide
  * inward-bowed
  * outward-bowed

This matters a lot: if every N–S tile is the same, macro coastlines will look griddy.

---

# 5. Minimum required tile combinatorics

For each of:

* **coastal**
* **elevation**

you need:

* **6 edge-pair classes**
* × **3 steepness variants**
* × optional **shape variants**

So the minimum strict set is:

* 6 coast topologies × 3 coast variants = **18 coastal tiles**
* 6 elevation topologies × 3 elevation variants = **18 elevation tiles**

That is the bare minimum.

Recommended practical set:

* 6 topologies
* × 3 steepness classes
* × 3 shape variants

So:

* coastal: **54**
* elevation: **54**

This will look much better.

---

# 6. Semantic side assignment

Each transition tile must also define which side of the line is which.

## 6.1 Coast

For coast tiles:

* one side is **land**
* the other is **water**

This must be explicit in metadata, not inferred from appearance.

Example:

* a given N–S coastal tile might define:

  * west side = land
  * east side = water

and another mirrored version:

* west side = water
* east side = land

This means orientation matters.

So for implementation, each edge-pair geometry generally has **two semantic polarities**:

* A-side = terrain1, B-side = terrain2
* or reversed

For coast:

* land/water
* water/land

For elevation:

* low/high
* high/low

If you rely on sprite rotation, these can often be derived automatically.
If you store pre-authored sprites, they must be available or generatable in both orientations.

---

# 7. Recommended canonical representation

Claude should model each transition tile as metadata first, art second.

Example structure:

```cpp
enum class TileFamily {
    PureTerrain,
    Coast,
    Elevation
};

enum class Edge {
    N, E, S, W
};

enum class TransitionGrade {
    Shallow,
    Medium,
    Steep
};

enum class BoundaryShape {
    Straight,
    GentleCurveA,
    GentleCurveB,
    WideCorner,
    TightCorner
};

struct EdgePair {
    Edge a;
    Edge b;
};

enum class SideSemantic {
    TerrainA,
    TerrainB
};

struct TransitionTileSpec {
    TileFamily family;              // Coast or Elevation
    Edge entryEdge;                 // e.g. N
    Edge exitEdge;                  // e.g. E
    TransitionGrade grade;          // shallow/medium/steep
    BoundaryShape shapeVariant;     // geometric style
    bool flippedSemantics;          // which side is land/high etc.
    bool rotatable;                 // can renderer rotate same base asset?
};
```

For pure tiles:

```cpp
struct PureTerrainTileSpec {
    TileFamily family;              // PureTerrain
    int terrainTypeId;              // rock type, dirt, gravel, etc.
    int visualVariant;              // texture variation
};
```

---

# 8. Rotation rules

To reduce authored art count, define canonical base forms and allow rotation where valid.

Example canonical edge-pair bases:

* N–S base for straight-through
* N–E base for corner-turn

Then derive:

* E–W by rotating N–S 90°
* E–S / S–W / W–N by rotating N–E

This can dramatically reduce asset production.

However:

* if lighting/shadows are directional, rotation may break visual consistency
* if top-down lighting is neutral or baked minimally, rotation is safe

Recommended rule:

* design coast/elevation tiles to be **rotation-safe**
* avoid directional highlights that make rotated tiles look wrong

---

# 9. Connectivity invariants

Claude should enforce these invariants in both generator and validation tools.

## 9.1 Transition topology invariants

Every coast/elevation tile must:

* have exactly one connected boundary component
* boundary must touch exactly 2 edges
* touched edges must be distinct
* boundary must not terminate inside the tile
* boundary must not branch
* boundary must not self-intersect

## 9.2 Region invariants

Each transition tile must divide the tile into exactly 2 contiguous regions:

* coast: land region + water region
* elevation: low region + high region

No tiny disconnected islands inside the tile unless that is an explicitly different tile family.

## 9.3 Tiling invariants

When two neighboring transition tiles meet:

* if a boundary exits through a shared edge midpoint/range, the adjacent tile must have a matching boundary entry there
* semantic sides should agree across the shared edge

Example:

* if tile A’s east edge is water on the north side of boundary and land on the south side, tile B must continue that logic consistently

---

# 10. Placement logic in the world generator

## 10.1 Pure terrain placement

Use pure terrain tiles when:

* all neighboring samples belong to same terrain band
* no coast boundary crosses tile
* no elevation boundary crosses tile

## 10.2 Coastal tile placement

Use a coastal tile when:

* the tile is intersected by the land/water boundary
* exactly one coastline segment passes through the tile

The generator should determine:

* which two edges the coastline crosses
* the curvature/shape variant
* the steepness class
* which side is land vs water

## 10.3 Elevation tile placement

Use an elevation tile when:

* the tile is intersected by a single contour / elevation step boundary
* exactly one elevation boundary passes through the tile

The generator should determine:

* which two edges the elevation line crosses
* shape variant
* incline class
* which side is low vs high

---

# 11. Important restriction: no multi-feature tiles in this set

For this tileset spec:

* do **not** mix coast + elevation in one tile
* do **not** allow a tile with two separate coast lines
* do **not** allow a tile with both a corner pocket and a crossing contour
* do **not** allow T-junction shoreline tiles

Why:

* this set is specifically for building macro structure cleanly from simple edge-to-edge primitives
* adding complex junction tiles is a separate, later tileset expansion

---

# 12. Visual design guidelines

## 12.1 Pure terrain

* should tile seamlessly
* should contain noise/detail but no strong directional structure
* multiple rock textures should feel like same world, not unrelated materials

## 12.2 Coast

* boundary should be visually readable at gameplay zoom
* shallow/medium/steep should be distinguishable even from far away
* shallow should imply more transition width
* steep should imply sharper edge, cliff, or minimal beach band

## 12.3 Elevation

* higher side should be readable immediately
* use shading, contour lip, exposed rock, or ledge cues
* shallow incline = wider band and gentler tonal shift
* steep incline = tighter boundary, stronger contrast

---

# 13. Suggested asset naming convention

Use names that encode structure.

Example:

```text
coast_NS_shallow_var0_landW
coast_NS_medium_var1_landE
coast_NE_steep_var2_landInside
elev_NS_shallow_var0_highE
elev_NE_medium_var1_highInside
rock_granite_var0
rock_granite_var1
rock_basalt_var0
```

Or more formal:

```text
{family}_{edgepair}_{grade}_{shape}_{semantic}
```

Examples:

* `coast_NS_shallow_curveA_landWest`
* `coast_NE_steep_wide_landSouth`
* `elev_EW_medium_straight_highNorth`

---

# 14. What Claude should implement

You can tell Claude to implement the system in this order:

## Phase 1: metadata model

* define enums and tile spec structs
* define canonical edge-pair types
* define grade variants
* define semantic polarity

## Phase 2: tile selection logic

Given map samples, determine:

* pure terrain tile
* coast tile
* elevation tile

For transition tiles compute:

* intersected edges
* polarity
* grade
* shape variant

## Phase 3: validation

Build a validator that checks each authored/generated tile spec for:

* exactly 2 edge contacts
* exactly 1 boundary component
* no branching
* legal semantics

## Phase 4: rendering/authoring support

* support rotation of canonical base tiles
* support asset lookup by metadata
* allow future expansion into more complex special tiles

---

# 15. Concise instruction block you can paste directly to Claude

Use this:

We need a procedural top-down terrain tileset with 3 tile families:

1. Pure terrain texture tiles
2. Coastal transition tiles
3. Elevation transition tiles

Rules:

* Pure terrain tiles are full-fill texture tiles with no boundary line.
* Coast and elevation tiles must each contain exactly one continuous boundary line running from one tile edge to another tile edge.
* That line must touch exactly 2 distinct edges.
* No dead ends, no branches, no T-junctions, no X-crossings, no multiple independent lines in one tile.
* The boundary divides the tile into exactly 2 contiguous regions.
* For coast: land vs water.
* For elevation: low vs high.

Required transition topologies:

* N-S
* E-W
* N-E
* E-S
* S-W
* W-N

For both coast and elevation, each topology must have 3 grades:

* shallow
* medium
* steep

Interpretation:

* coast: shallow/medium/steep shoreline transition
* elevation: shallow incline / medium slope / steep step

Each topology should also support multiple shape variants while preserving the same edge pair, e.g. centered, curved A, curved B.

So minimum:

* 6 coast topologies × 3 grades
* 6 elevation topologies × 3 grades

Recommended:

* 6 × 3 × 3 shape variants for each family

Each transition tile must also encode polarity:

* coast: which side is land and which is water
* elevation: which side is high and which is low

Implementation requirements:

* create enums/metadata for tile family, edge pair, grade, shape variant, and polarity
* make canonical transition forms rotation-safe where possible
* build selection logic that, given terrain/water/elevation samples, chooses the right tile
* build validation logic that enforces the topology invariants above

Do not include multi-feature tiles in this set:

* no coast+elevation combo tiles
* no junction tiles
* no multiple lines per tile

The goal is that many tiles can assemble into believable large-scale coastlines and elevation contours when viewed at larger map scales.
