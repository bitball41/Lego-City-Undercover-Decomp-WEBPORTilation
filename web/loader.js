(function () {
  const DEFAULT_REPO = "bitball41/Lego-City-Undercover-Decomp-WEBPORTilation";
  const DEFAULT_REF = "cursor/web-port-runtime-c6b2";
  const loaderSrc = document.currentScript && document.currentScript.src;

  function jsDelivrDist(repo, ref) {
    return "https://cdn.jsdelivr.net/gh/" + (repo || DEFAULT_REPO) + "@" + (ref || DEFAULT_REF) + "/web/cdn/";
  }

  function defaultBase() {
    const query = new URLSearchParams(location.search);
    if (query.get("dist")) {
      return query.get("dist").replace(/\/?$/, "/");
    }
    if (loaderSrc) {
      return new URL("cdn/", loaderSrc).href;
    }
    return new URL("cdn/", location.href).href;
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

  async function fetchChunk(url) {
    const response = await fetch(url, { cache: "force-cache" });
    if (!response.ok) {
      throw new Error(response.status + " " + url);
    }
    return new Uint8Array(await response.arrayBuffer());
  }

  async function loadPart(part, base, onBytes) {
    const pieces = new Array(part.chunks.length);
    let cursor = 0;
    async function worker() {
      while (cursor < part.chunks.length) {
        const index = cursor;
        cursor += 1;
        const bytes = await fetchChunk(base + part.chunks[index]);
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
    const base = (opts.base || defaultBase()).replace(/\/?$/, "/");
    setLabel("manifest");
    setFill(0);
    const manifestResponse = await fetch(base + "lcu-chunks.json", { cache: "no-cache" });
    if (!manifestResponse.ok) {
      throw new Error("manifest " + manifestResponse.status + " " + base + "lcu-chunks.json");
    }
    const manifest = await manifestResponse.json();
    const parts = manifest.parts || [];
    const total = parts.reduce((sum, part) => sum + part.size, 0);
    let got = 0;
    const loaded = {};
    await Promise.all(parts.map(async (part) => {
      loaded[part.name] = await loadPart(part, base, (n, name, index, count) => {
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
    defaultBase: defaultBase,
    jsDelivrDist: jsDelivrDist,
    repo: DEFAULT_REPO,
    ref: DEFAULT_REF
  };
})();
