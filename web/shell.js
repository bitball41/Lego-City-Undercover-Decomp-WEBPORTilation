(function () {
  const BTN_A = 1 << 0;
  const BTN_ZL = 1 << 6;
  const BTN_PLUS = 1 << 8;
  const BTN_UP = 1 << 10;
  const BTN_DOWN = 1 << 11;
  const BTN_LEFT = 1 << 12;
  const BTN_RIGHT = 1 << 13;

  function attach(module) {
    const canvas = document.getElementById("screen");
    const ctx = canvas.getContext("2d");
    const statusEl = document.getElementById("status");
    const logEl = document.getElementById("log");
    const keys = new Set();
    let lookX = 0;
    let lookY = 0;
    let image = null;

    window.addEventListener("keydown", (ev) => {
      keys.add(ev.code);
      if (ev.code === "Space" || ev.code === "ArrowUp" || ev.code === "ArrowDown") {
        ev.preventDefault();
      }
    });
    window.addEventListener("keyup", (ev) => keys.delete(ev.code));
    canvas.addEventListener("mousemove", (ev) => {
      if (ev.buttons & 1) {
        lookX += ev.movementX * 0.004;
        lookY += ev.movementY * 0.004;
      }
    });

    function axis(neg, pos) {
      return (keys.has(pos) ? 1 : 0) - (keys.has(neg) ? 1 : 0);
    }

    function samplePad() {
      const pads = navigator.getGamepads ? navigator.getGamepads() : [];
      const gp = pads && pads[0];
      let lx = axis("KeyA", "KeyD") + axis("ArrowLeft", "ArrowRight");
      let ly = axis("KeyS", "KeyW") + axis("ArrowDown", "ArrowUp");
      let rx = Math.max(-1, Math.min(1, lookX));
      let ry = Math.max(-1, Math.min(1, lookY));
      let buttons = 0;
      if (keys.has("Space") || keys.has("KeyJ")) buttons |= BTN_A;
      if (keys.has("ShiftLeft") || keys.has("ShiftRight")) buttons |= BTN_ZL;
      if (keys.has("Enter") || keys.has("Escape")) buttons |= BTN_PLUS;
      if (keys.has("ArrowUp")) buttons |= BTN_UP;
      if (keys.has("ArrowDown")) buttons |= BTN_DOWN;
      if (keys.has("ArrowLeft")) buttons |= BTN_LEFT;
      if (keys.has("ArrowRight")) buttons |= BTN_RIGHT;
      lookX *= 0.8;
      lookY *= 0.8;
      if (gp) {
        lx = gp.axes[0] || lx;
        ly = -(gp.axes[1] || 0) || ly;
        rx = gp.axes[2] || rx;
        ry = gp.axes[3] || ry;
        if (gp.buttons[0] && gp.buttons[0].pressed) buttons |= BTN_A;
        if (gp.buttons[6] && gp.buttons[6].pressed) buttons |= BTN_ZL;
        if (gp.buttons[9] && gp.buttons[9].pressed) buttons |= BTN_PLUS;
      }
      return { buttons, lx, ly, rx, ry };
    }

    function present() {
      const w = module._host_fb_width();
      const h = module._host_fb_height();
      const ptr = module._host_framebuffer();
      if (!ptr || !w || !h) return;
      if (!image || image.width !== w || image.height !== h) {
        canvas.width = w;
        canvas.height = h;
        image = ctx.createImageData(w, h);
      }
      image.data.set(module.HEAPU8.subarray(ptr, ptr + w * h * 4));
      ctx.putImageData(image, 0, 0);
    }

    let last = performance.now();
    let smokeFrames = 0;
    const smoke = new URLSearchParams(location.search).has("smoke");

    function note(message) {
      if (window.LcuLoader && LcuLoader.log) LcuLoader.log("GAME", message);
      else if (logEl) logEl.textContent += (logEl.textContent ? "\n" : "") + message;
    }

    function frame(now) {
      const dt = Math.min(0.05, Math.max(0, (now - last) / 1000));
      last = now;
      try {
        const pad = samplePad();
        if (smoke) {
          module._host_set_pad(0, 0, 1, 0, 0);
        } else {
          module._host_set_pad(pad.buttons, pad.lx, pad.ly, pad.rx, pad.ry);
        }
        module._host_frame(dt);
        present();
      } catch (err) {
        note("frame failed " + (err && err.stack ? err.stack : err));
        if (statusEl) statusEl.textContent = "frame failed";
        document.title = "LCU_FAIL";
        return;
      }
      const text = module.UTF8ToString(module._host_get_status());
      statusEl.textContent = text + "  models " + module._host_stud_count();
      if (smoke) {
        smokeFrames += 1;
        if (smokeFrames > 20) {
          document.title = text.indexOf("DROP") >= 0 || text.indexOf("MESH") >= 0 ? "LCU_OK" : "LCU_FAIL";
          return;
        }
      }
      requestAnimationFrame(frame);
    }

    function submitBytes(name, bytes) {
      const nameLen = module.lengthBytesUTF8(name) + 1;
      const np = module._malloc(nameLen);
      const bp = module._malloc(bytes.length);
      module.stringToUTF8(name, np, nameLen);
      module.HEAPU8.set(bytes, bp);
      const accepted = module._host_submit_asset(np, bp, bytes.length);
      module._free(np);
      module._free(bp);
      return accepted;
    }

    async function streamFiles(fileList) {
      const queued = [];
      for (const file of fileList) {
        if (/\.(obj|ghg|gsc)$/i.test(file.name)) {
          queued.push({ file: file, name: file.name });
          continue;
        }
        if (!/\.(dat|pak)$/i.test(file.name)) continue;
        if (file.size <= 48 * 1024 * 1024) {
          queued.push({ file: file, name: file.name, whole: true });
          continue;
        }
        let listing = null;
        try {
          listing = await LcuArchive.listFile(file);
        } catch (err) {
          note("archive failed " + file.name + " " + err);
          continue;
        }
        const models = LcuArchive.models(listing);
        note("archive " + file.name + " " + (listing && listing.kind ? listing.kind : "unknown") + " models " + models.length);
        models.sort(function (a, b) {
          const ag = /\.gsc$/i.test(a.name) ? 0 : 1;
          const bg = /\.gsc$/i.test(b.name) ? 0 : 1;
          return ag - bg;
        });
        for (const entry of models) {
          queued.push({ file: file, name: entry.name, offset: entry.offset, length: entry.length });
        }
      }
      let ok = 0;
      const cap = Math.min(queued.length, 80);
      for (let i = 0; i < cap; i++) {
        const item = queued[i];
        let bytes;
        if (item.offset != null) {
          bytes = new Uint8Array(await item.file.slice(item.offset, item.offset + item.length).arrayBuffer());
        } else {
          bytes = new Uint8Array(await item.file.arrayBuffer());
        }
        if (!bytes.length || bytes.length > 48 * 1024 * 1024) {
          note("skip " + item.name + " " + bytes.length + "B");
          continue;
        }
        ok += submitBytes(item.name, bytes);
        note("asset " + item.name + " " + bytes.length + "B accepted " + ok);
        await new Promise((resolve) => requestAnimationFrame(resolve));
      }
      if (queued.length > cap) note("stopped after " + cap + " of " + queued.length);
      note("assets finished accepted " + ok);
    }

    const folder = document.getElementById("folder");
    const clear = document.getElementById("clear");
    if (folder) {
      folder.addEventListener("change", (ev) => streamFiles(ev.target.files));
    }
    if (clear) {
      clear.addEventListener("click", () => module._host_clear_assets());
    }

    const lang = navigator.language || "en-GB";
    const langLen = module.lengthBytesUTF8(lang) + 1;
    const lp = module._malloc(langLen);
    module.stringToUTF8(lang, lp, langLen);
    module._host_set_language(lp);
    module._free(lp);
    try {
      module._host_boot();
      note("host_boot ok " + module._host_fb_width() + "x" + module._host_fb_height() + " status " + module.UTF8ToString(module._host_get_status()));
    } catch (err) {
      note("host_boot failed " + (err && err.stack ? err.stack : err));
      if (statusEl) statusEl.textContent = "boot failed";
      document.title = "LCU_FAIL";
      return;
    }
    requestAnimationFrame(frame);
  }

  function start(modulePromise) {
    Promise.resolve(modulePromise).then(attach).catch((err) => {
      const statusEl = document.getElementById("status");
      const detail = err && err.stack ? err.stack : String(err);
      if (window.LcuLoader && LcuLoader.log) LcuLoader.log("FAIL", "shell start " + detail);
      else {
        const logEl = document.getElementById("log");
        if (logEl) logEl.textContent = detail;
      }
      if (statusEl) statusEl.textContent = "boot failed";
      document.title = "LCU_FAIL";
    });
  }

  window.LcuShell = { start: start };
})();
