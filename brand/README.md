# Brand source assets

Master art the shipped brand derivatives are generated from. Keep the masters
here so app icons and site logos stay regenerable.

## `logos/` — versioned masters

| File | Role | Generated derivative(s) |
|------|------|-------------------------|
| `logo-switch.png` | Full-bleed Nintendo Switch app-icon master (edge-to-edge, opaque) | `resources/icon/icon.jpg` + `icon.png` (the Switch NRO icon / forwarder tile, 1024²) |
| `logo-switch-web.png` | Squircle Switch variant (rounded, round-buttons) | site logo variants |
| `logo-psvita.png` | Round D-pad puck (for the Vita's circular icon mask) | `app/platform/vita/sce_sys/icon0.png` (128², no alpha) + Vita LiveArea |
| `logo-psvita-web.png` | Squircle D-pad variant | `app/platform/ps4/sce_sys/icon0.png` (512², transparent) + site device art |
| `gmca-logo-src.png` | Master GMCA wordmark/logo | `site/assets/img/gmca-logo.webp` |

### Switch icon regeneration (verified this rebrand)

```sh
magick brand/logos/logo-switch.png -resize 1024x1024 \
  -background black -alpha remove -alpha off -quality 92 resources/icon/icon.jpg
magick brand/logos/logo-switch.png -resize 1024x1024 resources/icon/icon.png
```

Per-console constraints when regenerating the others:
- **PS Vita** `icon0.png`: the file is a **square** the system masks to a **circle**; **no alpha** allowed → composite the round puck on an opaque (black) ground.
- **PS4** `icon0.png`: square/rounded-square display; alpha OK → squircle D-pad on transparent.
- **Switch** HOME tile corner radius is system-fixed and theme-independent → ship a square, the system rounds it.

## `mockups/` — device frames (gitignored)

Raw device frames used to composite the site's device shots. **Not versioned**
(`/brand/mockups/` in `.gitignore`): the site ships its own optimized
derivatives under `site/assets/img/` (e.g. `device-switch.webp`, `*-hero.webp`).
Kept locally only for re-compositing the site.
