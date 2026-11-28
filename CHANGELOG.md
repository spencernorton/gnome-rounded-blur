# Changelog

All notable changes to this packaging of GNOME Rounded Blur are documented here.

## 2.1.0 — 2026-11-28

- Release offscreen blur framebuffers and detach pipeline texture references
  automatically after an actor remains unmapped for `unmap-release-delay` ms
  (default `2000`), and expose `release_targets()` for explicit cleanup.
  Remapping marks the backdrop dirty so restored windows never flash a stale
  cached frame.
- Fold an optional continuous squircle mask (`corner-exponent`, `2.0` circular
  default up to `5.0`, `3.0` continuous squircle) and deterministic 1/255
  interleaved gradient noise dither (`dither`, default `true`) into the final
  composite pass with zero additional framebuffers.

## 2.0.0 — 2026-11-26

- Background blur reuses its GPU render targets across paints and applies
  brightness and rounded corners in the final pass. Actor blur keeps its
  Gaussian filter. A compositor can also select Gaussian background blur
  with `NORVI_BLUR_ALGORITHM=gaussian` before startup.
- Extensions can opt into backdrop caching with `cache-background` and
  invalidate it when content behind the actor changes. The cache handles
  view crossings and reuses complete captures for screenshots.
- Skip texture allocation while a minimizing or restoring actor projects
  to less than one pixel.

## 1.0.2 — 2026-09-29

The first release from this repository, on upstream `f3bfcc7`, which follows upstream's `v1.0.1`. Debian version `1.0.2-1`.

- Upstream's anti-aliased corner mask.
- The mutter ABI is a meson option, and the Debian build derives it from the installed `libmutter-*-dev`.
- Moving the effect to another actor resets its cached content and texture size.
- gnome-shell's copyright line is back on the two files derived from `shell-blur-effect`.
