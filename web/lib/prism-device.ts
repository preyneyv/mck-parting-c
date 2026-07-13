const MAGIC = 0x4d535250;
const VERSION = 1;
const VID = 0x2e8a;
const PID = 0x000a;
const RESPONSE = 1;
const EVENT = 2;
const ERROR = 4;
const ICON_WIDTH = 36;
const ICON_HEIGHT = 36;
const ICON_BYTES = Math.ceil(ICON_WIDTH / 8) * ICON_HEIGHT;
const CARTRIDGE_LIST_INCLUDE_HIDDEN = 1;

export const Message = {
  hello: 0x01, deviceInfo: 0x02, storageInfo: 0x03, cartridges: 0x04, cartridgeIcon: 0x05, reboot: 0x06,
  installBegin: 0x10, installChunk: 0x11, installCommit: 0x12, delete: 0x13, compact: 0x14, operationProgress: 0x15, launch: 0x16,
  settingsGet: 0x20, settingsPreview: 0x21,
  mirrorSubscribe: 0x30, mirrorUnsubscribe: 0x31, mirrorFrame: 0x32,
  remoteInput: 0x33, heartbeat: 0x34, log: 0x35,
} as const;

export const Capability = { reboot: 1 << 7 } as const;

export type DeviceInfo = { serial: string; protocolVersion: number; firmware: string; flashBytes: number; blockBytes: number; capabilities: number };
export type CartridgeMetadata = { id: string; name: string; version: number; appKey: Uint8Array; icon: Uint8Array };
export type Cartridge = CartridgeMetadata & { packageBytes: number; persistentBytes: number; blocks: number; policy: number };
export type StorageInfo = { totalBlocks: number; liveBlocks: number; erasedBlocks: number; deadBlocks: number; largestFreeRun: number; largestReclaimableRun: number; scratchBlocks: number; requiredBlocks: number; blockStates: Uint8Array };
export type LedEffect = 0 | 1 | 2 | 3;
export type LedSettings = { effect: LedEffect; speedMs: number; phaseOffset: number; colors: string[] };
export type Settings = { volume: number; brightness: number; linked: boolean; leds: [LedSettings, LedSettings] };
export type MirrorFrame = { sequence: number; framebuffer: Uint8Array; leds: [string, string]; buttons: number };
export type OperationProgress = { operation: number; phase: number; completedBlocks: number; totalBlocks: number };
type Pending = { resolve: (payload: Uint8Array) => void; reject: (error: Error) => void; timer: ReturnType<typeof setTimeout> };

function view(bytes: Uint8Array) { return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength); }
function stringAt(bytes: Uint8Array, offset: number, length: number) { const end = bytes.indexOf(0, offset); return new TextDecoder().decode(bytes.subarray(offset, end >= offset && end < offset + length ? end : offset + length)); }
function hex(bytes: Uint8Array) { return Array.from(bytes, byte => byte.toString(16).padStart(2, "0")).join("").toUpperCase(); }
function rgb(bytes: Uint8Array, offset: number) { return `#${hex(bytes.subarray(offset, offset + 3))}`; }
function rgbBytes(color: string) { const clean = color.replace("#", ""); return [0, 2, 4].map(index => Number.parseInt(clean.slice(index, index + 2), 16)); }

function cartridgeIdValid(id: string) {
  const encoded = new TextEncoder().encode(id);
  if (encoded.length < 1 || encoded.length > 253 || encoded.length !== id.length) return false;
  return id.split(".").every(label => label.length >= 1 && label.length <= 63 && /^[a-z0-9](?:[a-z0-9-]*[a-z0-9])?$/.test(label));
}

async function packageMetadata(bytes: Uint8Array): Promise<CartridgeMetadata> {
  const header = view(bytes);
  const imageOffset = header.getUint32(40, true);
  const imageSize = header.getUint32(44, true);
  const descriptorOffset = header.getUint32(48, true);
  if (descriptorOffset < imageOffset || descriptorOffset + 60 > imageOffset + imageSize || imageOffset + imageSize > bytes.length)
    throw new Error("This cartridge has invalid metadata offsets.");
  const idRelative = header.getUint32(descriptorOffset + 16, true);
  const nameRelative = header.getUint32(descriptorOffset + 20, true);
  const iconRelative = header.getUint32(descriptorOffset + 24, true);
  if (idRelative >= imageSize || nameRelative >= imageSize)
    throw new Error("This cartridge has invalid identity metadata.");
  const id = stringAt(bytes, imageOffset + idRelative, imageSize - idRelative);
  const name = stringAt(bytes, imageOffset + nameRelative, imageSize - nameRelative);
  if (!cartridgeIdValid(id) || !name || new TextEncoder().encode(name).length > 31)
    throw new Error("This cartridge has invalid authored metadata.");
  const appKey = bytes.slice(16, 32);
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(`prism.app.v1\0${id}`)));
  if (!appKey.every((byte, index) => byte === digest[index]))
    throw new Error("This cartridge's app key does not match its full ID.");
  const icon = new Uint8Array(ICON_BYTES);
  if (iconRelative !== 0) {
    if (iconRelative + ICON_BYTES > imageSize)
      throw new Error("This cartridge has an invalid icon.");
    icon.set(bytes.subarray(imageOffset + iconRelative, imageOffset + iconRelative + ICON_BYTES));
  }
  return { id, name, version: header.getUint32(descriptorOffset + 12, true), appKey, icon };
}

