"use client";

import { useEffect, useRef, useState } from "react";
import {
  ArrowClockwise as RefreshCw,
  ArrowCounterClockwise as RotateCcw,
  CaretDown as ChevronDown,
  CircleNotch as Loader2,
  Moon,
  Package,
  Plugs as Unplug,
  PlugsConnected as Cable,
  Trash as Trash2,
  UploadSimple as Upload,
  Usb,
} from "@phosphor-icons/react";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { DropdownMenu, DropdownMenuContent, DropdownMenuItem, DropdownMenuTrigger } from "@/components/ui/dropdown-menu";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Slider } from "@/components/ui/slider";
import { Switch } from "@/components/ui/switch";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { PrismScreen, type PrismScreenHandle } from "@/components/prism-screen";
import { Capability, PrismDevice, type AssetPack, type Cartridge, type DeviceInfo, type InstallMetadata, type LedEffect, type LedSettings, type Settings, type StorageInfo } from "@/lib/prism-device";
import { prismpackIcon } from "@/lib/prismpack-icon";

const defaultSettings: Settings = { volume: 4, brightness: 8, linked: true, leds: [{ effect: 3, speedMs: 8000, phaseOffset: 0, colors: ["#1860ff"] }, { effect: 3, speedMs: 8000, phaseOffset: 14, colors: ["#1860ff"] }] };
const effects = [{ value: "0", label: "static" }, { value: "1", label: "breathing" }, { value: "2", label: "crossfade" }, { value: "3", label: "rainbow" }];
const developerModeKey = "prism:developer-mode:v1";

function loadDeveloperMode() {
  try { return window.localStorage.getItem(developerModeKey) === "1"; }
  catch { return false; }
}

function saveDeveloperMode(enabled: boolean) {
  try { window.localStorage.setItem(developerModeKey, enabled ? "1" : "0"); }
  catch { }
}

