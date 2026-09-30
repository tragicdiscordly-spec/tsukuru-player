// Micro benchmarks for the JavaScript engine (QuickJS-NG), including a mock of the per-frame work
// a scene graph does in RPG Maker MV/MZ (transform updates for a few thousand sprites).
function timeit(name, fn) {
  const t = Date.now();
  const r = fn();
  print(name + ": " + (Date.now() - t) + " ms");
  return r;
}
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

timeit("fib(29) recursion", () => fib(29));
timeit("object churn 1M", () => {
  let a = [];
  for (let i = 0; i < 1e6; i++) { a.push({ x: i, y: i * 2, z: [i] }); if (a.length > 1000) a.length = 0; }
  return a.length;
});
timeit("string concat 200k", () => { let s = ""; for (let i = 0; i < 2e5; i++) s += i % 10; return s.length; });
timeit("Float32Array 1M sin", () => { const f = new Float32Array(1e6); for (let i = 0; i < f.length; i++) f[i] = Math.sin(i) * 0.5; return f[10]; });
timeit("class method calls 2M", () => {
  class P { constructor() { this.v = 0; } add(n) { this.v += n; return this; } }
  const p = new P();
  for (let i = 0; i < 2e6; i++) p.add(i & 3);
  return p.v;
});

timeit("obfuscated accessor calls 3M", () => {
  const arr = []; for (let i = 0; i < 2000; i++) arr.push("name" + i);
  const d = function (i) { return arr[i - 0x15c]; };
  let n = 0; const o = { name400: 1 };
  for (let i = 0; i < 3e6; i++) { n += o[d(0x15c + 400)] ? 1 : 0; }
  return n;
});
timeit("regex replace/match 200k", () => {
  let n = 0;
  for (let i = 0; i < 2e5; i++) { n += ("value %1 and %2 x" + (i & 15)).replace(/%(\d+)/gi, (m, k) => k).length; n += /and/.test("x and y") ? 1 : 0; }
  return n;
});
timeit("array map/filter/join 3000x1000", () => {
  const base = []; for (let i = 0; i < 1000; i++) base.push(i);
  let n = 0;
  for (let r = 0; r < 3000; r++) n += base.map(x => x * 2).filter(x => x % 3 === 0).length;
  return n;
});
// Mock scene graph: 3000 sprites with 2D affine transforms, updated for 60 frames.
class Sprite {
  constructor(i) { this.x = i % 800; this.y = (i * 7) % 600; this.sx = 1; this.sy = 1; this.rot = 0; this.a = 1; this.b = 0; this.c = 0; this.d = 1; this.tx = 0; this.ty = 0; this.parent = null; this.alpha = 1; this.worldAlpha = 1; }
  update(pa, pb, pc, pd, ptx, pty, palpha) {
    const cos = Math.cos(this.rot), sin = Math.sin(this.rot);
    const la = cos * this.sx, lb = sin * this.sx, lc = -sin * this.sy, ld = cos * this.sy;
    this.a = la * pa + lb * pc; this.b = la * pb + lb * pd;
    this.c = lc * pa + ld * pc; this.d = lc * pb + ld * pd;
    this.tx = this.x * pa + this.y * pc + ptx; this.ty = this.x * pb + this.y * pd + pty;
    this.worldAlpha = this.alpha * palpha;
  }
}
const sprites = []; for (let i = 0; i < 3000; i++) sprites.push(new Sprite(i));
const perFrame = timeit("scene graph 3000 sprites x 60 frames", () => {
  for (let f = 0; f < 60; f++) for (let i = 0; i < sprites.length; i++) { const s = sprites[i]; s.rot += 0.01; s.x += 0.1; s.update(1, 0, 0, 1, 0, 0, 1); }
  return 0;
});
print("(a game frame has ~16.7 ms at 60 fps; the scene graph work above is per 60 frames)");
