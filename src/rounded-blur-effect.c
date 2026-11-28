/* rounded-blur-effect.c
 *
 * Copyright 2019 Georges Basile Stavracas Neto
 * Copyright 2025 GNOME Rounded Blur
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "rounded-blur-effect.h"

//#include <cogl/cogl.h>

//#include "bsm-enum-types.h"

static const gchar *brightness_glsl_declarations =
"uniform float brightness;                                                 \n";

static const gchar *brightness_glsl =
"  cogl_color_out.rgb *= brightness;                                       \n";

static const gchar *mask_glsl_declarations =
"uniform float u_corner_radius;                                            \n"
"uniform vec2  u_size;                                                     \n"
"uniform float u_corner_exponent;                                          \n";

static const gchar *mask_glsl =
"  vec2 uv = cogl_tex_coord_in[0].st;                                      \n"
"  vec2 size = max(u_size, vec2(1.0));                                     \n"
"  vec2 half_size = max(size * 0.5, vec2(0.5));                            \n"
"  float radius = clamp(u_corner_radius,                                   \n"
"                       0.0,                                               \n"
"                       max(min(half_size.x, half_size.y) - 0.5, 0.0));    \n"
"  float m = 1.0;                                                          \n"
"  if (radius > 0.0)                                                       \n"
"    {                                                                     \n"
"      vec2 p = uv * size;                                                 \n"
"      vec2 q = abs(p - half_size) - max(half_size - radius, vec2(0.0));   \n"
"      float dist;                                                         \n"
"      if (u_corner_exponent <= 2.01)                                      \n"
"        {                                                                 \n"
"          dist = length(max(q, vec2(0.0))) - radius;                      \n"
"        }                                                                 \n"
"      else                                                                \n"
"        {                                                                 \n"
"          vec2 qc = max(q, vec2(0.0));                                    \n"
"          float mc = max(qc.x, qc.y);                                     \n"
"          if (mc <= 0.0)                                                  \n"
"            {                                                             \n"
"              dist = max(q.x, q.y) - radius;                              \n"
"            }                                                             \n"
"          else                                                            \n"
"            {                                                             \n"
"              float rmin = min(qc.x, qc.y) / mc;                          \n"
"              float lp = (rmin > 0.0)                                     \n"
"                ? mc * pow(1.0 + pow(rmin, u_corner_exponent),            \n"
"                           1.0 / u_corner_exponent)                       \n"
"                : mc;                                                     \n"
"              dist = lp - radius;                                         \n"
"            }                                                             \n"
"        }                                                                 \n"
"      float aa = max(fwidth(dist) * 0.5, 0.5);                            \n"
"      m = 1.0 - smoothstep(-aa, aa, dist);                                \n"
"    }                                                                     \n"
"  cogl_color_out.rgb *= m;                                                \n"
"  cogl_color_out.a   *= m;                                                \n";

static const gchar *dither_glsl_declarations =
"uniform float u_dither;                                                   \n";

static const gchar *dither_glsl =
"  if (u_dither > 0.5 && cogl_color_out.a > 0.0)                           \n"
"    {                                                                     \n"
"      float ign = fract(52.9829189 *                                      \n"
"                        fract(dot(gl_FragCoord.xy,                        \n"
"                                  vec2(0.06711056, 0.00583715))));        \n"
"      float delta = ((ign - 0.5) / 255.0) * cogl_color_out.a;             \n"
"      cogl_color_out.rgb = clamp(cogl_color_out.rgb + vec3(delta),        \n"
"                                 vec3(0.0),                               \n"
"                                 vec3(cogl_color_out.a));                 \n"
"    }                                                                     \n";

/* Dual Kawase blur (Marius Bjørge, SIGGRAPH 2015), using the kernels of KWin's
 * blur effect (src/plugins/blur/shaders/downsample.frag and upsample.frag).
 * `halfpixel` is half a texel of the texture being read; `offset` sets the
 * spread. Each pass reads one level and writes the next, at half or double size.
 */
static const gchar *kawase_glsl_declarations =
"uniform vec2 halfpixel;                                                   \n"
"uniform float offset;                                                     \n";

static const gchar *kawase_down_glsl =
"  vec2 uv = cogl_tex_coord.st;                                            \n"
"  vec2 o = halfpixel * offset;                                            \n"
"  vec4 sum = texture2D (cogl_sampler, uv) * 4.0;                          \n"
"  sum += texture2D (cogl_sampler, uv - o);                                \n"
"  sum += texture2D (cogl_sampler, uv + o);                                \n"
"  sum += texture2D (cogl_sampler, uv + vec2 (o.x, -o.y));                 \n"
"  sum += texture2D (cogl_sampler, uv - vec2 (o.x, -o.y));                 \n"
"  cogl_texel = sum / 8.0;                                                 \n";

static const gchar *kawase_up_glsl =
"  vec2 uv = cogl_tex_coord.st;                                            \n"
"  vec2 o = halfpixel * offset;                                            \n"
"  vec4 sum = texture2D (cogl_sampler, uv + vec2 (-o.x * 2.0, 0.0));       \n"
"  sum += texture2D (cogl_sampler, uv + vec2 (-o.x, o.y)) * 2.0;           \n"
"  sum += texture2D (cogl_sampler, uv + vec2 (0.0, o.y * 2.0));            \n"
"  sum += texture2D (cogl_sampler, uv + vec2 (o.x, o.y)) * 2.0;            \n"
"  sum += texture2D (cogl_sampler, uv + vec2 (o.x * 2.0, 0.0));            \n"
"  sum += texture2D (cogl_sampler, uv + vec2 (o.x, -o.y)) * 2.0;           \n"
"  sum += texture2D (cogl_sampler, uv + vec2 (0.0, -o.y * 2.0));           \n"
"  sum += texture2D (cogl_sampler, uv + vec2 (-o.x, -o.y)) * 2.0;          \n"
"  cogl_texel = sum / 12.0;                                                \n";

#define KAWASE_MAX_ITERATIONS 4

#define MIN_DOWNSCALE_SIZE 256.f
#define MAX_RADIUS 12.f

typedef enum
{
  ACTOR_PAINTED = 1 << 0,
  BLUR_APPLIED = 1 << 1,
} CacheFlags;

typedef struct
{
  CoglFramebuffer *framebuffer;
  CoglPipeline *pipeline;
  CoglTexture *texture;
} FramebufferData;

struct _GbBlurEffect
{
  ClutterEffect parent_instance;

  ClutterActor *actor;

  unsigned int tex_width;
  unsigned int tex_height;

  /* The cached contents */
  FramebufferData actor_fb;
  CacheFlags cache_flags;

  FramebufferData background_fb;
  FramebufferData brightness_fb;
  int brightness_uniform;

  FramebufferData mask_fb;
  int corner_radius_uniform;
  int mask_size_uniform;
  int corner_exponent_uniform;

  GbBlurMode mode;
  float downscale_factor;
  float brightness;
  int radius;

  float corner_radius;
  float corner_exponent;
  gboolean dither;

  gulong actor_mapped_id;
  guint unmap_release_id;
  guint unmap_release_delay_ms;

  /* Dual Kawase chain for BACKGROUND mode. Every target is allocated when the
   * size or pass count changes and reused on every other frame: no texture or
   * framebuffer is created or freed per paint. down[i] is 1/2^(i+1) of the
   * captured background; up[k] has the size of down[k]. */
  gboolean use_kawase;
  int kawase_iterations;
  float kawase_offset;
  FramebufferData kawase_down[KAWASE_MAX_ITERATIONS];
  FramebufferData kawase_up[KAWASE_MAX_ITERATIONS - 1];
  CoglPipeline *kawase_bg_pipeline;
  unsigned int kawase_alloc_width;
  unsigned int kawase_alloc_height;
  int kawase_alloc_iterations;
  float kawase_alloc_offset;

  /* Reuse (BACKGROUND + Kawase): when cache_background is set, the blurred
   * result is painted again without capturing or blurring, until invalidate()
   * says something behind the actor changed, or its box or stage view moved.
   * Off by default, so callers that never invalidate keep re-blurring. */
  gboolean cache_background;
  gboolean background_dirty;