export function ManagementWorkspace() {
  const deviceRef = useRef<PrismDevice | undefined>(undefined);
  const screenRef = useRef<PrismScreenHandle>(null);
  const ledRefs = useRef<[HTMLSpanElement | null, HTMLSpanElement | null]>([null, null]);
  const remoteMask = useRef(0);
  const remoteQueue = useRef<Promise<void>>(Promise.resolve());
  const suppressSettingsPreview = useRef(false);
  const [info, setInfo] = useState<DeviceInfo>();
  const [cartridges, setCartridges] = useState<Cartridge[]>([]);
  const [assetPacks, setAssetPacks] = useState<AssetPack[]>([]);
  const [storage, setStorage] = useState<StorageInfo>();
  const [settings, setSettings] = useState<Settings>(defaultSettings);
  const [settingsReady, setSettingsReady] = useState(false);
  const [buttons, setButtons] = useState(0);
  const [remoteButtons, setRemoteButtons] = useState(0);
  const [sleeping, setSleeping] = useState(false);
  const [status, setStatus] = useState("not connected");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [installProgress, setInstallProgress] = useState<number>();
  const [installingPackage, setInstallingPackage] = useState<InstallMetadata>();
  const [compactProgress, setCompactProgress] = useState<number>();
  const [developerMode, setDeveloperMode] = useState(loadDeveloperMode);

  async function refresh(device = deviceRef.current, includeHidden = developerMode) {
    if (!device) return;
    const [apps, packs, deviceSettings, deviceStorage] = await Promise.allSettled([device.cartridges(includeHidden), device.assetPacks(), device.settings(), device.storage()]);
    if (apps.status === "fulfilled") setCartridges(apps.value);
    if (packs.status === "fulfilled") setAssetPacks(packs.value);
    if (deviceSettings.status === "fulfilled") { suppressSettingsPreview.current = true; setSettings(deviceSettings.value); setSettingsReady(true); }
    if (deviceStorage.status === "fulfilled") setStorage(deviceStorage.value);
  }

  async function refreshCartridges(device = deviceRef.current, includeHidden = developerMode) {
    if (!device) return;
    try { setCartridges(await device.cartridges(includeHidden)); }
    catch (caught) { setError(caught instanceof Error ? caught.message : "could not refresh cartridges."); }
  }

  function showLeds(colors: [string, string]) {
    colors.forEach((color, index) => {
      const led = ledRefs.current[index];
      if (!led) return;
      led.style.backgroundColor = color;
      led.style.boxShadow = `0 0 12px ${color}`;
    });
  }

  async function connect() {
    setBusy(true); setError(""); setStatus("connecting…");
    const device = new PrismDevice(); deviceRef.current = device;
    device.onMirror = frame => { screenRef.current?.draw(frame); showLeds(frame.leds); setButtons(frame.buttons); };
    device.onSleepChange = setSleeping;
    device.onLog = text => {
      if (text.includes("boot diagnostic")) {
        window.localStorage.setItem("prism:last-boot-diagnostic", text.trim());
        console.warn(`[Prism] ${text.trimEnd()}`);
      }
    };
    device.onDisconnect = () => { remoteMask.current = 0; setInfo(undefined); setStatus("not connected"); setButtons(0); setRemoteButtons(0); setSleeping(false); showLeds(["#000000", "#000000"]); };
    try { const deviceInfo = await device.connect(); setInfo(deviceInfo); setStatus("connected"); await refresh(device); await device.subscribeMirror(); }
    catch (caught) { setError(caught instanceof Error ? caught.message : "could not connect."); setStatus("not connected"); await device.close(); }
    finally { setBusy(false); }
  }

  async function disconnect() { await deviceRef.current?.close(); deviceRef.current = undefined; }

  useEffect(() => () => { void deviceRef.current?.close(); }, []);

  useEffect(() => {
    if (!info || !settingsReady || !deviceRef.current) return;
    if (suppressSettingsPreview.current) { suppressSettingsPreview.current = false; return; }
    const timer = setTimeout(() => deviceRef.current?.previewSettings(settings).catch(caught => setError(caught instanceof Error ? caught.message : "could not preview settings.")), 1000 / 60);
    return () => clearTimeout(timer);
  }, [info, settings, settingsReady]);

  async function install(file?: File) {
    if (!file || !deviceRef.current) return;
    setBusy(true); setError(""); setInstallProgress(0); setInstallingPackage(undefined);
    try { await deviceRef.current.install(file, setInstallProgress, setInstallingPackage); await refresh(); }
    catch (caught) { setError(caught instanceof Error ? caught.message : "installation failed."); }
    finally { setBusy(false); setInstallProgress(undefined); setInstallingPackage(undefined); }
  }

  async function remove(cartridge: Cartridge) {
    if (!deviceRef.current || !confirm(`delete ${cartridge.name} and its saved data?`)) return;
    setBusy(true); setError("");
    try { await deviceRef.current.deleteCartridge(cartridge.appKey); await refresh(); }
    catch (caught) { setError(caught instanceof Error ? caught.message : "delete failed."); }
    finally { setBusy(false); }
  }

  async function removeAssetPack(pack: AssetPack) {
    if (!deviceRef.current || !confirm(`delete ${pack.name}?`)) return;
    setBusy(true); setError("");
    try { await deviceRef.current.deleteAssetPack(pack.packKey); await refresh(); }
    catch (caught) { setError(caught instanceof Error ? caught.message : "delete failed."); }
    finally { setBusy(false); }
  }

  async function launch(cartridge: Cartridge) {
    if (!deviceRef.current) return;
    setBusy(true); setError("");
    try { await deviceRef.current.launchCartridge(cartridge.appKey); setStatus(`running ${cartridge.name}`); }
    catch (caught) { setError(caught instanceof Error ? caught.message : "launch failed."); }
    finally { setBusy(false); }
  }

  async function restart(bootsel: boolean) {
    if (!deviceRef.current) return;
    setBusy(true); setError(""); setStatus(bootsel ? "entering bootsel" : "restarting");
    try { await deviceRef.current.restart(bootsel); }
    catch (caught) { setError(caught instanceof Error ? caught.message : "restart failed."); }
    finally { setBusy(false); }
  }

  async function compact() {
    if (!deviceRef.current) return;
    setBusy(true); setError(""); setCompactProgress(0);
    try { await deviceRef.current.compact(setCompactProgress); await refresh(); }
    catch (caught) { setError(caught instanceof Error ? caught.message : "compaction failed."); }
    finally { setBusy(false); setCompactProgress(undefined); }
  }

  function changeLed(index: 0 | 1, next: LedSettings) { setSettings(current => { const leds = [...current.leds] as [LedSettings, LedSettings]; if (current.linked) { leds[0] = { ...next, phaseOffset: 0 }; leds[1] = { ...next, phaseOffset: current.leds[1].phaseOffset }; } else leds[index] = next; return { ...current, leds }; }); }
  function setLinked(linked: boolean) { setSettings(current => linked ? { ...current, linked, leds: [{ ...current.leds[0], phaseOffset: 0 }, { ...current.leds[0], phaseOffset: current.leds[1].phaseOffset }] } : { ...current, linked }); }
  function changeRainbowOffset(phaseOffset: number) { setSettings(current => ({ ...current, leds: [current.leds[0], { ...current.leds[1], phaseOffset }] })); }
  function changeDeveloperMode(enabled: boolean) { setDeveloperMode(enabled); saveDeveloperMode(enabled); void refreshCartridges(deviceRef.current, enabled); }

  function remote(bit: number, pressed: boolean) {
    if (pressed) remoteMask.current |= bit; else remoteMask.current &= ~bit;
    const mask = remoteMask.current;
    setRemoteButtons(mask);
    remoteQueue.current = remoteQueue.current
      .then(() => deviceRef.current?.remoteInput(mask))
      .then(() => undefined)
      .catch(() => undefined);
  }

  const storageNeedsCompaction = storage !== undefined &&
    (storage.deadBlocks > 0 || storage.largestFreeRun < storage.erasedBlocks);

  return <div className="space-y-6">
    <Card><CardHeader className="flex-row flex-wrap items-start justify-between gap-4"><div className="space-y-1.5"><CardTitle>prism</CardTitle><CardDescription>{info ? `${info.serial} · firmware ${info.firmware}` : "connect over USB to get started."}</CardDescription></div><div className="flex flex-wrap items-center justify-end gap-3"><div className="flex items-center gap-2"><Label htmlFor="developer-mode">developer mode</Label><Switch id="developer-mode" checked={developerMode} onCheckedChange={changeDeveloperMode} /></div>{info && (info.capabilities & Capability.reboot) ? <RestartControl developerMode={developerMode} busy={busy} onRestart={restart} /> : null}{info ? <Button variant="outline" onClick={disconnect}><Unplug className="size-4" />disconnect</Button> : <Button onClick={connect} disabled={busy}>{busy ? <Loader2 className="size-4 animate-spin" /> : <Cable className="size-4" />}connect</Button>}</div></CardHeader>
      {info ? <CardContent className="grid gap-3 border-t pt-5 text-sm sm:grid-cols-3"><Info label="serial number" value={info.serial} mono /><Info label="flash" value={`${(info.flashBytes / 1024 / 1024).toFixed(0)} MiB`} /><Info label="status" value={status} /></CardContent> : null}
    </Card>
    {error ? <div className="rounded-md border border-destructive/40 bg-destructive/5 px-4 py-3 text-sm text-destructive">{error}</div> : null}
    <div className="grid gap-6 lg:grid-cols-[minmax(0,1fr)_20rem]">
      <Tabs defaultValue="apps" className="min-w-0"><TabsList><TabsTrigger value="apps">cartridges</TabsTrigger><TabsTrigger value="appearance">appearance</TabsTrigger></TabsList>
        <TabsContent value="apps">
          <Card>
            <CardHeader className="space-y-4">
              <div className="flex items-center justify-between gap-4"><div><CardTitle>cartridges</CardTitle><CardDescription>apps and asset packs installed on this prism.</CardDescription></div><Button size="icon" variant="ghost" onClick={() => refresh()} disabled={!info}><RefreshCw className="size-4" /></Button></div>
              <div className="flex items-center gap-2">{storage ? <StorageMeter storage={storage} /> : <div className="h-3 min-w-0 flex-1 rounded-full border bg-muted/30" />}<Button size="sm" variant="outline" className="shrink-0" disabled={!storageNeedsCompaction || busy} onClick={() => void compact()}>{compactProgress === undefined ? "compact" : `compacting ${Math.round(compactProgress * 100)}%`}</Button></div>
            </CardHeader>
            <CardContent className="space-y-6">
              <Label className="flex min-h-24 cursor-pointer items-center justify-center rounded-md border border-dashed p-3 transition-colors hover:bg-muted/40">
                {installingPackage ? <div className="flex w-full items-center gap-4">{iconHasPixels(installingPackage.icon) ? <CartridgeIcon icon={installingPackage.icon} className="size-14 shrink-0" /> : <div className="grid size-14 shrink-0 place-items-center rounded-md border"><Package className="size-6" /></div>}<div className="min-w-0 flex-1 space-y-2"><div><p className="truncate font-medium">{installingPackage.name}</p><p className="text-xs text-muted-foreground">installing · {Math.round((installProgress ?? 0) * 100)}%</p></div><div className="h-1.5 overflow-hidden rounded-full bg-muted"><div className="h-full bg-foreground transition-[width] duration-150" style={{ width: `${Math.round((installProgress ?? 0) * 100)}%` }} /></div></div></div> : <span className="flex items-center gap-2"><Upload className="size-4" />choose a .prism or .prismpack file</span>}
                <Input type="file" accept=".prism,.prismpack" className="sr-only" disabled={!info || busy} onChange={event => void install(event.target.files?.[0])} />
              </Label>
              <div className="space-y-2">
                <Label>cartridges</Label>
                <div className="divide-y rounded-md border">{cartridges.length ? cartridges.map(app => <div key={Array.from(app.appKey, byte => byte.toString(16).padStart(2, "0")).join("")} className="flex items-center justify-between gap-4 p-3"><Button variant="ghost" className="-m-2 h-auto min-w-0 flex-1 justify-start p-2 text-left" disabled={busy} title={`launch ${app.name}`} onClick={() => void launch(app)}><CartridgeIcon icon={app.icon} className="size-12 shrink-0" /><div className="min-w-0"><p className="truncate font-medium">{app.name}</p>{developerMode ? <p className="truncate text-xs text-muted-foreground">{app.id}</p> : null}<p className="text-xs text-muted-foreground">v{app.version}{app.blocks ? ` · ${app.blocks} blocks` : " · bundled"}</p></div></Button><Button variant="ghost" size="icon" disabled={busy || Boolean(app.policy & 2)} title={app.policy & 2 ? "bundled cartridges cannot be deleted" : `delete ${app.name}`} onClick={() => void remove(app)}><Trash2 className="size-4" /></Button></div>) : <p className="p-4 text-sm text-muted-foreground">{info ? "no cartridges reported." : "connect to see installed cartridges."}</p>}</div>
              </div>
              {info && (info.capabilities & Capability.assetPacks) ? <div className="space-y-2">
                <Label>asset packs</Label>
                <div className="divide-y rounded-md border">{assetPacks.length ? assetPacks.map(pack => {
                  const target = cartridges.find(app => app.id === pack.targetId);
                  return <div key={Array.from(pack.packKey, byte => byte.toString(16).padStart(2, "0")).join("")} className="flex items-center justify-between gap-4 p-3"><div className="flex min-w-0 items-center gap-3"><CartridgeIcon icon={prismpackIcon} className="size-12 shrink-0" /><div className="min-w-0"><p className="truncate font-medium">{pack.name}</p>{developerMode ? <p className="truncate text-xs text-muted-foreground">{pack.id}</p> : null}<p className="truncate text-xs text-muted-foreground">{target?.name ?? "target not installed"} · v{pack.version} · {pack.blocks} blocks{pack.status & 1 ? " · target missing" : pack.status & 2 ? " · incompatible" : ""}</p>{developerMode ? <p className="truncate text-xs text-muted-foreground">for {pack.targetId}</p> : null}</div></div><Button variant="ghost" size="icon" disabled={busy} title={`delete ${pack.name}`} onClick={() => void removeAssetPack(pack)}><Trash2 className="size-4" /></Button></div>;
                }) : <p className="p-4 text-sm text-muted-foreground">no asset packs reported.</p>}</div>
              </div> : null}
            </CardContent>
          </Card>
        </TabsContent>
        <TabsContent value="appearance"><Card><CardHeader><CardTitle>idle appearance</CardTitle><CardDescription>changes preview immediately and save automatically on the device.</CardDescription></CardHeader><CardContent className="space-y-7"><div className="flex items-center justify-between"><div><Label>link LEDs</Label><p className="mt-1 text-sm text-muted-foreground">share effect, colors, speed, and phase.</p></div><Switch checked={settings.linked} onCheckedChange={setLinked} /></div><div className="grid gap-5 sm:grid-cols-2"><LedEditor label="left LED" value={settings.leds[0]} linkedOffset={settings.linked ? settings.leds[1].phaseOffset : undefined} onLinkedOffsetChange={changeRainbowOffset} onChange={value => changeLed(0, value)} /><LedEditor label="right LED" value={settings.leds[1]} disabled={settings.linked} onChange={value => changeLed(1, value)} /></div></CardContent></Card></TabsContent>
      </Tabs>
      <div className="space-y-4"><Card><CardHeader><CardTitle>screen</CardTitle></CardHeader><CardContent className="space-y-4"><div className="relative overflow-hidden rounded-md"><PrismScreen ref={screenRef} />{sleeping ? <div className="absolute inset-0 grid place-items-center bg-black/75 text-white"><span className="flex items-center gap-2 rounded-full border border-white/20 bg-black/60 px-3 py-1.5 text-sm font-medium"><Moon className="size-4" />sleeping</span></div> : null}</div><div className="flex justify-center gap-5"><Led elementRef={element => { ledRefs.current[0] = element; }} /><Led elementRef={element => { ledRefs.current[1] = element; }} /></div><div className="grid grid-cols-3 gap-2"><Remote label="L" active={Boolean((buttons | remoteButtons) & 1)} onPress={pressed => remote(1, pressed)} /><Remote label="menu" active={Boolean((buttons | remoteButtons) & 4)} onPress={pressed => remote(4, pressed)} /><Remote label="R" active={Boolean((buttons | remoteButtons) & 2)} onPress={pressed => remote(2, pressed)} /></div></CardContent></Card></div>
    </div>
  </div>;
}

