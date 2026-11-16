// Direct2D's point-specular lighting (SVG feSpecularLighting with a point light) on the GPU: the
// input's alpha is the height map, its normal comes from Sobel kernels over whole pixels, and the
// output is the light's colour scaled by the shine (alpha: the brightest channel). Coordinates are
// pixels of the effect's evaluation grid (FlutterFragCoord is the drawn rectangle's local space).
#include <flutter/runtime_effect.glsl>

uniform vec4 u_input;    // The input's left, top, width, height in grid pixels.
uniform vec4 u_light;    // The light's x, y, z in grid pixels; w: 1 samples the input upside down.
uniform vec4 u_material; // Surface scale, specular constant, specular exponent.
uniform vec4 u_color;    // The light's colour.
uniform sampler2D u_image;

out vec4 frag_color;

float heightAt(vec2 pixel) {
  vec2 inside = clamp(pixel, u_input.xy, u_input.xy + u_input.zw - 1.0);
  vec2 uv = (inside - u_input.xy + 0.5) / u_input.zw;
  if (u_light.w > 0.5) {
    uv.y = 1.0 - uv.y;
  }
  return texture(u_image, uv).a;
}

void main() {
  vec2 p = floor(FlutterFragCoord());
  float scale = u_material.x;
  float nx = -scale * 0.25 *
             ((heightAt(p + vec2(1, -1)) + 2.0 * heightAt(p + vec2(1, 0)) + heightAt(p + vec2(1, 1))) -
              (heightAt(p + vec2(-1, -1)) + 2.0 * heightAt(p + vec2(-1, 0)) + heightAt(p + vec2(-1, 1))));
  float ny = -scale * 0.25 *
             ((heightAt(p + vec2(-1, 1)) + 2.0 * heightAt(p + vec2(0, 1)) + heightAt(p + vec2(1, 1))) -
              (heightAt(p + vec2(-1, -1)) + 2.0 * heightAt(p + vec2(0, -1)) + heightAt(p + vec2(1, -1))));
  vec3 normal = vec3(nx, ny, 1.0);
  vec3 to_light = vec3(u_light.xy - p, u_light.z - scale * heightAt(p));
  float distance = length(to_light);
  if (distance > 0.0) {
    to_light /= distance;
  }
  vec3 half_vector = to_light + vec3(0.0, 0.0, 1.0);
  float shine = 0.0;
  float half_length = length(half_vector);
  if (half_length > 0.0) {
    float d = max(0.0, dot(normal, half_vector / half_length) / length(normal));
    shine = u_material.y * pow(d, u_material.z);
  }
  vec3 color = clamp(shine * u_color.rgb, 0.0, 1.0);
  frag_color = vec4(color, max(color.r, max(color.g, color.b)));
}