  /* Kawase final stage: brightness and the rounded mask in one pass that reads
   * brightness_fb directly (no separate mask target). Uniforms are written only
   * when a value changes, because Cogl has no equality check and every write
   * notifies (and can copy or flush) the pipeline. */
  CoglPipeline *kawase_final_pipeline;
  int kf_brightness_uniform;
  int kf_corner_radius_uniform;
  int kf_size_uniform;
  int kf_corner_exponent_uniform;
  int kf_dither_uniform;
  float kf_brightness;
  float kf_corner_radius;
  float kf_corner_exponent;
  int kf_dither;
  float kf_width;
  float kf_height;
  float last_brightness;
  float last_corner_radius;
  float last_corner_exponent;
  float last_mask_width;
  float last_mask_height;
  int last_paint_opacity;
  ClutterActorBox cache_box;
  ClutterStageView *cache_view;
  gboolean cache_complete;   /* the last capture covered the actor inside one view */
};

G_DEFINE_TYPE (GbBlurEffect, gb_blur_effect, CLUTTER_TYPE_EFFECT)

/* NORVI_BLUR_ALGORITHM=gaussian restores the previous per-paint Gaussian path. */
static gboolean kawase_disabled = FALSE;

enum {
  PROP_0,
  PROP_RADIUS,
  PROP_BRIGHTNESS,
  PROP_MODE,
  PROP_CORNER_RADIUS,
  PROP_CORNER_EXPONENT,
  PROP_DITHER,
  PROP_UNMAP_RELEASE_DELAY,
  PROP_CACHE_BACKGROUND,
  N_PROPS
};

static GParamSpec *properties [N_PROPS] = { NULL, };

static CoglPipeline*
create_base_pipeline (void)
{
  static CoglPipeline *base_pipeline = NULL;

  if (G_UNLIKELY (base_pipeline == NULL))
    {
       ClutterBackend *backend = clutter_get_default_backend ();
      
      CoglContext *ctx =
        clutter_backend_get_cogl_context (backend);
      
        
      

      base_pipeline = cogl_pipeline_new (ctx);
      cogl_pipeline_set_layer_null_texture (base_pipeline, 0);
      cogl_pipeline_set_layer_filters (base_pipeline,
                                       0,
                                       COGL_PIPELINE_FILTER_LINEAR,
                                       COGL_PIPELINE_FILTER_LINEAR);
      cogl_pipeline_set_layer_wrap_mode (base_pipeline,
                                         0,
                                         COGL_PIPELINE_WRAP_MODE_CLAMP_TO_EDGE);
    }

  return cogl_pipeline_copy (base_pipeline);
}

static CoglPipeline*
create_brightness_pipeline (void)
{
  static CoglPipeline *brightness_pipeline = NULL;

  if (G_UNLIKELY (brightness_pipeline == NULL))
    {
      CoglSnippet *snippet;

      brightness_pipeline = create_base_pipeline ();

      snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                                  brightness_glsl_declarations,
                                  brightness_glsl);
      cogl_pipeline_add_snippet (brightness_pipeline, snippet);
      g_object_unref (snippet);
    }

  return cogl_pipeline_copy (brightness_pipeline);
}

static CoglPipeline*
create_mask_pipeline (void)
{
  static CoglPipeline *mask_pipeline = NULL;

  if (G_UNLIKELY (mask_pipeline == NULL))
  {
    CoglSnippet *snippet;
    mask_pipeline = create_base_pipeline ();
    snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                                mask_glsl_declarations,
                                mask_glsl);
    cogl_pipeline_add_snippet (mask_pipeline, snippet);
    g_object_unref (snippet);
  }

  return cogl_pipeline_copy (mask_pipeline);
}

static CoglPipeline *
create_kawase_final_pipeline (void)
{
  static CoglPipeline *final_pipeline = NULL;

  if (G_UNLIKELY (final_pipeline == NULL))
    {
      CoglSnippet *snippet;

      final_pipeline = create_mask_pipeline ();
      snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                                  brightness_glsl_declarations,
                                  brightness_glsl);
      cogl_pipeline_add_snippet (final_pipeline, snippet);
      g_object_unref (snippet);

      snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_FRAGMENT,
                                  dither_glsl_declarations,
                                  dither_glsl);
      cogl_pipeline_add_snippet (final_pipeline, snippet);
      g_object_unref (snippet);
    }

  return cogl_pipeline_copy (final_pipeline);
}

static void
update_kawase_final (GbBlurEffect *self,
                     float          width,
                     float          height)
{
  CoglPipeline *p = self->kawase_final_pipeline;

  if (self->kf_brightness != self->brightness)
    {
      self->kf_brightness = self->brightness;
      if (self->kf_brightness_uniform > -1)
        cogl_pipeline_set_uniform_1f (p, self->kf_brightness_uniform, self->brightness);
    }
  if (self->kf_corner_radius != self->corner_radius)
    {
      self->kf_corner_radius = self->corner_radius;
      if (self->kf_corner_radius_uniform > -1)
        cogl_pipeline_set_uniform_1f (p, self->kf_corner_radius_uniform, self->corner_radius);
    }
  if (self->kf_corner_exponent != self->corner_exponent)
    {
      self->kf_corner_exponent = self->corner_exponent;
      if (self->kf_corner_exponent_uniform > -1)
        cogl_pipeline_set_uniform_1f (p, self->kf_corner_exponent_uniform, self->corner_exponent);
    }
  if (self->kf_dither != (int) self->dither)
    {
      self->kf_dither = (int) self->dither;
      if (self->kf_dither_uniform > -1)
        cogl_pipeline_set_uniform_1f (p, self->kf_dither_uniform, self->dither ? 1.f : 0.f);
    }
  if (self->kf_width != width || self->kf_height != height)
    {
      const float size[2] = { width, height };

      self->kf_width = width;
      self->kf_height = height;
      if (self->kf_size_uniform > -1)
        cogl_pipeline_set_uniform_float (p, self->kf_size_uniform, 2, 1, size);
    }
}

static void
update_brightness (GbBlurEffect *self,
                   uint8_t        paint_opacity)
{
  CoglColor color;

  if (self->last_paint_opacity == paint_opacity &&
      self->last_brightness == self->brightness)
    return;
  self->last_paint_opacity = paint_opacity;
  self->last_brightness = self->brightness;

  cogl_color_init_from_4f (&color,
                           paint_opacity / 255.0, paint_opacity / 255.0,
                           paint_opacity / 255.0, paint_opacity / 255.0);
  cogl_pipeline_set_color (self->brightness_fb.pipeline, &color);

  if (self->brightness_uniform > -1)
    {
      cogl_pipeline_set_uniform_1f (self->brightness_fb.pipeline,
                                    self->brightness_uniform,
                                    self->brightness);
    }
}

static void
update_mask_uniforms (GbBlurEffect *self,
                      float          width,
                      float          height)
{
  if (!self->mask_fb.pipeline)
    return;

  if (self->last_mask_width == width && self->last_mask_height == height &&
      self->last_corner_radius == self->corner_radius &&
      self->last_corner_exponent == self->corner_exponent)
    return;
  self->last_mask_width = width;
  self->last_mask_height = height;
  self->last_corner_radius = self->corner_radius;
  self->last_corner_exponent = self->corner_exponent;

  if (self->corner_radius_uniform > -1)
    cogl_pipeline_set_uniform_1f (self->mask_fb.pipeline,
                                  self->corner_radius_uniform,
                                  self->corner_radius);

  if (self->corner_exponent_uniform > -1)
    cogl_pipeline_set_uniform_1f (self->mask_fb.pipeline,
                                  self->corner_exponent_uniform,
                                  self->corner_exponent);

  if (self->mask_size_uniform > -1)
  {
    const float size[2] = { width, height };
    cogl_pipeline_set_uniform_float (self->mask_fb.pipeline,
                                     self->mask_size_uniform,
                                     2, 1, size);
  }
}

