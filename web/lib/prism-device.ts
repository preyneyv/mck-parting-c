const MAGIC = 0x4d535250;
const VERSION = 2;
const VID = 0x2e8a;
const PID = 0x000a;
const RESPONSE = 1;
const EVENT = 2;
const ERROR = 4;
const ICON_WIDTH = 36;
const ICON_HEIGHT = 36;
const ICON_BYTES = Math.ceil(ICON_WIDTH / 8) * ICON_HEIGHT;

export const Message = {
  hello: 0x01, deviceInfo: 0x02, storageInfo: 0x03, cartridges: 0x04, cartridgeIcon: 0x05,
  installBegin: 0x10, installChunk: 0x11, installCommit: 0x12, delete: 0x13, compact: 0x14, operationProgress: 0x15,
  settingsGet: 0x20, settingsPreview: 0x21,
  mirrorSubscribe: 0x30, mirrorUnsubscribe: 0x31, mirrorFrame: 0x32,
  remoteInput: 0x33, heartbeat: 0x34, log: 0x35,
} as const;

export type DeviceInfo = { serial: string; protocolVersion: number; firmware: string; flashBytes: number; blockBytes: number; capabilities: number };
export type CartridgeMetadata = { slug: string; name: string; icon: Uint8Array };
export type Cartridge = CartridgeMetadata & { uuid: Uint8Array; packageBytes: number; persistentBytes: number; blocks: number; policy: number };
export type StorageInfo = { totalBlocks: number; liveBlocks: number; erasedBlocks: number; deadBlocks: number; largestFreeRun: number; largestReclaimableRun: number; scratchBlocks: number; requiredBlocks: number; blockStates: Uint8Array };
export type LedEffect = 0 | 1 | 2 | 3;
export type LedSettings = { effect: LedEffect; speedMs: number; colors: string[] };
export type Settings = { volume: number; brightness: number; linked: boolean; leds: [LedSettings, LedSettings] };
export type MirrorFrame = { sequence: number; framebuffer: Uint8Array; leds: [string, string]; buttons: number };
export type OperationProgress = { operation: number; phase: number; completedBlocks: number; totalBlocks: number };
type Pending = { resolve: (payload: Uint8Array) => void; reject: (error: Error) => void; timer: ReturnType<typeof setTimeout> };

function view(bytes: Uint8Array) { return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength); }
function stringAt(bytes: Uint8Array, offset: number, length: number) { const end = bytes.indexOf(0, offset); return new TextDecoder().decode(bytes.subarray(offset, end >= offset && end < offset + length ? end : offset + length)); }
function hex(bytes: Uint8Array) { return Array.from(bytes, byte => byte.toString(16).padStart(2, "0")).join("").toUpperCase(); }
function rgb(bytes: Uint8Array, offset: number) { return `#${hex(bytes.subarray(offset, offset + 3))}`; }
function rgbBytes(color: string) { const clean = color.replace("#", ""); return [0, 2, 4].map(index => Number.parseInt(clean.slice(index, index + 2), 16)); }

function packageMetadata(bytes: Uint8Array): CartridgeMetadata {
  const header = view(bytes);
  const imageOffset = header.getUint32(108, true);
  const imageSize = header.getUint32(112, true);
  const descriptorOffset = header.getUint32(116, true);
  if (descriptorOffset + 64 > bytes.length || imageOffset + imageSize > bytes.length)
    throw new Error("This cartridge has invalid metadata offsets.");
  const iconRelative = header.getUint32(descriptorOffset + 24, true) & ~1;
  const icon = new Uint8Array(ICON_BYTES);
  if (iconRelative !== 0) {
    if (iconRelative + ICON_BYTES > imageSize)
      throw new Error("This cartridge has an invalid icon.");
    icon.set(bytes.subarray(imageOffset + iconRelative, imageOffset + iconRelative + ICON_BYTES));
  }
  return { slug: stringAt(bytes, 44, 32), name: stringAt(bytes, 76, 32), icon };
}