function RestartControl({ developerMode, busy, onRestart }: { developerMode: boolean; busy: boolean; onRestart(bootsel: boolean): void }) {
  if (!developerMode)
    return <Button variant="outline" disabled={busy} onClick={() => onRestart(false)}><RotateCcw className="size-4" />restart</Button>;

  return <div className="inline-flex">
    <Button variant="outline" disabled={busy} className="rounded-r-none" onClick={() => onRestart(false)}><RotateCcw className="size-4" />restart</Button>
    <DropdownMenu><DropdownMenuTrigger asChild><Button variant="outline" size="icon" disabled={busy} className="-ml-px rounded-l-none" aria-label="restart options"><ChevronDown className="size-4" /></Button></DropdownMenuTrigger><DropdownMenuContent align="end"><DropdownMenuItem onSelect={() => onRestart(true)}><Usb className="size-4" />restart into bootsel</DropdownMenuItem></DropdownMenuContent></DropdownMenu>
  </div>;
}

function StorageMeter({ storage }: { storage: StorageInfo }) {
  const summary = `${storage.liveBlocks} live, ${storage.erasedBlocks} free, ${storage.deadBlocks} reclaimable blocks`;

  return <div className="group relative min-w-0 flex-1">
    <button type="button" className="block w-full rounded-md focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2" aria-label={summary}>
      <div className="grid h-8 overflow-hidden rounded-md border bg-border" style={{ gridTemplateColumns: `repeat(${storage.totalBlocks}, minmax(0, 1fr))`, gap: "1px" }} role="img" aria-label={summary}>
        {Array.from(storage.blockStates, (state, index) => <span key={index} className={state === 1 ? "bg-foreground" : state === 2 ? "[background-image:repeating-linear-gradient(135deg,var(--muted)_0_3px,var(--border)_3px_5px)]" : "bg-background"} title={`block ${index + 1}: ${state === 1 ? "live" : state === 2 ? "reclaimable" : "free"}`} />)}
      </div>
    </button>
    <div className="pointer-events-none invisible absolute left-0 top-full z-30 mt-2 w-64 translate-y-1 rounded-lg border bg-card p-3 text-sm text-card-foreground opacity-0 shadow-lg transition-[opacity,transform,visibility] duration-150 group-hover:pointer-events-auto group-hover:visible group-hover:translate-y-0 group-hover:opacity-100 group-focus-within:pointer-events-auto group-focus-within:visible group-focus-within:translate-y-0 group-focus-within:opacity-100">
      <p className="mb-3 font-medium">storage</p>
      <div className="space-y-2">
        <StorageDetail label="live" value={storage.liveBlocks} kind="live" />
        <StorageDetail label="reclaimable" value={storage.deadBlocks} kind="dead" />
        <StorageDetail label="free" value={storage.erasedBlocks} kind="free" />
        <div className="border-t pt-2"><Info label="largest free run" value={`${storage.largestFreeRun} blocks`} /></div>
      </div>
    </div>
  </div>;
}