static void
setup_projection_matrix (CoglFramebuffer *framebuffer,
                         float            width,
                         float            height)
{
  graphene_matrix_t projection;

  graphene_matrix_init_translate (&projection,
                                  &GRAPHENE_POINT3D_INIT (-width / 2.0,
                                                          -height / 2.0,
                                                          0.f));
  graphene_matrix_scale (&projection, 2.0 / width, -2.0 / height, 1.f);

  cogl_framebuffer_set_projection_matrix (framebuffer, &projection);
}

static CoglPipeline *
create_kawase_pipeline (gboolean up)
{
  static CoglPipeline *down_base = NULL;
  static CoglPipeline *up_base = NULL;
  CoglPipeline **base = up ? &up_base : &down_base;

  if (G_UNLIKELY (*base == NULL))
    {
      CoglSnippet *snippet;

      *base = create_base_pipeline ();
      snippet = cogl_snippet_new (COGL_SNIPPET_HOOK_TEXTURE_LOOKUP,
                                  kawase_glsl_declarations,
                                  NULL);
      cogl_snippet_set_replace (snippet, up ? kawase_up_glsl : kawase_down_glsl);
      cogl_pipeline_add_layer_snippet (*base, 0, snippet);
      g_object_unref (snippet);
    }

  return cogl_pipeline_copy (*base);
}

static void
set_kawase_uniforms (CoglPipeline *pipeline,
                     CoglTexture  *source,
                     float         offset)
{
  int location;
  float halfpixel[2];

  halfpixel[0] = 0.5f / MAX (cogl_texture_get_width (source), 1);
  halfpixel[1] = 0.5f / MAX (cogl_texture_get_height (source), 1);

  location = cogl_pipeline_get_uniform_location (pipeline, "halfpixel");
  if (location > -1)
    cogl_pipeline_set_uniform_float (pipeline, location, 2, 1, halfpixel);

  location = cogl_pipeline_get_uniform_location (pipeline, "offset");
  if (location > -1)
    cogl_pipeline_set_uniform_1f (pipeline, location, offset);
}

/* Pass count and spread that match a Gaussian of sigma = radius / 2, the
 * convention of ClutterBlur. Fitted on step responses (mean error < 0.3%):
 * 4..8 -> 1 pass, ..20 -> 2, ..60 -> 3 (radius 30: offset 2.5), above -> 4. */
static void
kawase_params_for_radius (int    radius,
                          int   *iterations,
                          float *offset)
{
  float r = (float) radius;

  if (r <= 8.f)
    {
      *iterations = 1;
      *offset = CLAMP (0.25f * r + 0.5f, 1.f, 2.f);
    }
  else if (r <= 20.f)
    {
      *iterations = 2;
      *offset = CLAMP (2.f + (r - 12.f) / 6.f, 2.f, 3.f);
    }
  else if (r <= 60.f)
    {
      *iterations = 3;
      *offset = CLAMP (2.f + (r - 24.f) / 12.f, 2.f, 5.f);
    }
  else
    {
      *iterations = 4;
      *offset = CLAMP (3.25f + (r - 80.f) / 20.f, 3.f, 8.f);
    }
}

static void
clear_kawase (GbBlurEffect *self)
{
  for (int i = 0; i < KAWASE_MAX_ITERATIONS; i++)
    {
      g_clear_object (&self->kawase_down[i].texture);
      g_clear_object (&self->kawase_down[i].framebuffer);
      g_clear_object (&self->kawase_down[i].pipeline);
    }
  for (int k = 0; k < KAWASE_MAX_ITERATIONS - 1; k++)
    {
      g_clear_object (&self->kawase_up[k].texture);
      g_clear_object (&self->kawase_up[k].framebuffer);
      g_clear_object (&self->kawase_up[k].pipeline);
    }
  g_clear_object (&self->kawase_bg_pipeline);
  self->kawase_alloc_width = 0;
  self->kawase_alloc_height = 0;
  self->kawase_alloc_iterations = 0;
}

static gboolean update_fbo (FramebufferData *data, unsigned int width,
                            unsigned int height, float downscale_factor);

/* Allocate the chain for a capture of width x height, only when that size or the
 * pass count changed. Returns FALSE if a target could not be created. */
static gboolean
update_kawase_fbos (GbBlurEffect *self,
                    unsigned int   width,
                    unsigned int   height)
{
  int n = self->kawase_iterations;

  if (self->kawase_alloc_width == width &&
      self->kawase_alloc_height == height &&
      self->kawase_alloc_iterations == n &&
      self->kawase_down[0].framebuffer &&
      self->kawase_bg_pipeline)
    {
      if (self->kawase_alloc_offset != self->kawase_offset)
        {
          set_kawase_uniforms (self->kawase_bg_pipeline, self->background_fb.texture,
                               self->kawase_offset);
          for (int i = 0; i < n; i++)
            set_kawase_uniforms (self->kawase_down[i].pipeline,
                                 self->kawase_down[i].texture, self->kawase_offset);
          for (int k = 0; k < n - 1; k++)
            set_kawase_uniforms (self->kawase_up[k].pipeline,
                                 self->kawase_up[k].texture, self->kawase_offset);
          self->kawase_alloc_offset = self->kawase_offset;
        }
      return TRUE;
    }

  clear_kawase (self);

  for (int i = 0; i < n; i++)
    {
      FramebufferData *data = &self->kawase_down[i];

      /* every level but the last is read by a downsample; the last by the first upsample */
      data->pipeline = create_kawase_pipeline (i == n - 1);
      if (!update_fbo (data, width, height, (float) (1 << (i + 1))))
        return FALSE;
      set_kawase_uniforms (data->pipeline, data->texture, self->kawase_offset);
    }

  for (int k = 0; k < n - 1; k++)
    {
      FramebufferData *data = &self->kawase_up[k];

      data->pipeline = create_kawase_pipeline (TRUE);
      if (!update_fbo (data, width, height, (float) (1 << (k + 1))))
        return FALSE;
      set_kawase_uniforms (data->pipeline, data->texture, self->kawase_offset);
    }

  self->kawase_bg_pipeline = create_kawase_pipeline (FALSE);
  cogl_pipeline_set_layer_texture (self->kawase_bg_pipeline, 0, self->background_fb.texture);
  set_kawase_uniforms (self->kawase_bg_pipeline, self->background_fb.texture,
                       self->kawase_offset);

  self->kawase_alloc_width = width;
  self->kawase_alloc_height = height;
  self->kawase_alloc_iterations = n;
  self->kawase_alloc_offset = self->kawase_offset;

  return TRUE;
}

static gboolean
update_fbo (FramebufferData *data,
            unsigned int     width,
            unsigned int     height,
            float            downscale_factor)
{
   ClutterBackend *backend = clutter_get_default_backend ();
  
  CoglContext *ctx =
    clutter_backend_get_cogl_context (backend);
  
    
  

  g_clear_object (&data->texture);
  g_clear_object (&data->framebuffer);

  float new_width = floorf (width / downscale_factor);
  float new_height = floorf (height / downscale_factor);

  data->texture = cogl_texture_2d_new_with_size (ctx, new_width, new_height);
  if (!data->texture)
    return FALSE;

  cogl_pipeline_set_layer_texture (data->pipeline, 0, data->texture);

  data->framebuffer =
    COGL_FRAMEBUFFER (cogl_offscreen_new_with_texture (data->texture));
  if (!data->framebuffer)
    {
      g_warning ("%s: Unable to create an Offscreen buffer", G_STRLOC);
      return FALSE;
    }

  setup_projection_matrix (data->framebuffer, new_width, new_height);

  return TRUE;
}

static gboolean
update_actor_fbo (GbBlurEffect *self,
                  unsigned int   width,
                  unsigned int   height,
                  float          downscale_factor)
{
  if (self->tex_width == width &&
      self->tex_height == height &&
      self->downscale_factor == downscale_factor &&
      self->actor_fb.framebuffer)
    {
      return TRUE;
    }

  self->cache_flags &= ~ACTOR_PAINTED;

  return update_fbo (&self->actor_fb, width, height, downscale_factor);
}

