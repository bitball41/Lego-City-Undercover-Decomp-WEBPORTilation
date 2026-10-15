(function () {
  const DEFAULT_REPO = "bitball41/Lego-City-Undercover-Decomp-WEBPORTilation";
  const DEFAULT_REF = "cursor/web-port-runtime-c6b2";
  const started = performance.now();
  const lines = [];

  function stamp() {
    return ((performance.now() - started) / 1000).toFixed(3).padStart(7, " ");
  }

  function paint() {
    const log = document.getElementById("log");
    if (log) log.textContent = lines.join("\n");
  }

  function log(level, message) {
    const line = stamp() + "  " + level + "  " + message;
    lines.push(line);
    if (level === "FAIL") console.error(line);
    else console.log(line);
    paint();
    return line;
  }

  function describe(err) {
    if (!err) return "unknown error";
    const parts = [err.name ? err.name + ": " + err.message : String(err.message || err)];
    if (err.stack) parts.push(err.stack);
    return parts.join("\n");
  }

  window.addEventListener("error", function (event) {
    log("FAIL", "window error " + (event.filename || "") + ":" + (event.lineno || "?") + " " + describe(event.error || event.message));
  });
  window.addEventListener("unhandledrejection", function (event) {
    log("FAIL", "unhandled rejection " + describe(event.reason));
  });

  function githubFileUrl(repo, ref, file) {
    const params = new URLSearchParams();
    params.set("ref", ref || DEFAULT_REF);
    return "https://api.github.com/repos/" + (repo || DEFAULT_REPO) + "/contents/web/cdn/" + file + "?" + params.toString();
  }

  function setLabel(text) {
    const label = document.getElementById("loadlabel");
    if (label) label.textContent = text;
  }

  function setFill(ratio) {
    const fill = document.getElementById("loadfill");
    if (fill) fill.style.width = Math.max(0, Math.min(100, ratio * 100)) + "%";
  }

  function rateLine(response) {
    const left = response.headers.get("x-ratelimit-remaining");
    const limit = response.headers.get("x-ratelimit-limit");
    const reset = response.headers.get("x-ratelimit-reset");
    const requestId = response.headers.get("x-github-request-id");
    const bits = [];
    if (left !== null) bits.push("rate " + left + "/" + (limit || "?"));
    if (reset) bits.push("reset " + new Date(Number(reset) * 1000).toISOString());
    if (requestId) bits.push("req " + requestId);
    return bits.join(" ");
  }

  async function sha256(bytes) {
    if (!globalThis.crypto || !crypto.subtle) {
      log("WARN", "crypto.subtle missing, hash check skipped");
      return "";
    }
    const digest = await crypto.subtle.digest("SHA-256", bytes);
    return Array.from(new Uint8Array(digest)).map((b) => b.toString(16).padStart(2, "0")).join("");
  }

  function decodeBase64(text, file) {
    try {
      const binary = atob(String(text).replace(/\s/g, ""));
      const out = new Uint8Array(binary.length);
      for (let i = 0; i < binary.length; i += 1) out[i] = binary.charCodeAt(i);
      return out;
    } catch (err) {
      throw new Error("base64 decode failed for " + file + " (" + (text ? text.length : 0) + " chars): " + err.message);
    }
  }

  async function readBody(response) {
    const text = await response.text();
    try {
      return { json: JSON.parse(text), text: text };
    } catch (err) {
      return { json: null, text: text.slice(0, 500) };
    }
  }

  async function fetchBytes(file, opts) {
    const t0 = performance.now();
    if (opts.base) {
      const url = opts.base + file;
      log("GET", url);
      const response = await fetch(url);
      const ms = Math.round(performance.now() - t0);
      if (!response.ok) {
        const body = await readBody(response);
        throw new Error("GET " + response.status + " " + url + " in " + ms + "ms body " + (body.text || ""));
      }
      const bytes = new Uint8Array(await response.arrayBuffer());
      log("OK", file + " " + response.status + " " + bytes.length + "B " + ms + "ms");
      return bytes;
    }
    const url = githubFileUrl(opts.repo, opts.ref, file);
    log("GET", url);
    let response;
    try {
      response = await fetch(url);
    } catch (err) {
      throw new Error("network failure for " + file + ": " + err.message + " url " + url);
    }
    const ms = Math.round(performance.now() - t0);
    const rates = rateLine(response);
    const body = await readBody(response);
    if (!response.ok) {
      const message = body.json && (body.json.message || body.json.error);
      throw new Error(
        "GitHub " + response.status + " " + file + " in " + ms + "ms " + rates +
        (message ? " message " + message : " body " + (body.text || ""))
      );
    }
    const payload = body.json;
    if (!payload || payload.encoding !== "base64" || !payload.content) {
      throw new Error(
        "GitHub payload for " + file + " has no base64 content. keys=" +
        (payload ? Object.keys(payload).join(",") : "none") +
        " message=" + (payload && payload.message ? payload.message : "")
      );
    }
    const bytes = decodeBase64(payload.content, file);
    const declared = payload.size;
    if (typeof declared === "number" && declared !== bytes.length) {
      log("WARN", file + " GitHub size " + declared + " decoded " + bytes.length);
    }
    log("OK", file + " " + response.status + " " + bytes.length + "B " + ms + "ms sha " + (payload.sha || "?").slice(0, 12) + " " + rates);
    if (rates.indexOf("rate 0/") === 0 || rates.indexOf("rate 1/") === 0 || rates.indexOf("rate 2/") === 0) {
      log("WARN", "GitHub rate limit is almost gone. " + rates);
    }
    return bytes;
  }

  async function loadPart(part, opts, onBytes) {
    log("PART", part.name + " chunks " + part.chunks.length + " expect " + part.size + "B sha " + (part.sha256 || "none").slice(0, 16));
    const pieces = new Array(part.chunks.length);
    let cursor = 0;
    async function worker() {
      while (cursor < part.chunks.length) {
        const index = cursor;
        cursor += 1;
        try {
          const bytes = await fetchBytes(part.chunks[index], opts);
          pieces[index] = bytes;
          onBytes(bytes.length, part.name, index, part.chunks.length);
        } catch (err) {
          throw new Error(part.name + " chunk " + index + " (" + part.chunks[index] + ") failed: " + err.message);
        }
      }
    }
    const workers = Math.min(4, part.chunks.length);
    await Promise.all(Array.from({ length: workers }, worker));
    const out = new Uint8Array(part.size);
    let offset = 0;
    for (let i = 0; i < pieces.length; i += 1) {
      if (!pieces[i]) throw new Error("missing " + part.name + " chunk " + i + " " + part.chunks[i]);
      if (offset + pieces[i].length > out.length) {
        throw new Error(part.name + " overflow at chunk " + i + " offset " + offset + " plus " + pieces[i].length + " > " + out.length);
      }
      out.set(pieces[i], offset);
      offset += pieces[i].length;
    }
    if (offset !== part.size) {
      throw new Error(part.name + " assembled " + offset + " of " + part.size);
    }
    const got = await sha256(out);
    if (part.sha256) {
      if (!got) {
        log("WARN", part.name + " hash not checked");
      } else if (got !== part.sha256) {
        throw new Error(part.name + " hash mismatch\n expected " + part.sha256 + "\n got      " + got);
      } else {
        log("OK", part.name + " hash matches " + got);
      }
    }
    return out;
  }

  function runRuntime(bytes) {
    log("STEP", "eval runtime " + bytes.length + "B");
    const blob = new Blob([bytes], { type: "text/javascript" });
    const url = URL.createObjectURL(blob);
    return new Promise((resolve, reject) => {
      const script = document.createElement("script");
      script.src = url;
      script.onload = () => {
        URL.revokeObjectURL(url);
        if (typeof createLcuModule !== "function") {
          reject(new Error("runtime loaded but createLcuModule is missing. first bytes " + new TextDecoder().decode(bytes.slice(0, 80))));
          return;
        }
        log("OK", "createLcuModule is defined");
        resolve(createLcuModule);
      };
      script.onerror = () => {
        URL.revokeObjectURL(url);
        reject(new Error("runtime script failed to execute"));
      };
      document.head.appendChild(script);
    });
  }

  async function boot(options) {
    const opts = options || {};
    if (opts.base) opts.base = opts.base.replace(/\/?$/, "/");
    log("STEP", opts.base ? "local base " + opts.base : "GitHub " + (opts.repo || DEFAULT_REPO) + " ref " + (opts.ref || DEFAULT_REF));
    log("STEP", "page " + location.href + " ua " + navigator.userAgent);
    setLabel(opts.base ? "manifest" : "github manifest");
    setFill(0);
    const manifestBytes = await fetchBytes("lcu-chunks.json", opts);
    let manifest;
    try {
      manifest = JSON.parse(new TextDecoder().decode(manifestBytes));
    } catch (err) {
      throw new Error("manifest JSON parse failed: " + err.message + " head " + new TextDecoder().decode(manifestBytes.slice(0, 120)));
    }
    const parts = manifest.parts || [];
    if (!parts.length) throw new Error("manifest has no parts. keys " + Object.keys(manifest).join(","));
    const total = parts.reduce((sum, part) => sum + part.size, 0);
    log("STEP", "manifest chunkBytes " + manifest.chunkBytes + " parts " + parts.map((part) => part.name + ":" + part.chunks.length).join(" ") + " total " + total);
    let got = 0;
    const loaded = {};
    await Promise.all(parts.map(async (part) => {
      loaded[part.name] = await loadPart(part, opts, (n, name, index, count) => {
        got += n;
        setFill(total ? got / total : 1);
        setLabel(name + " " + (index + 1) + "/" + count + "  " + got + "/" + total);
        if (opts.onProgress) opts.onProgress(got, total, name);
      });
    }));
    if (!loaded.runtime || !loaded.wasm) {
      throw new Error("manifest missing runtime or wasm. have " + Object.keys(loaded).join(","));
    }
    setLabel("starting");
    const create = await runRuntime(loaded.runtime);
    const wasmBytes = loaded.wasm;
    log("STEP", "instantiate wasm " + wasmBytes.length + "B magic " + Array.from(wasmBytes.slice(0, 4)).map((b) => b.toString(16).padStart(2, "0")).join(" "));
    if (wasmBytes[0] !== 0x00 || wasmBytes[1] !== 0x61 || wasmBytes[2] !== 0x73 || wasmBytes[3] !== 0x6d) {
      throw new Error("wasm magic is not \\0asm. first 8 bytes " + Array.from(wasmBytes.slice(0, 8)).join(","));
    }
    let instantiateError = null;
    const module = await create({
      instantiateWasm: function (imports, receiveInstance) {
        const importNames = Object.keys(imports).map((key) => key + "[" + Object.keys(imports[key] || {}).length + "]").join(" ");
        log("STEP", "wasm imports " + importNames);
        WebAssembly.instantiate(wasmBytes, imports).then(function (result) {
          const exports = Object.keys(result.instance.exports);
          log("OK", "wasm instance exports " + exports.length + " " + exports.slice(0, 12).join(","));
          receiveInstance(result.instance);
        }).catch(function (err) {
          instantiateError = err;
          log("FAIL", "wasm instantiate " + describe(err));
        });
        return {};
      }
    });
    if (instantiateError) throw instantiateError;
    log("OK", "module ready exports " + ["_host_boot", "_host_frame", "_host_framebuffer"].filter((name) => typeof module[name] === "function").join(" "));
    const bar = document.getElementById("loadbar");
    if (bar) bar.style.display = "none";
    setLabel("loaded");
    return module;
  }

  window.LcuLoader = {
    boot: boot,
    log: log,
    lines: lines,
    githubFileUrl: githubFileUrl,
    repo: DEFAULT_REPO,
    ref: DEFAULT_REF
  };
})();
