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

    function frame(now) {
      const dt = Math.min(0.05, Math.max(0, (now - last) / 1000));
      last = now;
      const pad = samplePad();
      if (smoke) {
        module._host_set_pad(0, 0, 1, 0, 0);
      } else {
        module._host_set_pad(pad.buttons, pad.lx, pad.ly, pad.rx, pad.ry);
      }
      module._host_frame(dt);
      present();
      const text = module.UTF8ToString(module._host_get_status());
      statusEl.textContent = text + "  studs " + module._host_stud_count();
      if (smoke) {
        smokeFrames += 1;
        if (smokeFrames > 20) {
          document.title = text.indexOf("STUDS") >= 0 ? "LCU_OK" : "LCU_FAIL";
          return;
        }
      }
      requestAnimationFrame(frame);
    }

    async function streamFiles(fileList) {
      let ok = 0;
      for (const file of fileList) {
        if (!/\.(obj|ghg|gsc)$/i.test(file.name)) continue;
        const bytes = new Uint8Array(await file.arrayBuffer());
        const nameLen = module.lengthBytesUTF8(file.name) + 1;
        const np = module._malloc(nameLen);
        const bp = module._malloc(bytes.length);
        module.stringToUTF8(file.name, np, nameLen);
        module.HEAPU8.set(bytes, bp);
        ok += module._host_submit_asset(np, bp, bytes.length);
        module._free(np);
        module._free(bp);
        logEl.textContent = "streamed " + file.name + " (" + bytes.length + " bytes)";
        await new Promise((resolve) => requestAnimationFrame(resolve));
      }
      logEl.textContent += "\nmeshes accepted " + ok;
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
    module._host_boot();
    requestAnimationFrame(frame);
  }

  function start(modulePromise) {
    Promise.resolve(modulePromise).then(attach).catch((err) => {
      const statusEl = document.getElementById("status");
      const logEl = document.getElementById("log");
      if (statusEl) statusEl.textContent = "boot failed";
      if (logEl) logEl.textContent = String(err && err.stack ? err.stack : err);
    });
  }

  window.LcuShell = { start: start };
})();