static gboolean
update_brightness_fbo (GbBlurEffect *self,
                       unsigned int   width,
                       unsigned int   height,
                       float          downscale_factor)
{
  if (self->tex_width == width &&
      self->tex_height == height &&
      self->downscale_factor == downscale_factor &&
      self->brightness_fb.framebuffer)
    {
      return TRUE;
    }

  return update_fbo (&self->brightness_fb,
                     width, height,
                     downscale_factor);
}

static gboolean
update_background_fbo (GbBlurEffect *self,
                       unsigned int   width,
                       unsigned int   height)
{
  if (self->tex_width == width &&
      self->tex_height == height &&
      self->background_fb.framebuffer)
    {
      return TRUE;
    }

  return update_fbo (&self->background_fb, width, height, 1.0);
}

static gboolean
update_mask_fbo (GbBlurEffect *self,
                 unsigned int   width,
                 unsigned int   height,
                 float          downscale_factor)
{
  if (self->tex_width == width &&
      self->tex_height == height &&
      self->downscale_factor == downscale_factor &&
      self->mask_fb.framebuffer)
  {
    return TRUE;
  }

  return update_fbo (&self->mask_fb, width, height, downscale_factor);
}

static void
clear_framebuffer_data (FramebufferData *fb_data)
{
  g_clear_object (&fb_data->texture);
  g_clear_object (&fb_data->framebuffer);
}

static float
calculate_downscale_factor (float width,
                            float height,
                            float radius)
{
  float downscale_factor = 1.0;
  float scaled_width = width;
  float scaled_height = height;
  float scaled_radius = radius;

  /* This is the algorithm used by Firefox; keep downscaling until either the
   * blur radius is lower than the threshold, or the downscaled texture is too
   * small.
   */
  while (scaled_radius > MAX_RADIUS &&
         scaled_width > MIN_DOWNSCALE_SIZE &&
         scaled_height > MIN_DOWNSCALE_SIZE)
    {
      downscale_factor *= 2.f;

      scaled_width = width / downscale_factor;
      scaled_height = height / downscale_factor;
      scaled_radius = radius / downscale_factor;
    }

  return downscale_factor;
}

static gboolean
has_allocated_targets (GbBlurEffect *self)
{
  return self->tex_width > 0 ||
         self->actor_fb.framebuffer != NULL ||
         self->background_fb.framebuffer != NULL ||
         self->brightness_fb.framebuffer != NULL ||
         self->mask_fb.framebuffer != NULL ||
         self->kawase_down[0].framebuffer != NULL;
}

static void
cancel_unmap_release (GbBlurEffect *self)
{
  g_clear_handle_id (&self->unmap_release_id, g_source_remove);
}

static void
release_framebuffers (GbBlurEffect *self)
{
  /* clear out the previous state */
  clear_framebuffer_data (&self->actor_fb);
  clear_framebuffer_data (&self->background_fb);
  clear_framebuffer_data (&self->brightness_fb);
  clear_framebuffer_data (&self->mask_fb);
  clear_kawase (self);

  /* Detach released textures from persistent pipelines so Cogl drops its
   * GPU references immediately instead of pinning VRAM on layer 0. */
  if (self->actor_fb.pipeline)
    cogl_pipeline_set_layer_null_texture (self->actor_fb.pipeline, 0);
  if (self->background_fb.pipeline)
    cogl_pipeline_set_layer_null_texture (self->background_fb.pipeline, 0);
  if (self->brightness_fb.pipeline)
    cogl_pipeline_set_layer_null_texture (self->brightness_fb.pipeline, 0);
  if (self->mask_fb.pipeline)
    cogl_pipeline_set_layer_null_texture (self->mask_fb.pipeline, 0);
  if (self->kawase_final_pipeline)
    cogl_pipeline_set_layer_null_texture (self->kawase_final_pipeline, 0);

  /* A new actor or restored window must not reuse old cached content, or the
   * previous texture size to decide whether framebuffers still fit. */
  self->cache_flags = 0;
  self->background_dirty = TRUE;
  self->cache_view = NULL;
  self->cache_complete = FALSE;
  self->tex_width = 0;
  self->tex_height = 0;
  self->downscale_factor = 1.f;
}

static gboolean
on_unmap_release_timeout (gpointer user_data)
{
  GbBlurEffect *self = GB_BLUR_EFFECT (user_data);

  self->unmap_release_id = 0;

  if (self->actor && clutter_actor_is_mapped (self->actor))
    return G_SOURCE_REMOVE;

  release_framebuffers (self);
  return G_SOURCE_REMOVE;
}

static void
schedule_unmap_release (GbBlurEffect *self)
{
  cancel_unmap_release (self);

  if (self->unmap_release_delay_ms == 0 || !has_allocated_targets (self))
    return;

  self->unmap_release_id = g_timeout_add (self->unmap_release_delay_ms,
                                          on_unmap_release_timeout,
                                          self);
}

static void
on_actor_mapped_changed (ClutterActor *actor,
                         GParamSpec   *pspec,
                         GbBlurEffect *self)
{
  (void) pspec;

  if (clutter_actor_is_mapped (actor))
    {
      cancel_unmap_release (self);
      /* Force a fresh backdrop capture on remap so windows restored from
       * minimization or another workspace never flash a stale frame. */
      self->background_dirty = TRUE;
    }
  else
    {
      schedule_unmap_release (self);
    }
}

static void
gb_blur_effect_set_actor (ClutterActorMeta *meta,
                           ClutterActor     *actor)
{
  GbBlurEffect *self = GB_BLUR_EFFECT (meta);
  ClutterActorMetaClass *meta_class;

  cancel_unmap_release (self);
  if (self->actor)
    g_clear_signal_handler (&self->actor_mapped_id, self->actor);

  meta_class = CLUTTER_ACTOR_META_CLASS (gb_blur_effect_parent_class);
  meta_class->set_actor (meta, actor);

  release_framebuffers (self);

  /* we keep a back pointer here, to avoid going through the ActorMeta */
  self->actor = clutter_actor_meta_get_actor (meta);

  if (self->actor)
    {
      self->actor_mapped_id = g_signal_connect (self->actor,
                                                "notify::mapped",
                                                G_CALLBACK (on_actor_mapped_changed),
                                                self);
    }
}

static void
update_actor_box (GbBlurEffect       *self,
                  ClutterPaintContext *paint_context,
                  ClutterActorBox     *source_actor_box)
{
  ClutterStageView *stage_view;
  float box_scale_factor = 1.0f;
  float origin_x, origin_y;
  float width, height;

  switch (self->mode)
    {
    case GB_BLUR_MODE_ACTOR:
      clutter_actor_get_allocation_box (self->actor, source_actor_box);
      break;

    case GB_BLUR_MODE_BACKGROUND:
      stage_view = clutter_paint_context_get_stage_view (paint_context);

      clutter_actor_get_transformed_position (self->actor, &origin_x, &origin_y);
      clutter_actor_get_transformed_size (self->actor, &width, &height);

      if (stage_view)
        {
          MtkRectangle stage_view_layout;

          box_scale_factor = clutter_stage_view_get_scale (stage_view);
          clutter_stage_view_get_layout (stage_view, &stage_view_layout);

          origin_x -= stage_view_layout.x;
          origin_y -= stage_view_layout.y;
        }
      else
        {
          /* If we're drawing off stage, just assume scale = 1, this won't work
           * with stage-view scaling though.
           */
        }

      clutter_actor_box_set_origin (source_actor_box, origin_x, origin_y);
      clutter_actor_box_set_size (source_actor_box, width, height);

      clutter_actor_box_scale (source_actor_box, box_scale_factor);
      break;
    }

  clutter_actor_box_clamp_to_pixel (source_actor_box);
}

