// Ordered, tile-binned isometric compositing. The CPU submits geometry and
// immutable assets; pixel coverage, palette lookup, lighting and lines run here.
struct Command {
    rect: vec4<i32>,
    clip: vec4<i32>,
    data: vec4<u32>,
    tint: vec4<u32>,
    line: vec4<i32>,
    extra: vec4<u32>,
}
struct Params {
    source: vec4<f32>,
    frame_size: vec4<u32>,
}
@group(0) @binding(0) var<storage, read> assets: array<u32>;
@group(0) @binding(1) var<storage, read> commands: array<Command>;
@group(0) @binding(2) var<storage, read> bins: array<u32>;
@group(0) @binding(3) var<storage, read_write> output: array<u32>;
var<immediate> params: Params;

fn line_covers(p: vec2<i32>, line: vec4<i32>, period: i32) -> bool {
    var a = line.xy;
    var b = line.zw;
    let xmajor = abs(b.x - a.x) >= abs(b.y - a.y);
    if ((xmajor && a.x > b.x) || (!xmajor && a.y > b.y)) {
        let old = a; a = b; b = old;
    }
    if (all(p == a)) { return true; }
    let extent = abs(b - a);
    let major = select(extent.y, extent.x, xmajor);
    let minor = select(extent.x, extent.y, xmajor);
    let t = select(p.y - a.y, p.x - a.x, xmajor);
    if (major == 0 || t < 0 || t >= major || t % max(1, period) != 0) { return false; }
    let rise = (2 * minor * (t + 1) + major) / (2 * major);
    let direction = select(select(-1, 1, b.x >= a.x), select(-1, 1, b.y >= a.y), xmajor);
    return select(p.x == a.x + direction * rise, p.y == a.y + direction * rise, xmajor);
}

@compute @workgroup_size(8, 8)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x >= params.frame_size.x || id.y >= params.frame_size.y) { return; }
    let scene_pixel = vec2<i32>(floor(params.source.xy + vec2<f32>(id.xy) * params.source.zw));
    let screen_pixel = vec2<i32>(id.xy) + vec2<i32>(floor(params.source.xy));
    let bin = (id.y / 32u * params.frame_size.z + id.x / 32u) * 2u;
    let first = bins[bin];
    let count = bins[bin + 1u];
    var color = 0xffffffffu;
    for (var item = 0u; item < count; item++) {
        let cmd = commands[bins[first + item]];
        let p = select(scene_pixel, screen_pixel, cmd.extra.z != 0u);
        if (any(p < cmd.clip.xy) || any(p >= cmd.clip.zw)) { continue; }
        if (cmd.data.x == 2u) {
            if (line_covers(p, cmd.line, i32(cmd.extra.x))) { color = cmd.tint.y | 0xff000000u; }
            continue;
        }
        let local = p - cmd.rect.xy;
        if (any(local < vec2<i32>(0)) || any(local >= cmd.rect.zw)) { continue; }
        let texel = assets[cmd.data.y + u32(local.y + i32(cmd.extra.y)) * cmd.data.z + u32(local.x + i32(cmd.extra.x))];
        if (cmd.data.x == 1u) {
            if ((texel >> 24u) != 0u) { color = texel; }
            continue;
        }
        let index = texel & 255u;
        if (index == 0u || ((cmd.tint.z & 1u) != 0u && (texel & 65536u) == 0u)) { continue; }
        var pixel = assets[cmd.tint.x + index];
        if ((cmd.tint.z & 2u) != 0u && index >= 16u && index <= 31u) {
            var channels = vec3<u32>(cmd.tint.y & 255u, (cmd.tint.y >> 8u) & 255u, (cmd.tint.y >> 16u) & 255u);
            if ((cmd.tint.z & 1u) == 0u) { channels = channels * (31u - index) / 15u; }
            pixel = channels.x | (channels.y << 8u) | (channels.z << 16u);
        }
        if ((cmd.tint.z & 4u) != 0u) {
            let light = 200u + ((texel >> 8u) & 255u) * 300u / 255u;
            let channels = min(vec3<u32>(255u), vec3<u32>(pixel & 255u, (pixel >> 8u) & 255u, (pixel >> 16u) & 255u) * light / 255u);
            pixel = channels.x | (channels.y << 8u) | (channels.z << 16u);
        }
        color = pixel | 0xff000000u;
    }
    if (params.frame_size.w != 0u) {
        color = (color & 0xff00ff00u) | ((color & 255u) << 16u) | ((color >> 16u) & 255u);
    }
    output[id.y * params.frame_size.x + id.x] = color;
}
