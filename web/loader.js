(function () {
  const DEFAULT_REPO = "bitball41/Lego-City-Undercover-Decomp-WEBPORTilation";
  const DEFAULT_REF = "cursor/web-port-runtime-c6b2";

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

  async function sha256(bytes) {
    if (!globalThis.crypto || !crypto.subtle) return "";
    const digest = await crypto.subtle.digest("SHA-256", bytes);
    return Array.from(new Uint8Array(digest)).map((b) => b.toString(16).padStart(2, "0")).join("");
  }

  function decodeBase64(text) {
    const binary = atob(text.replace(/\s/g, ""));
    const out = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i += 1) out[i] = binary.charCodeAt(i);
    return out;
  }

  async function fetchBytes(file, opts) {
    if (opts.base) {
      const response = await fetch(opts.base + file);
      if (!response.ok) throw new Error(response.status + " " + opts.base + file);
      return new Uint8Array(await response.arrayBuffer());
    }
    const url = githubFileUrl(opts.repo, opts.ref, file);
    const response = await fetch(url);
    if (!response.ok) throw new Error(response.status + " " + url);
    const payload = await response.json();
    if (payload.encoding !== "base64" || !payload.content) {
      throw new Error(payload.message || "GitHub returned no file content for " + file);
    }
    return decodeBase64(payload.content);
  }

  async function loadPart(part, opts, onBytes) {
    const pieces = new Array(part.chunks.length);
    let cursor = 0;
    async function worker() {
      while (cursor < part.chunks.length) {
        const index = cursor;
        cursor += 1;
        const bytes = await fetchBytes(part.chunks[index], opts);
        pieces[index] = bytes;
        onBytes(bytes.length, part.name, index, part.chunks.length);
      }
    }
    const workers = Math.min(4, part.chunks.length);
    await Promise.all(Array.from({ length: workers }, worker));
    const out = new Uint8Array(part.size);
    let offset = 0;
    for (let i = 0; i < pieces.length; i += 1) {
      if (!pieces[i]) throw new Error("missing " + part.name + " chunk " + i);
      out.set(pieces[i], offset);
      offset += pieces[i].length;
    }
    if (offset !== part.size) {
      throw new Error(part.name + " assembled " + offset + " of " + part.size);
    }
    if (part.sha256) {
      const got = await sha256(out);
      if (got && got !== part.sha256) {
        throw new Error(part.name + " hash mismatch");
      }
    }
    return out;
  }

  function runRuntime(bytes) {
    const blob = new Blob([bytes], { type: "text/javascript" });
    const url = URL.createObjectURL(blob);
    return new Promise((resolve, reject) => {
      const script = document.createElement("script");
      script.src = url;
      script.onload = () => {
        URL.revokeObjectURL(url);
        if (typeof createLcuModule !== "function") {
          reject(new Error("runtime loaded but createLcuModule is missing"));
          return;
        }
        resolve(createLcuModule);
      };
      script.onerror = () => {
        URL.revokeObjectURL(url);
        reject(new Error("runtime script failed"));
      };
      document.head.appendChild(script);
    });
  }

  async function boot(options) {
    const opts = options || {};
    if (opts.base) opts.base = opts.base.replace(/\/?$/, "/");
    setLabel(opts.base ? "manifest" : "github manifest");
    setFill(0);
    const manifestBytes = await fetchBytes("lcu-chunks.json", opts);
    const manifest = JSON.parse(new TextDecoder().decode(manifestBytes));
    const parts = manifest.parts || [];
    const total = parts.reduce((sum, part) => sum + part.size, 0);
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
    setLabel("starting");
    const create = await runRuntime(loaded.runtime);
    const wasmBytes = loaded.wasm;
    const module = await create({
      instantiateWasm: function (imports, receiveInstance) {
        WebAssembly.instantiate(wasmBytes, imports).then(function (result) {
          receiveInstance(result.instance);
        }).catch(function (err) {
          const log = document.getElementById("log");
          if (log) log.textContent = String(err);
        });
        return {};
      }
    });
    const bar = document.getElementById("loadbar");
    if (bar) bar.style.display = "none";
    setLabel("");
    return module;
  }

  window.LcuLoader = {
    boot: boot,
    githubFileUrl: githubFileUrl,
    repo: DEFAULT_REPO,
    ref: DEFAULT_REF
  };
})();