static void
add_blurred_pipeline (GbBlurEffect    *self,
                      ClutterPaintNode *node,
                      uint8_t           paint_opacity)
{
  g_autoptr (ClutterPaintNode) pipeline_node = NULL;
  float width, height;

  /* Use the untransformed actor size here, since the framebuffer itself already
   * has the actor transform matrix applied.
   */
  clutter_actor_get_size (self->actor, &width, &height);

  if (self->use_kawase && self->kawase_final_pipeline)
    {
      update_kawase_final (self, width, height);
      pipeline_node = clutter_pipeline_node_new (self->kawase_final_pipeline);
      clutter_paint_node_set_static_name (pipeline_node, "GbBlurEffect (cached)");
      clutter_paint_node_add_child (node, pipeline_node);
      clutter_paint_node_add_rectangle (pipeline_node,
                                        &(ClutterActorBox) { 0.f, 0.f, width, height });
      return;
    }

  update_brightness (self, paint_opacity);
  update_mask_uniforms (self, width, height);

  pipeline_node = clutter_pipeline_node_new (self->mask_fb.pipeline);
  clutter_paint_node_set_static_name (pipeline_node, "GbBlurEffect (final)");
  clutter_paint_node_add_child (node, pipeline_node);

  clutter_paint_node_add_rectangle (pipeline_node,
                                    &(ClutterActorBox) {
                                      0.f, 0.f,
                                      width,
                                      height,
                                    });
}

static ClutterPaintNode *
create_blur_nodes (GbBlurEffect    *self,
                   ClutterPaintNode *node,
                   uint8_t           paint_opacity)
{
  g_autoptr (ClutterPaintNode) brightness_node = NULL;
  g_autoptr (ClutterPaintNode) blur_node = NULL;
  g_autoptr (ClutterPaintNode) mask_node = NULL;
  float width;
  float height;

  clutter_actor_get_size (self->actor, &width, &height);

  update_mask_uniforms (self, width, height);
  mask_node = clutter_layer_node_new_to_framebuffer (self->mask_fb.framebuffer,
                                                     self->mask_fb.pipeline);
  clutter_paint_node_set_static_name (mask_node, "ShellBlurEffect (mask)");
  clutter_paint_node_add_child (node, mask_node);
  clutter_paint_node_add_rectangle (mask_node,
                                    &(ClutterActorBox) {
                                      0.f, 0.f,
                                      width, height,
                                    });

  update_brightness (self, paint_opacity);
  brightness_node = clutter_layer_node_new_to_framebuffer (self->brightness_fb.framebuffer,
                                                           self->brightness_fb.pipeline);
  clutter_paint_node_set_static_name (brightness_node, "ShellBlurEffect (brightness)");
  clutter_paint_node_add_child (mask_node, brightness_node);
  clutter_paint_node_add_rectangle (brightness_node,
                                    &(ClutterActorBox) {
                                      0.f, 0.f,
                                      cogl_texture_get_width (self->mask_fb.texture),
                                      cogl_texture_get_height (self->mask_fb.texture),
                                    });

  blur_node = clutter_blur_node_new (self->tex_width / self->downscale_factor,
                                     self->tex_height / self->downscale_factor,
                                     self->radius / self->downscale_factor);
  clutter_paint_node_set_static_name (blur_node, "ShellBlurEffect (blur)");
  clutter_paint_node_add_child (brightness_node, blur_node);
  clutter_paint_node_add_rectangle (blur_node,
                                    &(ClutterActorBox) {
                                      0.f, 0.f,
                                      cogl_texture_get_width (self->mask_fb.texture),
                                      cogl_texture_get_height (self->mask_fb.texture),
                                    });

  self->cache_flags |= BLUR_APPLIED;

  return g_steal_pointer (&blur_node);
}

static ClutterPaintNode *
add_pass (ClutterPaintNode *parent,
          FramebufferData  *target,
          CoglPipeline     *composite_pipeline,
          float             parent_width,
          float             parent_height,
          const char       *name)
{
  ClutterPaintNode *layer;

  /* Render the children into `target`, then draw target's texture into the
   * parent through `composite_pipeline`: that draw is the Kawase pass. */
  layer = clutter_layer_node_new_to_framebuffer (target->framebuffer, composite_pipeline);
  clutter_paint_node_set_static_name (layer, name);
  clutter_paint_node_add_child (parent, layer);
  clutter_paint_node_add_rectangle (layer,
                                    &(ClutterActorBox) {
                                      0.f, 0.f, parent_width, parent_height,
                                    });
  clutter_paint_node_unref (layer);   /* the parent holds it */
  return layer;
}

/* mask > brightness > up[0] > ... > up[n-2] > down[n-1] > ... > down[0]; the
 * captured background is added inside down[0] by paint_background(). Painting
 * runs innermost first, so the background is downsampled n times and upsampled
 * back to half size, all in targets that already exist. */
static ClutterPaintNode *
create_kawase_nodes (GbBlurEffect    *self,
                     ClutterPaintNode *node,
                     uint8_t           paint_opacity)
{
  g_autoptr (ClutterPaintNode) mask_node = NULL;
  ClutterPaintNode *parent;
  float width, height, pw, ph;
  int n = self->kawase_alloc_iterations;

  clutter_actor_get_size (self->actor, &width, &height);
  (void) paint_opacity;                 /* always 255 in BACKGROUND mode */

  update_kawase_final (self, width, height);
  mask_node = clutter_layer_node_new_to_framebuffer (self->brightness_fb.framebuffer,
                                                     self->kawase_final_pipeline);
  clutter_paint_node_set_static_name (mask_node, "GbBlurEffect (final: brightness + mask)");
  clutter_paint_node_add_child (node, mask_node);
  clutter_paint_node_add_rectangle (mask_node,
                                    &(ClutterActorBox) { 0.f, 0.f, width, height });
  parent = mask_node;
  pw = cogl_texture_get_width (self->brightness_fb.texture);
  ph = cogl_texture_get_height (self->brightness_fb.texture);

  /* upsample chain, outermost (largest) first */
  for (int k = 0; k < n - 1; k++)
    {
      FramebufferData *up = &self->kawase_up[k];

      parent = add_pass (parent, up, up->pipeline, pw, ph, "GbBlurEffect (kawase up)");
      pw = cogl_texture_get_width (up->texture);
      ph = cogl_texture_get_height (up->texture);
    }

  /* downsample chain, smallest first */
  for (int i = n - 1; i >= 0; i--)
    {
      FramebufferData *down = &self->kawase_down[i];

      parent = add_pass (parent, down, down->pipeline, pw, ph, "GbBlurEffect (kawase down)");
      pw = cogl_texture_get_width (down->texture);
      ph = cogl_texture_get_height (down->texture);
    }

  self->cache_flags |= BLUR_APPLIED;

  return clutter_paint_node_ref (parent);
}

static void
paint_background (GbBlurEffect      *self,
                  ClutterPaintNode    *node,
                  ClutterPaintContext *paint_context,
                  ClutterActorBox     *source_actor_box)
{
  g_autoptr (ClutterPaintNode) background_node = NULL;
  g_autoptr (ClutterPaintNode) blit_node = NULL;
  CoglFramebuffer *src;
  float transformed_x;
  float transformed_y;
  float transformed_width;
  float transformed_height;

  clutter_actor_box_get_origin (source_actor_box,
                                &transformed_x,
                                &transformed_y);
  clutter_actor_box_get_size (source_actor_box,
                              &transformed_width,
                              &transformed_height);

  /* Background layer node */
  background_node =
    clutter_layer_node_new_to_framebuffer (self->background_fb.framebuffer,
                                           self->use_kawase ? self->kawase_bg_pipeline
                                                            : self->background_fb.pipeline);
  clutter_paint_node_set_static_name (background_node, "GbBlurEffect (background)");
  clutter_paint_node_add_child (node, background_node);
  clutter_paint_node_add_rectangle (background_node,
                                    &(ClutterActorBox) {
                                      0.f, 0.f,
                                      self->tex_width / self->downscale_factor,
                                      self->tex_height / self->downscale_factor,
                                    });

  /* Blit node */
  src = clutter_paint_context_get_framebuffer (paint_context);
  blit_node = clutter_blit_node_new (src);
  clutter_paint_node_set_static_name (blit_node, "GbBlurEffect (blit)");
  clutter_paint_node_add_child (background_node, blit_node);
  clutter_blit_node_add_blit_rectangle (CLUTTER_BLIT_NODE (blit_node),
                                        transformed_x,
                                        transformed_y,
                                        0, 0,
                                        transformed_width,
                                        transformed_height);
}