export class PrismDevice {
  private device?: USBDevice;
  private interfaceNumber = -1;
  private endpointIn = -1;
  private endpointOut = -1;
  private requestId = 0;
  private pending = new Map<number, Pending>();
  private incoming = new Uint8Array(8192);
  private incomingStart = 0;
  private incomingEnd = 0;
  private requestQueue: Promise<void> = Promise.resolve();
  private reading = false;
  private heartbeat?: ReturnType<typeof setInterval>;
  onMirror?: (frame: MirrorFrame) => void;
  onSleepChange?: (sleeping: boolean) => void;
  onLog?: (text: string) => void;
  onOperationProgress?: (progress: OperationProgress) => void;
  onDisconnect?: () => void;

  static supported() { return typeof navigator !== "undefined" && "usb" in navigator; }

  async connect() {
    if (!PrismDevice.supported()) throw new Error("WebUSB is not available in this browser. Use Chrome or Edge on desktop.");
    const known = (await navigator.usb.getDevices()).find(device => device.vendorId === VID && device.productId === PID);
    this.device = known ?? await navigator.usb.requestDevice({ filters: [{ vendorId: VID, productId: PID }] });
    await this.device.open();
    if (!this.device.configuration) await this.device.selectConfiguration(1);
    const management = this.device.configuration?.interfaces.find(item => item.alternate.interfaceClass === 0xff);
    if (!management) throw new Error("This Prism firmware does not expose the management interface.");
    this.interfaceNumber = management.interfaceNumber;
    this.endpointIn = management.alternate.endpoints.find(endpoint => endpoint.direction === "in")?.endpointNumber ?? -1;
    this.endpointOut = management.alternate.endpoints.find(endpoint => endpoint.direction === "out")?.endpointNumber ?? -1;
    if (this.endpointIn < 0 || this.endpointOut < 0) throw new Error("Prism management endpoints are missing.");
    await this.device.claimInterface(this.interfaceNumber);
    this.reading = true;
    navigator.usb.addEventListener("disconnect", this.handleDisconnect);
    void this.readLoop();
    this.heartbeat = setInterval(() => {
      // Every command refreshes the device-side session timeout. Avoid a
      // concurrent heartbeat transfer while an install/erase request is in
      // flight; WinUSB can otherwise report the flash stall on the unrelated
      // OUT transfer and poison the next chunk submission.
      if (this.pending.size === 0)
        void this.request(Message.heartbeat)
          .then(payload => this.onSleepChange?.(payload.length >= 4 && (view(payload).getUint16(2, true) & 1) !== 0))
          .catch(() => undefined);
    }, 1000);
    return this.deviceInfo();
  }

  private handleDisconnect = (event: USBConnectionEvent) => { if (event.device === this.device) void this.close(); };

  async close() {
    this.reading = false;
    if (this.heartbeat) clearInterval(this.heartbeat);
    this.heartbeat = undefined;
    navigator.usb?.removeEventListener("disconnect", this.handleDisconnect);
    for (const item of this.pending.values()) { clearTimeout(item.timer); item.reject(new Error("Prism disconnected.")); }
    this.pending.clear();
    try { if (this.device?.opened && this.interfaceNumber >= 0) await this.device.releaseInterface(this.interfaceNumber); } catch { /* already gone */ }
    try { if (this.device?.opened) await this.device.close(); } catch { /* already gone */ }
    this.device = undefined;
    this.onDisconnect?.();
  }

