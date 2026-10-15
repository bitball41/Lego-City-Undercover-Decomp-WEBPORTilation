(function (root, factory) {
  const api = factory();
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  root.LcuArchive = api;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  function u32(bytes, off) {
    return (bytes[off] | (bytes[off + 1] << 8) | (bytes[off + 2] << 16) | (bytes[off + 3] << 24)) >>> 0;
  }

  function u16(bytes, off) {
    return bytes[off] | (bytes[off + 1] << 8);
  }

  function i16(bytes, off) {
    const v = u16(bytes, off);
    return v & 0x8000 ? v - 0x10000 : v;
  }

  function i32(bytes, off) {
    return u32(bytes, off) | 0;
  }

  function be32(bytes, off) {
    return ((bytes[off] << 24) | (bytes[off + 1] << 16) | (bytes[off + 2] << 8) | bytes[off + 3]) >>> 0;
  }

  function be16(bytes, off) {
    return (bytes[off] << 8) | bytes[off + 1];
  }

  function cstr(bytes, off) {
    if (off < 0 || off >= bytes.length) return "";
    let out = "";
    for (let i = off; i < bytes.length && out.length < 512; i++) {
      if (bytes[i] === 0) return out;
      out += String.fromCharCode(bytes[i]);
    }
    return "";
  }

  function modelName(name) {
    return /\.(obj|ghg|gsc)$/i.test(name || "");
  }

  function pushFile(files, name, offset, size, zsize, fileSize) {
    const bytes = zsize || size;
    if (!bytes || offset < 0 || bytes > fileSize || offset > fileSize - bytes || bytes > 64 * 1024 * 1024) return;
    files.push({ name: name || "file", offset: offset, size: size, zsize: zsize, length: bytes });
  }

  function parseOld(info, fileSize) {
    if (info.length < 12) return null;
    const type = i32(info, 0);
    const count = u32(info, 4);
    if (!count || count > 400000 || type > 16 || type < -32) return null;
    if (8 + count * 16 + 4 > info.length) return null;
    const field = type <= -5 ? 12 : 8;
    const nameInfo = 8 + count * 16;
    const nameCount = u32(info, nameInfo);
    if (nameCount > 1000000) return null;
    const nameCursor0 = nameInfo + 4;
    const stringsAt = nameCursor0 + nameCount * field;
    if (stringsAt + 4 > info.length) return null;
    const stringBase = stringsAt + 4;
    const produced = [];
    const stored = [];
    let fullPath = "";
    let nameCursor = nameCursor0;
    for (let i = 0; i < count; i++) {
      let next = 1;
      let name = "";
      let guard = 0;
      do {
        if (++guard > 100000 || nameCursor + field > info.length) return null;
        next = i16(info, nameCursor);
        const prev = i16(info, nameCursor + 2);
        const off = i32(info, nameCursor + 4);
        nameCursor += field;
        if (off >= 0) name = cstr(info, stringBase + off);
        else name = "";
        if (name && name.charCodeAt(0) >= 0xf0) name = "";
        if (prev !== 0) fullPath = prev > 0 && prev < stored.length ? stored[prev] : "";
        stored.push(fullPath);
        if (next > 0 && name) {
          if (fullPath && fullPath[fullPath.length - 1] !== "\\") fullPath += "\\";
          fullPath += name + "\\";
        }
      } while (next > 0);
      produced.push(fullPath + name);
    }
    const files = [];
    for (let i = 0; i < count; i++) {
      const rec = 8 + i * 16;
      let offset = u32(info, rec);
      const zsize = u32(info, rec + 4);
      const size = u32(info, rec + 8);
      const offset2 = info[rec + 15];
      if (type !== -1) offset = ((offset << 8) | offset2) >>> 0;
      else offset = (offset + offset2) >>> 0;
      pushFile(files, produced[i], offset, size, zsize, fileSize);
    }
    return files.length ? { kind: "dat", files: files } : null;
  }

  function parseCc4(info, fileSize) {
    if (info.length < 24) return null;
    let p = 4;
    const magic = String.fromCharCode.apply(null, info.subarray(p, p + 8));
    if (magic !== ".CC40TAD") return null;
    p += 8;
    const ver = be32(info, p + 4);
    p += 8;
    const names = be32(info, p + 4);
    const namesSize = be32(info, p + 8);
    p += 12;
    if (!names || names > 1000000 || p + namesSize + 4 > info.length) return null;
    const namesOff = p;
    p += namesSize + 4;
    const folders = new Array(names).fill("");
    const produced = [];
    let myId = 0;
    for (let i = 0; i < names; i++) {
      const step = ver >= 2 ? 12 : 10;
      if (p + step > info.length) return null;
      const nameOff = be32(info, p);
      const folderId = be16(info, p + 4);
      let fileId;
      if (ver >= 2) fileId = be16(info, p + 10);
      else fileId = be16(info, p + 8);
      p += step;
      if (nameOff === 0xffffffff) continue;
      let name = cstr(info, namesOff + nameOff);
      if (i + 1 === names) fileId = myId;
      let parent = folderId < folders.length ? folders[folderId] : "";
      if (parent && parent[parent.length - 1] !== "\\") parent += "\\";
      name = parent + name;
      if (fileId !== 0) {
        produced.push(name);
        myId++;
      } else if (i < folders.length) folders[i] = name;
    }
    if (p + 8 > info.length) return null;
    const type2 = be32(info, p) | 0;
    const files2 = be32(info, p + 4);
    p += 8;
    if (!files2 || files2 > 400000) return null;
    const files = [];
    for (let i = 0; i < files2; i++) {
      let offset;
      let zsize;
      let size;
      if (type2 <= -11) {
        if (p + 16 > info.length) return null;
        const hi = be32(info, p);
        const lo = be32(info, p + 4);
        offset = hi * 0x100000000 + lo;
        zsize = be32(info, p + 8);
        size = be32(info, p + 12) & 0x7fffffff;
        p += 16;
      } else {
        if (p + 16 > info.length) return null;
        offset = be32(info, p);
        zsize = be32(info, p + 4);
        size = be32(info, p + 8);
        const offset2 = info[p + 15];
        offset = ((offset << 8) | offset2) >>> 0;
        if (type2 <= -10) size &= 0x7fffffff;
        p += 16;
      }
      const name = produced[i] || "";
      pushFile(files, name, offset, size, zsize, fileSize);
    }
    return files.length ? { kind: "cc4", files: files } : null;
  }

  function parseDatInfo(info, fileSize) {
    if (!info || info.length < 12) return null;
    const type = u32(info, 0);
    const files = u32(info, 4);
    if (type === 0x3443432e || type === 0x2e434334 || files === 0x3443432e || files === 0x2e434334) {
      return parseCc4(info, fileSize);
    }
    return parseOld(info, fileSize);
  }

  function parsePak(bytes) {
    if (!bytes || bytes.length < 24 || u32(bytes, 0) !== 0x1234567a) return null;
    const count = u32(bytes, 4);
    if (!count || count > 400000 || 24 + count * 28 > bytes.length) return null;
    const files = [];
    for (let i = 0; i < count; i++) {
      const e = 24 + i * 28;
      const nameOff = u32(bytes, e);
      const offset = u32(bytes, e + 4);
      const size = u32(bytes, e + 8);
      pushFile(files, cstr(bytes, nameOff), offset, size, size, bytes.length);
    }
    return files.length ? { kind: "pak", files: files } : null;
  }

  function list(bytes) {
    if (!bytes || bytes.length < 16) return null;
    const pak = parsePak(bytes);
    if (pak) return pak;
    let infoOff = u32(bytes, 0);
    if (infoOff & 0x80000000) {
      infoOff = (((infoOff ^ 0xffffffff) << 8) + 0x100) >>> 0;
    }
    const infoSize = u32(bytes, 4);
    if (infoOff < 8 || infoSize < 16 || infoOff > bytes.length || infoSize > bytes.length - infoOff) return null;
    return parseDatInfo(bytes.subarray(infoOff, infoOff + infoSize), bytes.length);
  }

  function models(listing) {
    if (!listing || !listing.files) return [];
    return listing.files.filter(function (file) { return modelName(file.name); });
  }

  async function listFile(file) {
    const size = file.size;
    const head = new Uint8Array(await file.slice(0, Math.min(size, 24)).arrayBuffer());
    if (head.length >= 8 && u32(head, 0) === 0x1234567a) {
      const count = u32(head, 4);
      const table = 24 + count * 28;
      if (!count || count > 400000 || table > size || table > 32 * 1024 * 1024) return null;
      const bytes = new Uint8Array(await file.slice(0, table).arrayBuffer());
      const files = [];
      for (let i = 0; i < count; i++) {
        const e = 24 + i * 28;
        const nameOff = u32(bytes, e);
        const offset = u32(bytes, e + 4);
        const fsize = u32(bytes, e + 8);
        let name = "";
        if (nameOff < bytes.length) name = cstr(bytes, nameOff);
        else if (nameOff < size) {
          const raw = new Uint8Array(await file.slice(nameOff, Math.min(size, nameOff + 256)).arrayBuffer());
          name = cstr(raw, 0);
        }
        pushFile(files, name, offset, fsize, fsize, size);
      }
      return { kind: "pak", files: files };
    }
    if (head.length < 8) return null;
    let infoOff = u32(head, 0);
    if (infoOff & 0x80000000) infoOff = (((infoOff ^ 0xffffffff) << 8) + 0x100) >>> 0;
    const infoSize = u32(head, 4);
    if (infoOff < 8 || infoSize < 16 || infoOff > size || infoSize > size - infoOff || infoSize > 64 * 1024 * 1024) return null;
    const info = new Uint8Array(await file.slice(infoOff, infoOff + infoSize).arrayBuffer());
    return parseDatInfo(info, size);
  }

  return { list: list, models: models, listFile: listFile };
});