static gboolean
update_framebuffers (GbBlurEffect       *self,
                     ClutterPaintContext *paint_context,
                     ClutterActorBox     *source_actor_box)
{
  gboolean updated = FALSE;
  float downscale_factor;
  float height = -1;
  float width = -1;

  clutter_actor_box_get_size (source_actor_box, &width, &height);

  /* A minimize or restore animation can project the actor to less than one
   * pixel. Keep its targets for the next frame and paint the actor directly. */
  if (width < 1.f || height < 1.f)
    return FALSE;

  /* Kawase works at half size and below; a capture under 4 px in either
   * direction keeps the full-resolution Gaussian path instead. */
  self->use_kawase = !kawase_disabled && self->mode == GB_BLUR_MODE_BACKGROUND &&
                     width >= 4.f && height >= 4.f;
  if (self->use_kawase)
    {
      int n;
      float offset;

      kawase_params_for_radius (self->radius, &n, &offset);
      /* never shrink a level below one pixel */
      while (n > 1 && (MIN (width, height) / (float) (1 << n)) < 1.f)
        n--;
      self->kawase_iterations = n;
      self->kawase_offset = offset;
      downscale_factor = 2.f;           /* the result lives at half size */

      updated = update_brightness_fbo (self, width, height, downscale_factor) &&
                update_background_fbo (self, width, height);
      self->tex_width = width;
      self->tex_height = height;
      self->downscale_factor = downscale_factor;
      if (updated && self->kawase_final_pipeline &&
          cogl_pipeline_get_layer_texture (self->kawase_final_pipeline, 0) !=
          self->brightness_fb.texture)
        cogl_pipeline_set_layer_texture (self->kawase_final_pipeline, 0,
                                         self->brightness_fb.texture);
      return updated && update_kawase_fbos (self, width, height);
    }

  downscale_factor = calculate_downscale_factor (width, height, self->radius);

  updated = update_actor_fbo (self, width, height, downscale_factor) &&
            update_brightness_fbo (self, width, height, downscale_factor) &&
            update_mask_fbo (self, width, height, downscale_factor);

  if (self->mode == GB_BLUR_MODE_BACKGROUND)
    updated = updated && update_background_fbo (self, width, height);

  self->tex_width = width;
  self->tex_height = height;
  self->downscale_factor = downscale_factor;

  return updated;
}

static void
add_actor_node (GbBlurEffect    *self,
                ClutterPaintNode *node,
                int               opacity)
{
  g_autoptr (ClutterPaintNode) actor_node = NULL;

  actor_node = clutter_actor_node_new (self->actor, opacity);
  clutter_paint_node_add_child (node, actor_node);
}

static void
paint_actor_offscreen (GbBlurEffect           *self,
                       ClutterPaintNode        *node,
                       ClutterEffectPaintFlags  flags)
{
  gboolean actor_dirty;

  actor_dirty = (flags & CLUTTER_EFFECT_PAINT_ACTOR_DIRTY) != 0;

  /* The actor offscreen framebuffer is updated already */
  if (actor_dirty || !(self->cache_flags & ACTOR_PAINTED))
    {
      g_autoptr (ClutterPaintNode) transform_node = NULL;
      g_autoptr (ClutterPaintNode) layer_node = NULL;
      graphene_matrix_t transform;

      /* Layer node */
      layer_node = clutter_layer_node_new_to_framebuffer (self->actor_fb.framebuffer,
                                                          self->actor_fb.pipeline);
      clutter_paint_node_set_static_name (layer_node, "GbBlurEffect (actor offscreen)");
      clutter_paint_node_add_child (node, layer_node);
      clutter_paint_node_add_rectangle (layer_node,
                                        &(ClutterActorBox) {
                                          0.f, 0.f,
                                          self->tex_width / self->downscale_factor,
                                          self->tex_height / self->downscale_factor,
                                        });

      /* Transform node */
      graphene_matrix_init_scale (&transform,
                                  1.f / self->downscale_factor,
                                  1.f / self->downscale_factor,
                                  1.f);
      transform_node = clutter_transform_node_new (&transform);
      clutter_paint_node_set_static_name (transform_node, "GbBlurEffect (downscale)");
      clutter_paint_node_add_child (layer_node, transform_node);

      /* Actor node */
      add_actor_node (self, transform_node, 255);

      self->cache_flags |= ACTOR_PAINTED;
    }
  else
    {
      g_autoptr (ClutterPaintNode) pipeline_node = NULL;

      pipeline_node = clutter_pipeline_node_new (self->actor_fb.pipeline);
      clutter_paint_node_set_static_name (pipeline_node,
                                          "GbBlurEffect (actor texture)");
      clutter_paint_node_add_child (node, pipeline_node);
      clutter_paint_node_add_rectangle (pipeline_node,
                                        &(ClutterActorBox) {
                                          0.f, 0.f,
                                          self->tex_width / self->downscale_factor,
                                          self->tex_height / self->downscale_factor,
                                        });
    }
}

static gboolean
needs_repaint (GbBlurEffect         *self,
               ClutterEffectPaintFlags  flags)
{
  gboolean actor_cached;
  gboolean blur_cached;
  gboolean actor_dirty;

  actor_dirty = (flags & CLUTTER_EFFECT_PAINT_ACTOR_DIRTY) != 0;
  blur_cached = (self->cache_flags & BLUR_APPLIED) != 0;
  actor_cached = (self->cache_flags & ACTOR_PAINTED) != 0;

  switch (self->mode)
    {
    case GB_BLUR_MODE_ACTOR:
      return actor_dirty || !blur_cached || !actor_cached;

    case GB_BLUR_MODE_BACKGROUND:
      return !(self->use_kawase && self->cache_background &&
               !self->background_dirty && blur_cached);
    }

  return TRUE;
}

/* TRUE if the box lies inside one stage view: a window spanning monitors is
 * painted once per view, and one cache cannot serve two captures. */
static gboolean
box_within_view (ClutterPaintContext *paint_context,
                 ClutterActorBox     *box)
{
  ClutterStageView *view = clutter_paint_context_get_stage_view (paint_context);
  MtkRectangle layout;
  float scale;

  if (!view)
    return FALSE;
  clutter_stage_view_get_layout (view, &layout);
  scale = clutter_stage_view_get_scale (view);
  return box->x1 >= 0.f && box->y1 >= 0.f &&
         box->x2 <= layout.width * scale && box->y2 <= layout.height * scale;
}

