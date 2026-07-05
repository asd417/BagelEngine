# LEGO connectors — taxonomy, detection, and the baked format

How BagelEngine recovers LEGO connection points from LDraw parts, what each maps to, and
how the connection graph turns them into physics joints.

## LDraw has no connector metadata

Standard LDraw `.dat` parts are pure geometry: a tree of type-1 sub-file references plus
triangles. There is **no semantic "this is a connector" tag**. Everything below is *inferred*
by recognizing the standard named **primitives** LDraw authors build parts from (e.g. a raised
stud is a reference to `stud.dat`). "Detectable" therefore means "there is a canonical primitive
whose title we can match" — and where there isn't, the connector must be **hand-authored**.

## Connector table

| Connector | LDraw primitive(s) | Title match | `ConnectorType` / `ConnFamily` | Detected? |
|---|---|---|---|---|
| Stud (raised, on top) | `stud.dat`, `stud2.dat` | basename `stud*`, not "Tube"/"Group" | `Male` / `None` | ✅ |
| Anti-stud tube (underside) | `stud3.dat`, `stud4.dat` | "Stud **Tube** …" | `Female` / `None` | ✅ |
| Axle hole (cross) | `axlehole.dat`, `axlehol8.dat` | "Axle Hole" | `Axle` / `None` | ✅ |
| Pin / connector hole (round) | `connhole.dat`, `beamhole.dat` | "Beam Hole" / "Connector Hole" | `Pin` / `None` | ✅ |
| Small ball (towball) | `joint8ball.dat` | basename `joint8ball*` | `Ball` / `Joint8` | ✅ |
| Small socket | `joint8socket*.dat` | basename `joint8socket*` | `Socket` / `Joint8` | ✅ |
| Click-lock hinge finger | `clh*.dat` | basename `clh*`; "Single"/"Dual" Finger | `Male`/`Female` / `ClickHinge` | ✅ |
| **Male pin** | `confric*.dat`, `connect*.dat` ("Technic … Pin") | — | `Male` / `None` (intended) | ❌ *not yet* |
| **Male axle** | assembled from `axleend*` (no canonical marker) | — | `Male` / `None` | ❌ |
| **Big ball** (Technic 32474 / Bionicle) | generic `4-8sphe.dat` + subfiles | — | `Ball` / `Constraction` | ❌ |
| **Raw rotation joint** (44225/44224) | raw cylinders/discs | — | — | ❌ |

The ❌ rows are modeled from generic geometry with no dedicated primitive, so the offline baker
cannot place them. They must be **hand-authored** (see below), or added via a per-part table.
Do **not** try to detect big balls from `4-8sphe.dat` — it's just "a sphere" and false-positives
on every dome/rounded stud.

### Click-hinge detents ("dibs")

`clh*` titles encode the click-stop count, e.g. `clh10` = "…Dual Finger **7-Position**". The baker
parses `N-Position` into `ConnectionPoint::detents`. The single-finger half carries no count and
inherits it from its dual-finger mate at connection time.

## Joint resolution (connection graph)

The graph (`connections.hpp`) records edges between bricks as `Connection{other, connector,
connectorOther, axisGroup, type (MateType), family}`. `resolveJoint(a, b)` classifies the *joint* (all shared connections
between a pair) into a `JointKind`, in this order:

1. **Ball** — any ball/socket connection → 3-DOF; motion left entirely to Jolt (cone/swing-twist).
2. **ClickHinge** — any click-hinge connection → 1-DOF hinge that holds pose at `detents` stops.
3. **Rigid** — any non-rotatable connection present (`AXLE_AXLESLOT`: a cross axle in a cross
   hole keys the pair), OR ≥2 shared connections in **different `axisGroup`s** (offset-parallel,
   e.g. a brick's studs).
4. **Hinge** — a single connection, or ≥2 all in **one `axisGroup`** (collinear holes: a long
   axle/pin spinning through inline round holes stays a hinge).

`axisGroup` groups a part's connectors that share one rotation-axis *line* (parallel axes on a
common line). It's **derived from geometry on load** (`assignAxisGroups`), never stored — so
hand-authored parts get correct collinearity for free.

## Baked file format

One plain-text file per part: `lego/baked/connections/<part>.conn` (produced by `partsParser`;
read lazily + cached by `ldraw::BakedConnectors`). One connector per line, `#` comments:

```
# source: baked
# BagelEngine connectors -- part 3001
# fields: type family detents  px py pz  ax ay az   (axis = +Y/stud direction)
male none 0  -30 -24 -10  0 -1 0
female none 0  0 -4 0  0 -1 0
```

- `type` = `male|female|pin|axle|ball|socket`, `family` = `none|joint8|constraction|clickhinge|duplo`.
- `pos` is part-local LDU (studs-down, pre-scale); `ax ay az` is the unit connector axis. The full
  3×3 basis is **not** stored — the reader rebuilds one from the axis (roll is unconstrained and
  the only consumer, the gizmo, needs just the direction).

### Hand-authoring / manual override

Because many connectors (the ❌ rows) can't be auto-detected, files are meant to be editable:

- Edit a `<part>.conn` by hand to add/fix connectors — just add lines in the format above.
- Change line 1 to `# source: manual` and the baker will **skip that file** on future runs, so
  your edits survive a re-bake. (`--skip-existing-connectors` skips *all* existing files.)

## Baker flags (connectors)

`partsParser` writes connector files by default. Relevant flags:
`--no-connectors`, `--skip-existing-connectors`, `--connections-dir <dir>`.
For a fast connector-only re-bake, turn the slow passes off with `--no-thumbnails`
`--no-collision`, or resume an interrupted run with `--skip-done-thumbnail` /
`--skip-done-collision` (skip parts already written).
