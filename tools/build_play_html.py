#!/usr/bin/env python3
"""Write web/play.html as one file with the loader and shell inlined."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOADER = (ROOT / "web" / "loader.js").read_text()
ARCHIVE = (ROOT / "web" / "archive.js").read_text()
SHELL = (ROOT / "web" / "shell.js").read_text()

PAGE = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>LEGO City Undercover</title>
  <style>
    :root { color-scheme: dark; }
    body {
      margin: 0;
      min-height: 100vh;
      background: #12151c;
      color: #f2efe6;
      font: 16px/1.4 "Trebuchet MS", sans-serif;
    }
    main { max-width: 1040px; margin: 0 auto; padding: 20px; }
    h1 { margin: 0 0 6px; letter-spacing: 0.04em; }
    p { color: #c8c2b4; }
    canvas {
      width: min(100%, 960px);
      height: auto;
      background: #000;
      border: 4px solid #e8b423;
      display: block;
    }
    .row { display: flex; flex-wrap: wrap; gap: 12px; align-items: center; margin: 12px 0; }
    button, label.file {
      background: #e8b423;
      color: #1b1404;
      border: 0;
      padding: 8px 14px;
      font-weight: 700;
      cursor: pointer;
    }
    #status, #loadlabel { font-variant-numeric: tabular-nums; }
    #log {
      white-space: pre-wrap;
      color: #c5d0c0;
      background: #0c0f14;
      border: 1px solid #2c3442;
      font: 12px/1.45 ui-monospace, monospace;
      max-height: 280px;
      overflow: auto;
      padding: 8px;
    }
    #loadbar {
      width: min(100%, 960px);
      height: 10px;
      background: #2a303c;
      margin: 8px 0;
    }
    #loadfill { height: 100%; width: 0; background: #e8b423; }
  </style>
</head>
<body>
<main>
  <h1>LEGO CITY UNDERCOVER</h1>
  <p>Save this file and open it. The runtime and wasm are fetched from the GitHub API in chunks. WASD moves, Shift sprints, Space jumps, mouse looks, Enter pauses. Open a folder from a game you own (<code>.dat</code>, <code>.pak</code>, <code>.ghg</code>, <code>.gsc</code>). This file does not contain the campaign or any game assets. The decomp still has to grow into the real city, missions, and characters.</p>
  <div id="loadbar"><div id="loadfill"></div></div>
  <div id="loadlabel">starting</div>
  <canvas id="screen" width="480" height="270"></canvas>
  <div class="row">
    <label class="file">Open dump folder<input id="folder" type="file" webkitdirectory multiple hidden /></label>
    <button id="clear" type="button">Clear meshes</button>
    <button id="copylog" type="button">Copy log</button>
    <span id="status">loading</span>
  </div>
  <div id="log"></div>
</main>
<script>
__LOADER__
</script>
<script>
__ARCHIVE__
</script>
<script>
__SHELL__
</script>
<script>
  const query = new URLSearchParams(location.search);
  document.getElementById("copylog").addEventListener("click", function () {
    const text = (LcuLoader.lines || []).join("\\n");
    const done = function () { LcuLoader.log("OK", "log copied " + LcuLoader.lines.length + " lines"); };
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(done).catch(function (err) {
        LcuLoader.log("FAIL", "copy failed " + err);
      });
      return;
    }
    LcuLoader.log("FAIL", "clipboard unavailable\\n" + text);
  });
  LcuLoader.boot({
    repo: query.get("repo") || undefined,
    ref: query.get("ref") || undefined,
    base: query.get("dist") || undefined
  }).then(function (module) {
    LcuShell.start(module);
  }).catch(function (err) {
    document.getElementById("status").textContent = "load failed";
    LcuLoader.log("FAIL", err && err.stack ? err.stack : String(err));
    if (query.has("smoke")) document.title = "LCU_FAIL";
  });
</script>
</body>
</html>
"""

def guard(source, name):
    if "</script" in source.lower():
        raise SystemExit(name + " contains a script end tag")
    return source.rstrip() + "\n"

html = PAGE.replace("__LOADER__", guard(LOADER, "loader.js")).replace("__ARCHIVE__", guard(ARCHIVE, "archive.js")).replace("__SHELL__", guard(SHELL, "shell.js"))
out = ROOT / "web" / "play.html"
out.write_text(html)
print(f"wrote {out} ({out.stat().st_size} bytes)")
