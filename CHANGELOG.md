# Changelog

All notable changes to this packaging of GNOME Rounded Blur are documented here.

## Unreleased

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