export class PrismDevice {
  private device?: USBDevice;
  private interfaceNumber = -1;
  private endpointIn = -1;
  private endpointOut = -1;
  private requestId = 0;
  private pending = new Map<number, Pending>();
  private incoming = new Uint8Array();
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
    const merged = new Uint8Array(this.incoming.length + chunk.length);
    merged.set(this.incoming); merged.set(chunk, this.incoming.length); this.incoming = merged;
    while (this.incoming.length >= 16) {
      const header = view(this.incoming);
      if (header.getUint32(0, true) !== MAGIC || header.getUint8(4) !== VERSION) { this.incoming = this.incoming.slice(1); continue; }
      const length = header.getUint32(12, true);
      if (length > 4096) { this.incoming = this.incoming.slice(1); continue; }
      if (this.incoming.length < 16 + length) return;
      const type = header.getUint8(5), flags = header.getUint16(6, true), requestId = header.getUint32(8, true);
      const payload = this.incoming.slice(16, 16 + length);
      this.incoming = this.incoming.slice(16 + length);
      if (flags & EVENT) this.event(type, payload);
      else if (flags & RESPONSE) {
        const pending = this.pending.get(requestId);
        if (!pending) continue;
        clearTimeout(pending.timer); this.pending.delete(requestId);
        if (flags & ERROR) pending.reject(new Error(`Prism rejected command (status ${payload.length >= 2 ? view(payload).getUint16(0, true) : "unknown"}).`));
        else pending.resolve(payload);
      }
    }
  }

  private event(type: number, payload: Uint8Array) {
    if (type === Message.log) this.onLog?.(new TextDecoder().decode(payload));
    if (type === Message.mirrorFrame && payload.length === 1036) { this.onSleepChange?.(false); this.onMirror?.({ sequence: view(payload).getUint32(0, true), framebuffer: payload.slice(4, 1028), leds: [rgb(payload, 1028), rgb(payload, 1031)], buttons: payload[1034] }); }
    if (type === Message.operationProgress && payload.length === 8) { const value = view(payload); this.onOperationProgress?.({ operation: payload[0], phase: payload[1], completedBlocks: value.getUint16(2, true), totalBlocks: value.getUint16(4, true) }); }
  }

  private async request(type: number, payload: Uint8Array<ArrayBufferLike> = new Uint8Array(), timeoutMs = 5000) {
    if (!this.device?.opened) throw new Error("Connect a Prism first.");
    const requestId = ++this.requestId;
    const packet = new Uint8Array(16 + payload.length), header = view(packet);
    header.setUint32(0, MAGIC, true); header.setUint8(4, VERSION); header.setUint8(5, type); header.setUint32(8, requestId, true); header.setUint32(12, payload.length, true); packet.set(payload, 16);
    const response = new Promise<Uint8Array>((resolve, reject) => {
      const timer = setTimeout(() => { this.pending.delete(requestId); reject(new Error("Prism did not respond.")); }, timeoutMs);
      this.pending.set(requestId, { resolve, reject, timer });
    });
    let lastError: unknown;
    for (let attempt = 0; attempt < 3; attempt++) {
      try { await this.device.transferOut(this.endpointOut, packet); lastError = undefined; break; }
      catch (error) {
        lastError = error;
        await new Promise(resolve => setTimeout(resolve, 200));
        // A response can arrive even when WinUSB reports the OUT transfer as
        // failed. In that case the pending entry has already been resolved.
        if (!this.pending.has(requestId)) return response;
        try { await this.device.clearHalt("out", this.endpointOut); } catch { /* retry open pipe */ }
      }
    }
    if (lastError) { const item = this.pending.get(requestId); if (item) clearTimeout(item.timer); this.pending.delete(requestId); throw lastError; }
    return response;
  }

  async deviceInfo(): Promise<DeviceInfo> {
    const data = await this.request(Message.hello), value = view(data);
    return { serial: hex(data.subarray(0, 8)), protocolVersion: value.getUint16(8, true), firmware: `${value.getUint16(10, true)}.${value.getUint16(12, true)}.${value.getUint16(14, true)}`, flashBytes: value.getUint32(16, true), blockBytes: value.getUint32(20, true), capabilities: value.getUint32(24, true) };
  }

  async cartridges(): Promise<Cartridge[]> {
    const data = await this.request(Message.cartridges), count = view(data).getUint16(0, true), result: Cartridge[] = []; let offset = 4;
    for (let index = 0; index < count; index++, offset += 92) { const value = view(data.subarray(offset, offset + 92)), uuid = data.slice(offset, offset + 16); result.push({ uuid, packageBytes: value.getUint32(16, true), persistentBytes: value.getUint32(20, true), blocks: value.getUint16(24, true), policy: value.getUint16(26, true), slug: stringAt(data, offset + 28, 32), name: stringAt(data, offset + 60, 32), icon: await this.cartridgeIcon(uuid) }); }
    return result;
  }

  private async cartridgeIcon(uuid: Uint8Array) { return this.request(Message.cartridgeIcon, uuid); }

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
  async deleteCartridge(uuid: Uint8Array) { await this.request(Message.delete, uuid); }
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
        packageHeader.getUint16(4, true) !== 3 ||
        packageHeader.getUint16(6, true) !== 256 ||
        packageHeader.getUint16(8, true) !== 1 ||
        packageHeader.getUint32(12, true) !== bytes.length)
      throw new Error("This is not a compatible Prism cartridge package.");
    const metadata = packageMetadata(bytes);
    onMetadata(metadata);
    const uuid = bytes.subarray(16, 32);
    const begin = new Uint8Array(60 + ICON_BYTES), value = view(begin);
    begin.set(uuid); value.setUint32(16, bytes.length, true); value.setUint32(20, crc32(bytes), true); value.setUint16(24, Math.ceil(bytes.length / (128 * 1024)), true);
    begin.set(new TextEncoder().encode(metadata.name).subarray(0, 31), 28);
    begin.set(metadata.icon, 60);
    await this.request(Message.installBegin, begin, 30000);
    for (let offset = 0; offset < bytes.length; offset += 1024) { const chunk = bytes.subarray(offset, offset + 1024), packet = new Uint8Array(8 + chunk.length), chunkView = view(packet); chunkView.setUint32(0, offset, true); chunkView.setUint16(4, chunk.length, true); packet.set(chunk, 8); await this.request(Message.installChunk, packet, 30000); onProgress((offset + chunk.length) / bytes.length); }
    await this.request(Message.installCommit, new Uint8Array(), 30000);
  }
}