  private async readLoop() {
    let consecutiveFailures = 0;
    let firstFailureAt = 0;
    while (this.reading && this.device?.opened) {
      try {
        // One mirror frame is just over 1 KiB. Reading it as 64-byte WebUSB
        // operations creates ~1,000 promises/second at 60 fps and can starve
        // installer responses. Let the USB stack batch a complete frame.
        const result = await this.device.transferIn(this.endpointIn, 2048);
        if (result.status === "stall") {
          await this.device.clearHalt("in", this.endpointIn);
          continue;
        }
        if (result.data?.byteLength) this.accept(new Uint8Array(result.data.buffer, result.data.byteOffset, result.data.byteLength));
        consecutiveFailures = 0;
        firstFailureAt = 0;
      } catch (error) {
        if (!this.reading || !this.device?.opened) break;
        consecutiveFailures++;
        if (!firstFailureAt) firstFailureAt = Date.now();
        if (consecutiveFailures === 1 || consecutiveFailures % 20 === 0)
          this.onLog?.(`[management] waiting for Prism after a flash operation: ${error instanceof Error ? error.message : "read failed"}\n`);
        // RP2040 flash erase/program temporarily stalls XIP and WinUSB may
        // reject reads during that window. A physical unplug is handled by
        // navigator.usb's disconnect event; only abandon a mounted device
        // after a genuinely prolonged outage.
        if (Date.now() - firstFailureAt >= 120000) { await this.close(); break; }
        await new Promise(resolve => setTimeout(resolve, 250));
      }
    }
  }

  private accept(chunk: Uint8Array) {
    this.appendIncoming(chunk);
    while (this.incomingEnd - this.incomingStart >= 16) {
      const available = this.incoming.subarray(this.incomingStart, this.incomingEnd);
      const header = view(available);
      if (header.getUint32(0, true) !== MAGIC || header.getUint8(4) !== VERSION) {
        this.incomingStart++;
        continue;
      }
      const length = header.getUint32(12, true);
      if (length > 4096) {
        this.incomingStart++;
        continue;
      }
      if (available.length < 16 + length) break;
      const type = header.getUint8(5), flags = header.getUint16(6, true), requestId = header.getUint32(8, true);
      const payload = available.slice(16, 16 + length);
      this.incomingStart += 16 + length;
      if (flags & EVENT) this.event(type, payload);
      else if (flags & RESPONSE) {
        const pending = this.pending.get(requestId);
        if (!pending) continue;
        clearTimeout(pending.timer); this.pending.delete(requestId);
        if (flags & ERROR) pending.reject(new Error(`Prism rejected command (status ${payload.length >= 2 ? view(payload).getUint16(0, true) : "unknown"}).`));
        else pending.resolve(payload);
      }
    }
    if (this.incomingStart === this.incomingEnd) {
      this.incomingStart = 0;
      this.incomingEnd = 0;
    }
  }

  private appendIncoming(chunk: Uint8Array) {
    const unread = this.incomingEnd - this.incomingStart;
    if (this.incoming.length - this.incomingEnd < chunk.length) {
      if (this.incomingStart > 0) {
        this.incoming.copyWithin(0, this.incomingStart, this.incomingEnd);
        this.incomingStart = 0;
        this.incomingEnd = unread;
      }
      if (this.incoming.length - this.incomingEnd < chunk.length) {
        const capacity = Math.max(this.incoming.length * 2, unread + chunk.length);
        const expanded = new Uint8Array(capacity);
        expanded.set(this.incoming.subarray(this.incomingStart, this.incomingEnd));
        this.incoming = expanded;
        this.incomingStart = 0;
        this.incomingEnd = unread;
      }
    }
    this.incoming.set(chunk, this.incomingEnd);
    this.incomingEnd += chunk.length;
  }

  private event(type: number, payload: Uint8Array) {
    if (type === Message.log) this.onLog?.(new TextDecoder().decode(payload));
    if (type === Message.mirrorFrame && payload.length === 1036) { this.onSleepChange?.(false); this.onMirror?.({ sequence: view(payload).getUint32(0, true), framebuffer: payload.slice(4, 1028), leds: [rgb(payload, 1028), rgb(payload, 1031)], buttons: payload[1034] }); }
    if (type === Message.operationProgress && payload.length === 8) { const value = view(payload); this.onOperationProgress?.({ operation: payload[0], phase: payload[1], completedBlocks: value.getUint16(2, true), totalBlocks: value.getUint16(4, true) }); }
  }

  private request(type: number, payload: Uint8Array<ArrayBufferLike> = new Uint8Array(), timeoutMs = 5000) {
    const operation = this.requestQueue.then(() =>
      this.performRequest(type, payload, timeoutMs));
    this.requestQueue = operation.then(() => undefined, () => undefined);
    return operation;
  }

