// Run against the INSTALLED package: gjs finds the typelib and the shared
// library, resolves the type and its properties. Constructing the effect needs
// a live compositor, so this stops at the type.
//   GI_TYPELIB_PATH=/usr/lib/x86_64-linux-gnu/mutter-18 \
//   LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/mutter-18 gjs tests/smoke.js
imports.gi.versions.Blur = '1.0';
const GObject = imports.gi.GObject;
const Blur = imports.gi.Blur;

const pspec = GObject.Object.find_property.call(Blur.BlurEffect, 'corner-radius');
if (Blur.BlurEffect.$gtype.name !== 'GbBlurEffect' || !pspec)
    throw new Error('Blur.BlurEffect or its corner-radius property is missing');
print(`ok: ${Blur.BlurEffect.$gtype.name} has ${pspec.name}`);

const cache = GObject.Object.find_property.call(Blur.BlurEffect, 'cache-background');
if (!cache || cache.default_value !== false ||
    typeof Blur.BlurEffect.prototype.invalidate !== 'function')
    throw new Error('Expected opt-in background caching and invalidate()');
print('ok: background caching is opt-in');

const exp = GObject.Object.find_property.call(Blur.BlurEffect, 'corner-exponent');
if (!exp || Math.abs(exp.default_value - 2.0) > 1e-4)
    throw new Error('Expected corner-exponent property defaulting to 2.0');
print(`ok: corner-exponent defaults to ${exp.default_value}`);

const dither = GObject.Object.find_property.call(Blur.BlurEffect, 'dither');
if (!dither || dither.default_value !== true)
    throw new Error('Expected dither property defaulting to true');
print('ok: dither defaults to true');

const delay = GObject.Object.find_property.call(Blur.BlurEffect, 'unmap-release-delay');
if (!delay || delay.default_value !== 2000 ||
    typeof Blur.BlurEffect.prototype.release_targets !== 'function')
    throw new Error('Expected unmap-release-delay (2000 ms) and release_targets()');
print(`ok: unmap-release-delay defaults to ${delay.default_value} ms`);