function decodeSettings(data: Uint8Array): Settings {
  const value = view(data), leds: LedSettings[] = [];
  for (let led = 0; led < 2; led++) { const offset = 4 + led * 52, count = Math.max(1, data[offset + 1]), colors: string[] = []; for (let color = 0; color < count; color++) colors.push(rgb(data, offset + 4 + color * 3)); leds.push({ effect: data[offset] as LedEffect, speedMs: value.getUint16(offset + 2, true), colors }); }
  return { volume: data[0], brightness: data[2], linked: Boolean(data[1]), leds: leds as [LedSettings, LedSettings] };
}

function encodeSettings(settings: Settings) {
  const data = new Uint8Array(108), value = view(data); data[0] = settings.volume; data[1] = settings.linked ? 1 : 0; data[2] = settings.brightness; data[3] = 1;
  for (let led = 0; led < 2; led++) { const item = settings.linked && led === 1 ? settings.leds[0] : settings.leds[led], offset = 4 + led * 52; data[offset] = item.effect; data[offset + 1] = Math.min(16, item.colors.length); value.setUint16(offset + 2, item.speedMs, true); item.colors.slice(0, 16).forEach((color, index) => data.set(rgbBytes(color), offset + 4 + index * 3)); }
  return data;
}

function crc32(bytes: Uint8Array) { let crc = 0xffffffff; for (const byte of bytes) { crc ^= byte; for (let bit = 0; bit < 8; bit++) crc = (crc >>> 1) ^ (0xedb88320 & -(crc & 1)); } return (crc ^ 0xffffffff) >>> 0; }