  private async performRequest(type: number, payload: Uint8Array<ArrayBufferLike>, timeoutMs: number) {
    if (!this.device?.opened) throw new Error("Connect a Prism first.");
    const requestId = ++this.requestId;
    const packet = new Uint8Array(16 + payload.length), header = view(packet);
    header.setUint32(0, MAGIC, true); header.setUint8(4, VERSION); header.setUint8(5, type); header.setUint32(8, requestId, true); header.setUint32(12, payload.length, true); packet.set(payload, 16);
    const response = new Promise<Uint8Array>((resolve, reject) => {
      const timer = setTimeout(() => { this.pending.delete(requestId); reject(new Error("Prism did not respond.")); }, timeoutMs);
      this.pending.set(requestId, { resolve, reject, timer });
    });
    try {
      await this.device.transferOut(this.endpointOut, packet);
    } catch {
      // WinUSB may report failure after the device accepted a command. Never
      // replay a mutating request with an ambiguous outcome; its response or
      // timeout determines the result.
      try { await this.device.clearHalt("out", this.endpointOut); } catch { }
    }
    return response;
  }

  async deviceInfo(): Promise<DeviceInfo> {
    const data = await this.request(Message.hello), value = view(data);
    return { serial: hex(data.subarray(0, 8)), protocolVersion: value.getUint16(8, true), firmware: `${value.getUint16(10, true)}.${value.getUint16(12, true)}.${value.getUint16(14, true)}`, flashBytes: value.getUint32(16, true), blockBytes: value.getUint32(20, true), capabilities: value.getUint32(24, true) };
  }

  async cartridges(includeHidden = false): Promise<Cartridge[]> {
    const result: Cartridge[] = [];
    let startIndex = 0;
    let totalCount = 0;
    do {
      const request = new Uint8Array(4);
      view(request).setUint16(0, startIndex, true);
      view(request).setUint16(2, includeHidden ? CARTRIDGE_LIST_INCLUDE_HIDDEN : 0, true);
      const data = await this.request(Message.cartridges, request);
      if (data.length < 8) throw new Error("Prism returned an invalid cartridge list.");
      const list = view(data), count = list.getUint16(4, true);
      totalCount = list.getUint16(0, true);
      const responseStart = list.getUint16(2, true);
      const stringBytes = list.getUint16(6, true), stringsOffset = 8 + count * 40;
      if (responseStart !== startIndex || stringsOffset + stringBytes > data.length)
        throw new Error("Prism returned an invalid cartridge list.");
      for (let index = 0; index < count; index++) {
        const offset = 8 + index * 40, entry = view(data.subarray(offset, offset + 40));
        const appKey = data.slice(offset, offset + 16);
        const idOffset = entry.getUint16(32, true), idLength = entry.getUint16(34, true);
        const nameOffset = entry.getUint16(36, true), nameLength = entry.getUint16(38, true);
        if (idOffset + idLength > stringBytes || nameOffset + nameLength > stringBytes)
          throw new Error("Prism returned invalid cartridge metadata.");
        result.push({ appKey, packageBytes: entry.getUint32(16, true), persistentBytes: entry.getUint32(20, true), version: entry.getUint32(24, true), blocks: entry.getUint16(28, true), policy: entry.getUint16(30, true), id: new TextDecoder().decode(data.subarray(stringsOffset + idOffset, stringsOffset + idOffset + idLength)), name: new TextDecoder().decode(data.subarray(stringsOffset + nameOffset, stringsOffset + nameOffset + nameLength)), icon: await this.cartridgeIcon(appKey) });
      }
      if (count === 0 && startIndex < totalCount) throw new Error("A cartridge entry is too large for the management protocol.");
      startIndex += count;
    } while (startIndex < totalCount);
    return result;
  }

  private async cartridgeIcon(appKey: Uint8Array) { return this.request(Message.cartridgeIcon, appKey); }

  async storage(): Promise<StorageInfo> {
    const data = await this.request(Message.storageInfo), value = view(data);
    const totalBlocks = value.getUint16(0, true);
    let blockStates = data.slice(16, 16 + totalBlocks);
    if (blockStates.length !== totalBlocks) {
      blockStates = new Uint8Array(totalBlocks);
      blockStates.fill(1, 0, value.getUint16(2, true));
      blockStates.fill(2, value.getUint16(2, true), value.getUint16(2, true) + value.getUint16(6, true));
    }
    return { totalBlocks, liveBlocks: value.getUint16(2, true), erasedBlocks: value.getUint16(4, true), deadBlocks: value.getUint16(6, true), largestFreeRun: value.getUint16(8, true), largestReclaimableRun: value.getUint16(10, true), scratchBlocks: value.getUint16(12, true), requiredBlocks: value.getUint16(14, true), blockStates };
  }

