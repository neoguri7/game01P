# Game01P art style

Style version: 2

This is the source of truth for generated art. Read it before requesting an
asset. There are **no approved reference images**. Do not automatically use
existing PNGs as references.

## Overall art direction

Game01P is a compact, readable fantasy grid-tactics roguelike. Current content
includes warriors, archers, mages, imps, spells, and dungeon rooms. Favor clear
silhouettes and gameplay readability over ornament.

The repository is still a prototype, not a finished art library. The existing
animation check uses 16x16 pixel frames, the battlefield uses 64-pixel cells,
and the renderer uses nearest-neighbor sampling. These are technical evidence,
not approved visual references. The conservative rules below establish an
initial baseline from that evidence. Do not assume an established gothic,
photorealistic, isometric, or highly ornate direction. A darker material or
subject requested for one asset must still use this compact pixel style.

## Common consistency rules

Consistency takes priority over novelty or extra detail. Preserve this document's
pixel scale, palette roles, outline weight, detail level, lighting and category
rules. Asset requests change subject or motion, never the visual style.

## Pixel-art rules

- Draw at the requested native pixel resolution, not at a larger resolution
  followed by downsampling.
- Use hard, deliberate pixel edges and coherent clusters. No anti-aliasing,
  blur, smooth gradients, or scattered single-pixel noise.
- Keep sprites on transparent backgrounds unless an opaque tile/background is
  explicitly requested. Leave clear padding; do not crop limbs or equipment.
- One asset per image. No mockups, labels, decorative borders, or contact sheets.
  Animation frames are separate images assembled by the tool.

## Sprite resolution / target sizes

- 16x16 is the existing native animation-test size and the CLI default.
- 32x32 is a reasonable requested size for icons or more readable small sprites.
- 64x64 is the current battlefield cell size, not an established native sprite
  resolution. Use it when the request explicitly needs a larger sprite.
- Keep complexity appropriate to the selected size. A 64x64 sprite should not
  become an illustration with many tiny unrelated details.
- Keep every animation frame at exactly the requested size. The game consumes
  equal-size frames in a single horizontal row, without spacing between cells.
- Other sizes require an explicit request and must meet the backend limits.

## Color and palette rules

- Use a small, coherent palette with clearly separated light, middle, and dark
  values. Reuse material colors across frames.
