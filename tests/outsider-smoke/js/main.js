// Smoke test for the Outsider runtime on the PS5: no RPG Maker files needed. Draws two layers of
// tiles taken from ONE atlas texture (like a tilemap), moving sprites and some text, and steps through
// four scenes (every 180 frames) while printing the frame rate of each:
//   A: 2 layers + 60 moving sprites   B: 2 layers   C: 1 layer   D: nothing but the background
console.log("smoke: start");

var W = 816, H = 624;
var view = document.createElement("canvas");
view.width = W; view.height = H;
var app = new PIXI.Application({ view: view, width: W, height: H, backgroundColor: 0x203060 });
document.body.appendChild(view);

// One 96x96 atlas with four 48x48 tiles.
var atlas = document.createElement("canvas");
atlas.width = 96; atlas.height = 96;
var g = atlas.getContext("2d");
var colors = ["#c04040", "#40a040", "#4060d0", "#c0a020"];
for (var c = 0; c < 4; c++) {
  g.fillStyle = colors[c]; g.fillRect((c % 2) * 48, Math.floor(c / 2) * 48, 48, 48);
  g.fillStyle = "rgba(255,255,255,0.35)"; g.fillRect((c % 2) * 48 + 2, Math.floor(c / 2) * 48 + 2, 44, 44);
}
var base = PIXI.BaseTexture.from(atlas);
var tiles = [];
for (var t = 0; t < 4; t++) {
  tiles.push(new PIXI.Texture(base, new PIXI.Rectangle((t % 2) * 48, Math.floor(t / 2) * 48, 48, 48)));
}

var layers = [new PIXI.Container(), new PIXI.Container()];
var layer, x, y, i;
for (layer = 0; layer < 2; layer++) {
  for (y = 0; y < 13; y++) {
    for (x = 0; x < 17; x++) {
      var tile = new PIXI.Sprite(tiles[(x + y + layer) % 4]);
      tile.x = x * 48; tile.y = y * 48; tile.alpha = layer ? 0.5 : 1;
      layers[layer].addChild(tile);
    }
  }
  app.stage.addChild(layers[layer]);
}
var moverLayer = new PIXI.Container();
var movers = [];
for (i = 0; i < 60; i++) {
  var s = new PIXI.Sprite(tiles[i % 4]);
  s.anchor.set(0.5); movers.push(s); moverLayer.addChild(s);
}
app.stage.addChild(moverLayer);
var label = new PIXI.Text("Outsider on PS5", { fill: 0xffffff, fontSize: 32 });
label.x = 20; label.y = 20; app.stage.addChild(label);

var PHASE = 180;
var names = ["A: 2 layers + 60 movers", "B: 2 layers", "C: 1 layer", "D: empty"];
var frame = 0, t0 = Date.now(), tick = 0, phase = 0;
function setPhase(p) {
  layers[0].visible = p < 3;
  layers[1].visible = p < 2;
  moverLayer.visible = p < 1;
}
setPhase(0);
function loop() {
  frame++; tick += 0.05;
  if (moverLayer.visible) {
    for (var k = 0; k < movers.length; k++) {
      movers[k].x = W / 2 + Math.cos(tick + k * 0.2) * 300;
      movers[k].y = H / 2 + Math.sin(tick * 1.3 + k * 0.2) * 200;
      movers[k].rotation = tick + k;
    }
  }
  if (frame % PHASE === 0) {
    var now = Date.now();
    console.log("smoke: " + names[phase] + ": " + (PHASE * 1000 / (now - t0)).toFixed(1) + " fps");
    t0 = now;
    phase = Math.min(phase + 1, 3);
    setPhase(phase);
  }
  requestAnimationFrame(loop);
}
requestAnimationFrame(loop);