static void
gb_blur_effect_paint_node (ClutterEffect           *effect,
                              ClutterPaintNode        *node,
                              ClutterPaintContext     *paint_context,
                              ClutterEffectPaintFlags  flags)
{
  GbBlurEffect *self = GB_BLUR_EFFECT (effect);
  uint8_t paint_opacity;

  g_assert (self->actor != NULL);

  cancel_unmap_release (self);

  if (self->radius > 0)
    {
      g_autoptr (ClutterPaintNode) blur_node = NULL;

      switch (self->mode)
        {
        case GB_BLUR_MODE_ACTOR:
          paint_opacity = clutter_actor_get_paint_opacity (self->actor);
          break;

        case GB_BLUR_MODE_BACKGROUND:
          paint_opacity = 255;
          break;

        default:
          g_assert_not_reached();
          break;
        }

      if (self->mode == GB_BLUR_MODE_BACKGROUND && self->cache_background &&
          !clutter_paint_context_get_stage_view (paint_context))
        {
          /* Offscreen paint (a screenshot, or a window screencast painting the
           * window alone): a capture here would see only what this paint drew,
           * so show the cached glass if it is a complete, current capture. */
          if (!self->cache_complete || self->background_dirty)
            self->background_dirty = TRUE;
        }
      else if (self->mode == GB_BLUR_MODE_BACKGROUND && self->cache_background)
        {
          ClutterActorBox box;

          update_actor_box (self, paint_context, &box);
          if (!box_within_view (paint_context, &box) ||
              !clutter_actor_box_equal (&box, &self->cache_box) ||
              clutter_paint_context_get_stage_view (paint_context) != self->cache_view)
            self->background_dirty = TRUE;
        }

      if (needs_repaint (self, flags))
        {
          ClutterActorBox source_actor_box;

          update_actor_box (self, paint_context, &source_actor_box);

          /* Failing to create or update the offscreen framebuffers prevents
           * the entire effect to be applied.
           */
          if (!update_framebuffers (self, paint_context, &source_actor_box))
            goto fail;

          blur_node = self->use_kawase ? create_kawase_nodes (self, node, paint_opacity)
                                       : create_blur_nodes (self, node, paint_opacity);

          switch (self->mode)
            {
            case GB_BLUR_MODE_ACTOR:
              paint_actor_offscreen (self, blur_node, flags);
              break;

            case GB_BLUR_MODE_BACKGROUND:
              paint_background (self, blur_node, paint_context, &source_actor_box);
              self->cache_box = source_actor_box;
              self->cache_view = clutter_paint_context_get_stage_view (paint_context);
              self->cache_complete = self->cache_view != NULL &&
                                     box_within_view (paint_context, &source_actor_box);
              self->background_dirty = FALSE;
              break;
            }
        }
      else
        {
          /* Use the cached pipeline if no repaint is needed */
          add_blurred_pipeline (self, node, paint_opacity);
        }

      /* Background blur needs to paint the actor after painting the blurred
       * background.
       */
      switch (self->mode)
        {
        case GB_BLUR_MODE_ACTOR:
          break;

        case GB_BLUR_MODE_BACKGROUND:
          add_actor_node (self, node, -1);
          break;
        }

      if (!clutter_actor_is_mapped (self->actor))
        schedule_unmap_release (self);

      return;
    }

fail:
  /* When no blur is applied, or the offscreen framebuffers
   * couldn't be created, fallback to simply painting the actor.
   */
  add_actor_node (self, node, -1);
}

static void
gb_blur_effect_dispose (GObject *object)
{
  GbBlurEffect *self = (GbBlurEffect *)object;

  G_OBJECT_CLASS (gb_blur_effect_parent_class)->dispose (object);

  cancel_unmap_release (self);
  if (self->actor)
    {
      g_clear_signal_handler (&self->actor_mapped_id, self->actor);
      self->actor = NULL;
    }
}

static void
gb_blur_effect_finalize (GObject *object)
{
  GbBlurEffect *self = (GbBlurEffect *)object;

  cancel_unmap_release (self);

  clear_framebuffer_data (&self->actor_fb);
  clear_framebuffer_data (&self->background_fb);
  clear_framebuffer_data (&self->brightness_fb);
  clear_framebuffer_data (&self->mask_fb);

  g_clear_object (&self->actor_fb.pipeline);
  g_clear_object (&self->background_fb.pipeline);
  g_clear_object (&self->brightness_fb.pipeline);
  g_clear_object (&self->mask_fb.pipeline);
  g_clear_object (&self->kawase_final_pipeline);
  clear_kawase (self);

  G_OBJECT_CLASS (gb_blur_effect_parent_class)->finalize (object);
}