- The prototype's faction cues are party blue (#508CFF), enemy red (#E65050),
  selection yellow (#FFDC3C), and reachable-cell cyan (#00C8FF). Preserve these
  meanings when adding faction/UI accents; do not recolor an entire creature
  just to indicate allegiance.
- Dark neutral cues already used by the grid are #46465A and #14141E. Keep
  foreground silhouettes legible against dark neutral backgrounds.
- These cues are not a locked universal palette. There is no approved image
  palette yet. Use restrained fantasy material colors (metal, cloth, bone,
  stone) and avoid competing neon accents.

## Outline rules

- Use a clean, dark, single-color outline where needed to separate the subject
  from the battlefield. Prefer one native pixel in thickness.
- Internal separation should use clusters and value changes, not outlines
  around every small feature. No thick cartoon stickers or soft edge halos.

## Character proportions

- Use compact, simplified figures that read in one grid cell. Emphasize the
  head, main body shape, and class-defining equipment rather than anatomy.
- Warriors, archers, and mages should be distinguishable by silhouette.
- Frame a full-body unit, centered with a stable foot baseline. Use a readable
  front-facing game-sprite presentation unless another facing is requested.
- Do not invent a fixed head-to-body ratio or a new perspective convention:
  neither is established by the current prototype.
- For a related character/enemy family, keep the same native size, facing,
  apparent body scale, outline thickness, and shading treatment. Different
  classes change silhouette and equipment, not the underlying rendering style.
- For the same character, retain identifying colors, clothing, anatomy, and
  equipment across poses; an animation must not redesign the character.

## Animation rules

Keep the same silhouette, proportions, equipment, palette, facing, canvas, and
foot baseline in every frame. Use crisp pixels and restrained motion; no camera
movement, resizing, morphing, flicker, cropping, or added objects. Idle motion
is subtle and loops smoothly back to the first pose. Action animations should
communicate anticipation, action, and recovery clearly.

## Environment rules

- Design simple fantasy dungeon elements that preserve visibility of units and
  grid feedback. Keep floor detail quieter than characters and effects.
- Request floor/wall tiles as opaque, full-cell images; isolated props use
  transparency. Tile dimensions and seamless edges must be requested explicitly.
- Do not add baked grid lines, characters, interface elements, or an unrelated
  cinematic/isometric scene to an environment asset.
- Within a tile/background family, keep the same tile dimensions, perspective,
  material colors, lighting direction, texture density, and apparent prop scale.
  Variants change wear or layout, not the material's rendering style.
- For requested seamless tiles, preserve edge colors and feature scale across
  opposite edges and neighboring tiles. Preview repeated tiles and adjacent
  variants together; a standalone tile is not proof of seamlessness.

## UI/icon rules

- Keep icons centered, immediately recognizable, and readable at native size.
- Use one dominant silhouette and a few material/value clusters.
- No embedded text, inventory-slot frame, rarity border, or faction marker
  unless explicitly requested. UI currently uses ImGui; generated art does not
  replace its widgets automatically.
- Within an icon/UI family, keep the same canvas size, subject scale, padding,
  outline weight, shading, and palette roles. Do not mix pixel scales or add a
  different decorative treatment for each icon.
- UI state variants retain their silhouette and placement; change only the
  requested state cue. Do not reverse the existing faction/selection colors.

## Detail level

Low detail. Keep only details that clarify the asset's identity, material, or
gameplay role. Avoid ornamental filigree, texture noise, and crowded interiors.

## Lighting/shading rules

- Use simple discrete value steps rather than continuous gradients. Keep light
  consistent across the subject and all frames.
- Use a restrained upper-left light as the initial generation baseline; this
  is a text rule, not a claim about an approved reference.
- No photorealistic reflections, volumetric light, bloom, lens effects, or
  baked cast shadows outside the subject. Emissive effects must be requested.

## Things the generator must avoid

No photorealism, vector smoothing, painterly brushwork, 3D renders, upscaled
pixel blocks, excessive dithering, noisy textures, watermarks, logos, text,
extra subjects, unwanted backgrounds, inconsistent frame dimensions, or
unrequested style changes. Never use the animation-check PNG as a reference.
Asset-specific requests choose the subject and motion, not a different style.

## Consistency review

Consistency is the primary acceptance criterion, not just a prompt preference.
Before approving a candidate:

- Characters/enemies: compare related units side by side at native size for
  body scale, facing, outline, palette treatment, and light direction.
- Tiles/backgrounds: preview a repeated grid and neighboring variants for
  seams, perspective, material colors, texture density, and prop scale.
- Animations: preview all frames and the loop for identity changes, palette
  flicker, outline shimmer, foot drift, equipment changes, and loop discontinuity.
- UI/icons: compare a row of icons and state variants for subject scale, padding,
  outline, shading, and consistent color meanings.

If no approved family exists yet, review against this document first. Existing
generated candidates are not automatically approved references. API success,
matching dimensions, or a repeated seed does not prove visual consistency.
Do not integrate a visually inconsistent candidate just because it generated
successfully. Fix the subject request or deliberately revise this document,
then generate a new variant without overwriting previous results.

## Generator settings

The wrapper reads this small settings block as well as the full document.
These are documented PixelLab style controls; change this document rather than
duplicating style choices in code.

<!-- generation-settings -->
```json
{
  "outline": "single color outline",
  "detail": "low detail"
}
```

## Agent workflow

Read this document, then follow [the asset-generation guide](tools/pixellab/README.md).
Construct a subject-specific request, choose the category and native size,
inspect a dry run, and invoke the CLI. Generated assets are candidates requiring
visual approval, not automatically approved references. If reference support
is added later, use explicitly approved files under `assets/references/` and
record them in the manifest; keep this document authoritative.