function StorageDetail({ label, value, kind }: { label: string; value: number; kind: "live" | "free" | "dead" }) {
  const swatch = kind === "live"
    ? "bg-foreground"
    : kind === "free"
      ? "bg-background"
      : "[background-image:repeating-linear-gradient(135deg,var(--muted)_0_4px,var(--border)_4px_7px)]";
  return <div className="flex items-center justify-between gap-4"><span className="flex items-center gap-2 text-muted-foreground"><span className={`size-2.5 rounded-sm border ${swatch}`} />{label}</span><span className="font-medium">{value} blocks</span></div>;
}

function CartridgeIcon({ icon, className = "" }: { icon: Uint8Array; className?: string }) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  useEffect(() => {
    const canvas = canvasRef.current;
    const context = canvas?.getContext("2d");
    if (!canvas || !context) return;
    context.clearRect(0, 0, 36, 36);
    context.fillStyle = getComputedStyle(canvas).color;
    for (let y = 0; y < 36; y++)
      for (let x = 0; x < 36; x++)
        if ((icon[y * 5 + (x >> 3)] & (1 << (x & 7))) !== 0)
          context.fillRect(x, y, 1, 1);
  }, [icon]);
  return <span className={`grid place-items-center rounded-md border bg-background p-1 text-foreground ${className}`}><canvas ref={canvasRef} width={36} height={36} className="size-full [image-rendering:pixelated]" aria-label="cartridge icon" /></span>;
}