static void
gb_blur_effect_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  GbBlurEffect *self = GB_BLUR_EFFECT (object);

  switch (prop_id)
    {
    case PROP_RADIUS:
      g_value_set_int (value, self->radius);
      break;

    case PROP_BRIGHTNESS:
      g_value_set_float (value, self->brightness);
      break;

    case PROP_MODE:
      g_value_set_enum (value, self->mode);
      break;

    case PROP_CORNER_RADIUS:
      g_value_set_float (value, self->corner_radius);
      break;

    case PROP_CORNER_EXPONENT:
      g_value_set_float (value, self->corner_exponent);
      break;

    case PROP_DITHER:
      g_value_set_boolean (value, self->dither);
      break;

    case PROP_UNMAP_RELEASE_DELAY:
      g_value_set_uint (value, self->unmap_release_delay_ms);
      break;

    case PROP_CACHE_BACKGROUND:
      g_value_set_boolean (value, self->cache_background);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gb_blur_effect_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  GbBlurEffect *self = GB_BLUR_EFFECT (object);

  switch (prop_id)
    {
    case PROP_RADIUS:
      gb_blur_effect_set_radius (self, g_value_get_int (value));
      break;

    case PROP_BRIGHTNESS:
      gb_blur_effect_set_brightness (self, g_value_get_float (value));
      break;

    case PROP_MODE:
      gb_blur_effect_set_mode (self, g_value_get_enum (value));
      break;

    case PROP_CORNER_RADIUS:
      gb_blur_effect_set_corner_radius (self, g_value_get_float (value));
      break;

    case PROP_CORNER_EXPONENT:
      gb_blur_effect_set_corner_exponent (self, g_value_get_float (value));
      break;

    case PROP_DITHER:
      gb_blur_effect_set_dither (self, g_value_get_boolean (value));
      break;

    case PROP_UNMAP_RELEASE_DELAY:
      gb_blur_effect_set_unmap_release_delay (self, g_value_get_uint (value));
      break;

    case PROP_CACHE_BACKGROUND:
      if (self->cache_background != g_value_get_boolean (value))
        {
          self->cache_background = g_value_get_boolean (value);
          self->background_dirty = TRUE;
          if (self->actor)
            clutter_effect_queue_repaint (CLUTTER_EFFECT (self));
        }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
gb_blur_effect_class_init (GbBlurEffectClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  ClutterActorMetaClass *meta_class = CLUTTER_ACTOR_META_CLASS (klass);
  ClutterEffectClass *effect_class = CLUTTER_EFFECT_CLASS (klass);

  object_class->dispose = gb_blur_effect_dispose;
  object_class->finalize = gb_blur_effect_finalize;
  object_class->get_property = gb_blur_effect_get_property;
  object_class->set_property = gb_blur_effect_set_property;

  meta_class->set_actor = gb_blur_effect_set_actor;

  kawase_disabled = g_strcmp0 (g_getenv ("NORVI_BLUR_ALGORITHM"), "gaussian") == 0;

  effect_class->paint_node = gb_blur_effect_paint_node;

  properties[PROP_RADIUS] =
    g_param_spec_int ("radius", NULL, NULL,
                      0, G_MAXINT, 0,
                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  properties[PROP_BRIGHTNESS] =
    g_param_spec_float ("brightness", NULL, NULL,
                        0.f, 1.f, 1.f,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  properties[PROP_MODE] =
    g_param_spec_enum ("mode", NULL, NULL,
                       GB_TYPE_BLUR_MODE,
                       GB_BLUR_MODE_ACTOR,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  properties[PROP_CORNER_RADIUS] =
    g_param_spec_float ("corner-radius", NULL, NULL,
                        0.f, G_MAXFLOAT, 0.f,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  properties[PROP_CORNER_EXPONENT] =
    g_param_spec_float ("corner-exponent", NULL, NULL,
                        2.f, 5.f, 2.f,
                        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  properties[PROP_DITHER] =
    g_param_spec_boolean ("dither", NULL, NULL,
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  properties[PROP_UNMAP_RELEASE_DELAY] =
    g_param_spec_uint ("unmap-release-delay", NULL, NULL,
                       0, 60000, 2000,
                       G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_EXPLICIT_NOTIFY);

  properties[PROP_CACHE_BACKGROUND] =
    g_param_spec_boolean ("cache-background", NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
gb_blur_effect_init (GbBlurEffect *self)
{
  self->mode = GB_BLUR_MODE_ACTOR;
  self->radius = 0;
  self->background_dirty = TRUE;
  self->brightness = 1.f;
  self->corner_radius = 0.f;
  self->corner_exponent = 2.f;
  self->dither = TRUE;
  self->unmap_release_delay_ms = 2000;

  self->actor_fb.pipeline = create_base_pipeline ();
  self->background_fb.pipeline = create_base_pipeline ();
  self->brightness_fb.pipeline = create_brightness_pipeline ();
  self->mask_fb.pipeline = create_mask_pipeline ();
  self->brightness_uniform =
    cogl_pipeline_get_uniform_location (self->brightness_fb.pipeline, "brightness");
  self->corner_radius_uniform =
    cogl_pipeline_get_uniform_location (self->mask_fb.pipeline, "u_corner_radius");
  self->mask_size_uniform =
    cogl_pipeline_get_uniform_location (self->mask_fb.pipeline, "u_size");
  self->corner_exponent_uniform =
    cogl_pipeline_get_uniform_location (self->mask_fb.pipeline, "u_corner_exponent");

  self->kawase_final_pipeline = create_kawase_final_pipeline ();
  self->kf_brightness_uniform =
    cogl_pipeline_get_uniform_location (self->kawase_final_pipeline, "brightness");
  self->kf_corner_radius_uniform =
    cogl_pipeline_get_uniform_location (self->kawase_final_pipeline, "u_corner_radius");
  self->kf_size_uniform =
    cogl_pipeline_get_uniform_location (self->kawase_final_pipeline, "u_size");
  self->kf_corner_exponent_uniform =
    cogl_pipeline_get_uniform_location (self->kawase_final_pipeline, "u_corner_exponent");
  self->kf_dither_uniform =
    cogl_pipeline_get_uniform_location (self->kawase_final_pipeline, "u_dither");
  self->kf_brightness = self->kf_corner_radius = self->kf_corner_exponent = -1.f;
  self->kf_width = self->kf_height = -1.f;
  self->kf_dither = -1;
  self->last_brightness = self->last_corner_radius = self->last_corner_exponent = -1.f;
  self->last_mask_width = self->last_mask_height = -1.f;
  self->last_paint_opacity = -1;
}

/**
 * gb_blur_effect_invalidate:
 * @self: a #GbBlurEffect
 *
 * Tells a background blur with #GbBlurEffect:cache-background set that what is
 * behind its actor changed, and queues a full repaint of the actor so the next
 * capture sees a freshly composited backdrop.
 */
float
gb_blur_effect_get_corner_exponent (GbBlurEffect *self)
{
  g_return_val_if_fail (GB_IS_BLUR_EFFECT (self), 2.f);

  return self->corner_exponent;
}

void
gb_blur_effect_set_corner_exponent (GbBlurEffect *self,
                                    float          corner_exponent)
{
  g_return_if_fail (GB_IS_BLUR_EFFECT (self));

  corner_exponent = CLAMP (corner_exponent, 2.f, 5.f);
  if (G_APPROX_VALUE (self->corner_exponent, corner_exponent, FLT_EPSILON))
    return;

  self->corner_exponent = corner_exponent;

  if (self->actor)
    clutter_effect_queue_repaint (CLUTTER_EFFECT (self));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_EXPONENT]);
}

gboolean
gb_blur_effect_get_dither (GbBlurEffect *self)
{
  g_return_val_if_fail (GB_IS_BLUR_EFFECT (self), TRUE);

  return self->dither;
}

void
gb_blur_effect_set_dither (GbBlurEffect *self,
                           gboolean      dither)
{
  g_return_if_fail (GB_IS_BLUR_EFFECT (self));

  dither = !!dither;
  if (self->dither == dither)
    return;

  self->dither = dither;

  if (self->actor)
    clutter_effect_queue_repaint (CLUTTER_EFFECT (self));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DITHER]);
}

guint
gb_blur_effect_get_unmap_release_delay (GbBlurEffect *self)
{
  g_return_val_if_fail (GB_IS_BLUR_EFFECT (self), 0);

  return self->unmap_release_delay_ms;
}

void
gb_blur_effect_set_unmap_release_delay (GbBlurEffect *self,
                                        guint         delay_ms)
{
  g_return_if_fail (GB_IS_BLUR_EFFECT (self));

  delay_ms = MIN (delay_ms, 60000);
  if (self->unmap_release_delay_ms == delay_ms)
    return;

  self->unmap_release_delay_ms = delay_ms;

  if (self->actor && !clutter_actor_is_mapped (self->actor))
    {
      if (delay_ms == 0)
        cancel_unmap_release (self);
      else
        schedule_unmap_release (self);
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_UNMAP_RELEASE_DELAY]);
}

void
gb_blur_effect_release_targets (GbBlurEffect *self)
{
  g_return_if_fail (GB_IS_BLUR_EFFECT (self));

  cancel_unmap_release (self);
  release_framebuffers (self);
}

void
gb_blur_effect_invalidate (GbBlurEffect *self)
{
  g_return_if_fail (GB_IS_BLUR_EFFECT (self));

  /* A pending capture already has its full repaint queued: invalidations
   * between frames (several damaged windows below) collapse into one. */
  if (self->background_dirty && self->cache_flags & BLUR_APPLIED)
    return;

  self->background_dirty = TRUE;
  if (self->actor)
    clutter_effect_queue_repaint (CLUTTER_EFFECT (self));
}

GbBlurEffect *
gb_blur_effect_new (void)
{
  return g_object_new (GB_TYPE_BLUR_EFFECT, NULL);
}

int
gb_blur_effect_get_radius (GbBlurEffect *self)
{
  g_return_val_if_fail (GB_IS_BLUR_EFFECT (self), 0);

  return self->radius;
}

void
gb_blur_effect_set_radius (GbBlurEffect *self,
                              int           radius)
{
  g_return_if_fail (GB_IS_BLUR_EFFECT (self));

  if (self->radius == radius)
    return;

  self->radius = radius;
  self->cache_flags &= ~BLUR_APPLIED;

  if (self->actor)
    clutter_effect_queue_repaint (CLUTTER_EFFECT (self));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_RADIUS]);
}

float
gb_blur_effect_get_brightness (GbBlurEffect *self)
{
  g_return_val_if_fail (GB_IS_BLUR_EFFECT (self), -1);

  return self->brightness;
}

void
gb_blur_effect_set_brightness (GbBlurEffect *self,
                                  float            brightness)
{
  g_return_if_fail (GB_IS_BLUR_EFFECT (self));

  if (self->brightness == brightness)
    return;

  self->brightness = brightness;
  self->cache_flags &= ~BLUR_APPLIED;

  if (self->actor)
    clutter_effect_queue_repaint (CLUTTER_EFFECT (self));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BRIGHTNESS]);
}

GbBlurMode
gb_blur_effect_get_mode (GbBlurEffect *self)
{
  g_return_val_if_fail (GB_IS_BLUR_EFFECT (self), GB_BLUR_MODE_ACTOR);

  return self->mode;
}

void
gb_blur_effect_set_mode (GbBlurEffect *self,
                            GbBlurMode    mode)
{
  g_return_if_fail (GB_IS_BLUR_EFFECT (self));

  if (self->mode == mode)
    return;

  self->mode = mode;
  self->cache_flags = 0;

  /* The two modes size their targets differently and share the size fields,
   * so drop every target and let the next paint allocate for the new mode. */
  clear_framebuffer_data (&self->actor_fb);
  clear_framebuffer_data (&self->brightness_fb);
  clear_framebuffer_data (&self->mask_fb);
  self->tex_width = 0;
  self->tex_height = 0;
  self->background_dirty = TRUE;

  switch (mode)
    {
    case GB_BLUR_MODE_ACTOR:
      clear_framebuffer_data (&self->background_fb);
      clear_kawase (self);
      break;

    case GB_BLUR_MODE_BACKGROUND:
    default:
      /* Do nothing */
      break;
    }

  if (self->actor)
    clutter_effect_queue_repaint (CLUTTER_EFFECT (self));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MODE]);
}

float
gb_blur_effect_get_corner_radius (GbBlurEffect *self)
{
  g_return_val_if_fail (GB_IS_BLUR_EFFECT (self), 0.f);
  return self->corner_radius;
}

void
gb_blur_effect_set_corner_radius (GbBlurEffect *self,
                                     float          corner_radius)
{
  g_return_if_fail (GB_IS_BLUR_EFFECT (self));

  if (self->corner_radius == corner_radius)
    return;

  self->corner_radius = MAX (0.f, corner_radius);
  self->cache_flags &= ~BLUR_APPLIED;

  if (self->actor)
    clutter_effect_queue_repaint (CLUTTER_EFFECT (self));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_CORNER_RADIUS]);
}
