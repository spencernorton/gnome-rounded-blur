#!/bin/sh
# The two changes this fork carries over upstream. A re-sync that drops either
# fails here instead of shipping quietly.
set -eu
cd "$(dirname "$0")/.."

require() {  # <file> <exact line>
  grep -Fq -- "$2" "$1" || { echo "missing in $1: $2" >&2; exit 1; }
}

# Moving the effect to another actor must invalidate the cached content and
# the size used to decide whether the framebuffers can be reused.
require src/rounded-blur-effect.c '  self->cache_flags = 0;'
require src/rounded-blur-effect.c '  self->tex_width = 0;'
require src/rounded-blur-effect.c '  self->tex_height = 0;'
require src/rounded-blur-effect.c '  self->downscale_factor = 1.f;'

# The mutter ABI is a build option (debian/rules derives it), never a literal.
require meson.build "mutter_api_version = get_option('mutter_api_version')"
require meson.build "libmutter_dep = dependency('libmutter-' + mutter_api_version)"
if grep -Eq "dependency\('libmutter-[0-9]+'\)" meson.build; then
  echo "meson.build hardcodes a mutter ABI again" >&2; exit 1
fi

# Unmapped actors must cancel timers and clear pipeline texture refs on release.
require src/rounded-blur-effect.c '  g_clear_handle_id (&self->unmap_release_id, g_source_remove);'
require src/rounded-blur-effect.c '    cogl_pipeline_set_layer_null_texture (self->kawase_final_pipeline, 0);'

echo "source invariants: ok"