function iconHasPixels(icon: Uint8Array) { return icon.some(byte => byte !== 0); }

function Info({ label, value, mono = false }: { label: string; value: string; mono?: boolean }) { return <div className="flex justify-between gap-4 sm:block"><span className="text-muted-foreground">{label}</span><p className={mono ? "font-mono" : "font-medium"}>{value}</p></div>; }
function Led({ elementRef }: { elementRef(element: HTMLSpanElement | null): void }) { return <span ref={elementRef} className="size-4 rounded-full border shadow-sm" style={{ backgroundColor: "#000000", boxShadow: "0 0 12px #000000" }} />; }
function Remote({ label, active, onPress }: { label: string; active: boolean; onPress(pressed: boolean): void }) { return <Button variant={active ? "default" : "outline"} disabled={false} onPointerDown={event => { event.currentTarget.setPointerCapture(event.pointerId); onPress(true); }} onPointerUp={() => onPress(false)} onPointerCancel={() => onPress(false)}>{label}</Button>; }

function LedEditor({ label, value, disabled, linkedOffset, onLinkedOffsetChange, onChange }: { label: string; value: LedSettings; disabled?: boolean; linkedOffset?: number; onLinkedOffsetChange?(offset: number): void; onChange(value: LedSettings): void }) {
  const configurable = value.effect !== 3;
  const animated = value.effect === 1 || value.effect === 2 || value.effect === 3;
  return <fieldset disabled={disabled} className="space-y-4 rounded-lg border p-4 disabled:opacity-50"><Label>{label}</Label><Select value={String(value.effect)} onValueChange={effect => onChange({ ...value, effect: Number(effect) as LedEffect, colors: Number(effect) === 2 ? value.colors : [value.colors[0] ?? "#ffffff"] })}><SelectTrigger><SelectValue /></SelectTrigger><SelectContent>{effects.map(effect => <SelectItem key={effect.value} value={effect.value}>{effect.label}</SelectItem>)}</SelectContent></Select>{configurable ? <div className="space-y-2"><Label>color{value.effect === 2 ? "s" : ""}</Label><div className="flex flex-wrap gap-2">{value.colors.map((color, index) => <div key={index} className="flex items-center gap-1"><Input type="color" value={color} onChange={event => onChange({ ...value, colors: value.colors.map((item, itemIndex) => itemIndex === index ? event.target.value : item) })} className="size-9 cursor-pointer p-1" />{value.effect === 2 && value.colors.length > 2 ? <Button type="button" size="icon" variant="ghost" onClick={() => onChange({ ...value, colors: value.colors.filter((_, itemIndex) => itemIndex !== index) })}><Trash2 className="size-3" /></Button> : null}</div>)}{value.effect === 2 && value.colors.length < 16 ? <Button type="button" variant="outline" size="sm" onClick={() => onChange({ ...value, colors: [...value.colors, value.colors.at(-1) ?? "#ffffff"] })}>add</Button> : null}</div></div> : null}{animated ? <div className="space-y-2"><div className="flex justify-between"><Label>speed</Label><span className="text-xs text-muted-foreground">{(value.speedMs / 1000).toFixed(1)} s</span></div><Slider min={250} max={10000} step={250} value={[value.speedMs]} onValueChange={speed => onChange({ ...value, speedMs: speed[0] })} /></div> : null}{value.effect === 3 && linkedOffset !== undefined ? <div className="space-y-2"><div className="flex justify-between"><Label>offset</Label><span className="text-xs text-muted-foreground">{Math.round(linkedOffset * 360 / 256)}°</span></div><Slider min={0} max={255} step={1} value={[linkedOffset]} onValueChange={offset => onLinkedOffsetChange?.(offset[0])} /></div> : null}</fieldset>;
}