  async settings(): Promise<Settings> { return decodeSettings(await this.request(Message.settingsGet)); }
  async previewSettings(settings: Settings) { await this.request(Message.settingsPreview, encodeSettings(settings)); }
  async subscribeMirror() { await this.request(Message.mirrorSubscribe); }
  async unsubscribeMirror() { await this.request(Message.mirrorUnsubscribe); }
  async remoteInput(buttons: number) { await this.request(Message.remoteInput, Uint8Array.of(buttons, 0, 0, 0)); }
  async launchCartridge(appKey: Uint8Array) { await this.request(Message.launch, appKey); }
  async restart(bootsel = false) { await this.request(Message.reboot, Uint8Array.of(bootsel ? 1 : 0, 0, 0, 0)); }
  async deleteCartridge(appKey: Uint8Array) { await this.request(Message.delete, appKey); }
  async compact(onProgress: (ratio: number) => void) {
    const previous = this.onOperationProgress;
    this.onOperationProgress = progress => {
      previous?.(progress);
      if (progress.operation === 1)
        onProgress(progress.totalBlocks === 0 ? 0 : progress.completedBlocks / progress.totalBlocks);
    };
    try { await this.request(Message.compact, new Uint8Array(), 120000); }
    finally { this.onOperationProgress = previous; }
  }

  async install(file: File, onProgress: (ratio: number) => void, onMetadata: (metadata: CartridgeMetadata) => void) {
    const bytes = new Uint8Array(await file.arrayBuffer());
    if (bytes.length < 256) throw new Error("This .prism package is too short.");
    const packageHeader = view(bytes);
    if (packageHeader.getUint32(0, true) !== 0x4b505250 ||
        packageHeader.getUint16(4, true) !== 1 ||
        packageHeader.getUint16(6, true) !== 256 ||
        packageHeader.getUint16(8, true) !== 1 ||
        packageHeader.getUint16(10, true) === 0 ||
        packageHeader.getUint32(12, true) !== bytes.length)
      throw new Error("This is not a compatible Prism cartridge package.");
    const metadata = await packageMetadata(bytes);
    onMetadata(metadata);
    const appKey = bytes.subarray(16, 32);
    const begin = new Uint8Array(60 + ICON_BYTES), value = view(begin);
    begin.set(appKey); value.setUint32(16, bytes.length, true); value.setUint32(20, crc32(bytes), true); value.setUint16(24, Math.ceil(bytes.length / (128 * 1024)), true);
    begin.set(new TextEncoder().encode(metadata.name).subarray(0, 31), 28);
    begin.set(metadata.icon, 60);
    await this.request(Message.installBegin, begin, 30000);
    for (let offset = 0; offset < bytes.length; offset += 1024) { const chunk = bytes.subarray(offset, offset + 1024), packet = new Uint8Array(8 + chunk.length), chunkView = view(packet); chunkView.setUint32(0, offset, true); chunkView.setUint16(4, chunk.length, true); packet.set(chunk, 8); await this.request(Message.installChunk, packet, 30000); onProgress((offset + chunk.length) / bytes.length); }
    await this.request(Message.installCommit, new Uint8Array(), 30000);
  }
}

function decodeSettings(data: Uint8Array): Settings {
  const value = view(data), leds: LedSettings[] = [];
  for (let led = 0; led < 2; led++) { const offset = 4 + led * 56, count = Math.max(1, data[offset + 1]), colors: string[] = []; for (let color = 0; color < count; color++) colors.push(rgb(data, offset + 8 + color * 3)); leds.push({ effect: data[offset] as LedEffect, speedMs: value.getUint16(offset + 2, true), phaseOffset: data[offset + 4], colors }); }
  return { volume: data[0], brightness: data[2], linked: Boolean(data[1]), leds: leds as [LedSettings, LedSettings] };
}

function encodeSettings(settings: Settings) {
  const data = new Uint8Array(116), value = view(data); data[0] = settings.volume; data[1] = settings.linked ? 1 : 0; data[2] = settings.brightness; data[3] = 2;
  for (let led = 0; led < 2; led++) { const item = settings.leds[led], offset = 4 + led * 56; data[offset] = item.effect; data[offset + 1] = Math.min(16, item.colors.length); value.setUint16(offset + 2, item.speedMs, true); data[offset + 4] = item.phaseOffset; item.colors.slice(0, 16).forEach((color, index) => data.set(rgbBytes(color), offset + 8 + index * 3)); }
  return data;
}

function crc32(bytes: Uint8Array) { let crc = 0xffffffff; for (const byte of bytes) { crc ^= byte; for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1)); } return (crc ^ 0xffffffff) >>> 0; }
