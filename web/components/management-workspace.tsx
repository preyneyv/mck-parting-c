"use client";

import { useEffect, useRef, useState } from "react";
import { Cable, Loader2, Moon, RefreshCw, Trash2, Unplug, Upload } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Card, CardContent, CardDescription, CardHeader, CardTitle } from "@/components/ui/card";
import { Input } from "@/components/ui/input";
import { Label } from "@/components/ui/label";
import { Select, SelectContent, SelectItem, SelectTrigger, SelectValue } from "@/components/ui/select";
import { Slider } from "@/components/ui/slider";
import { Switch } from "@/components/ui/switch";
import { Tabs, TabsContent, TabsList, TabsTrigger } from "@/components/ui/tabs";
import { PrismScreen, type PrismScreenHandle } from "@/components/prism-screen";
import { PrismDevice, type Cartridge, type CartridgeMetadata, type DeviceInfo, type LedEffect, type LedSettings, type Settings, type StorageInfo } from "@/lib/prism-device";

const defaultSettings: Settings = { volume: 4, brightness: 8, linked: true, leds: [{ effect: 0, speedMs: 2000, colors: ["#1860ff"] }, { effect: 0, speedMs: 2000, colors: ["#1860ff"] }] };
const effects = [{ value: "0", label: "Static" }, { value: "1", label: "Breathing" }, { value: "2", label: "Crossfade" }, { value: "3", label: "Rainbow" }];

export function ManagementWorkspace() {
  const deviceRef = useRef<PrismDevice | undefined>(undefined);
  const screenRef = useRef<PrismScreenHandle>(null);
  const remoteMask = useRef(0);
  const remoteQueue = useRef<Promise<void>>(Promise.resolve());
  const suppressSettingsPreview = useRef(false);
  const [info, setInfo] = useState<DeviceInfo>();
  const [cartridges, setCartridges] = useState<Cartridge[]>([]);
  const [storage, setStorage] = useState<StorageInfo>();
  const [settings, setSettings] = useState<Settings>(defaultSettings);
  const [settingsReady, setSettingsReady] = useState(false);
  const [leds, setLeds] = useState<[string, string]>(["#000000", "#000000"]);
  const [buttons, setButtons] = useState(0);
  const [remoteButtons, setRemoteButtons] = useState(0);
  const [sleeping, setSleeping] = useState(false);
  const [status, setStatus] = useState("Not connected");
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [installProgress, setInstallProgress] = useState<number>();
  const [installingCartridge, setInstallingCartridge] = useState<CartridgeMetadata>();
  const [compactProgress, setCompactProgress] = useState<number>();

  async function refresh(device = deviceRef.current) {
    if (!device) return;
    const [apps, deviceSettings, deviceStorage] = await Promise.allSettled([device.cartridges(), device.settings(), device.storage()]);
    if (apps.status === "fulfilled") setCartridges(apps.value);
    if (deviceSettings.status === "fulfilled") { suppressSettingsPreview.current = true; setSettings(deviceSettings.value); setSettingsReady(true); }
    if (deviceStorage.status === "fulfilled") setStorage(deviceStorage.value);
  }

  async function connect() {
    setBusy(true); setError(""); setStatus("Connecting…");
    const device = new PrismDevice(); deviceRef.current = device;
    device.onMirror = frame => { screenRef.current?.draw(frame); setLeds(frame.leds); setButtons(frame.buttons); };
    device.onSleepChange = setSleeping;
    device.onLog = text => {
      if (text.includes("boot diagnostic")) console.warn(`[Prism] ${text.trimEnd()}`);
    };
    device.onDisconnect = () => { remoteMask.current = 0; setInfo(undefined); setStatus("Not connected"); setButtons(0); setRemoteButtons(0); setSleeping(false); };
    try { const deviceInfo = await device.connect(); setInfo(deviceInfo); setStatus("Connected"); await refresh(device); await device.subscribeMirror(); }
    catch (caught) { setError(caught instanceof Error ? caught.message : "Could not connect."); setStatus("Not connected"); await device.close(); }
    finally { setBusy(false); }
  }

  async function disconnect() { await deviceRef.current?.close(); deviceRef.current = undefined; }

  useEffect(() => () => { void deviceRef.current?.close(); }, []);

  useEffect(() => {
    if (!info || !settingsReady || !deviceRef.current) return;
    if (suppressSettingsPreview.current) { suppressSettingsPreview.current = false; return; }
    const timer = setTimeout(() => deviceRef.current?.previewSettings(settings).catch(caught => setError(caught instanceof Error ? caught.message : "Could not preview settings.")), 100);
    return () => clearTimeout(timer);
  }, [info, settings, settingsReady]);

  async function install(file?: File) {
    if (!file || !deviceRef.current) return;
    setBusy(true); setError(""); setInstallProgress(0); setInstallingCartridge(undefined);
    try { await deviceRef.current.install(file, setInstallProgress, setInstallingCartridge); await refresh(); }
    catch (caught) { setError(caught instanceof Error ? caught.message : "Installation failed."); }
    finally { setBusy(false); setInstallProgress(undefined); setInstallingCartridge(undefined); }
  }

  async function remove(cartridge: Cartridge) {
    if (!deviceRef.current || !confirm(`Delete ${cartridge.name} and its saved data?`)) return;
    setBusy(true); setError("");
    try { await deviceRef.current.deleteCartridge(cartridge.uuid); await refresh(); }
    catch (caught) { setError(caught instanceof Error ? caught.message : "Delete failed."); }
    finally { setBusy(false); }
  }

  async function compact() {
    if (!deviceRef.current) return;
    setBusy(true); setError(""); setCompactProgress(0);
    try { await deviceRef.current.compact(setCompactProgress); await refresh(); }
    catch (caught) { setError(caught instanceof Error ? caught.message : "Compaction failed."); }
    finally { setBusy(false); setCompactProgress(undefined); }
  }

  function changeLed(index: 0 | 1, next: LedSettings) { setSettings(current => { const leds = [...current.leds] as [LedSettings, LedSettings]; leds[index] = next; if (current.linked) leds[1] = next; return { ...current, leds }; }); }

  function remote(bit: number, pressed: boolean) {
    if (pressed) remoteMask.current |= bit; else remoteMask.current &= ~bit;
    const mask = remoteMask.current;
    setRemoteButtons(mask);
    remoteQueue.current = remoteQueue.current
      .then(() => deviceRef.current?.remoteInput(mask))
      .then(() => undefined)
      .catch(() => undefined);
  }

  return <div className="space-y-6">
    <Card><CardHeader className="flex-row items-start justify-between gap-4"><div className="space-y-1.5"><CardTitle>Prism</CardTitle><CardDescription>{info ? `${info.serial} · firmware ${info.firmware}` : "Connect over USB to get started."}</CardDescription></div>{info ? <Button variant="outline" onClick={disconnect}><Unplug className="size-4" />Disconnect</Button> : <Button onClick={connect} disabled={busy}>{busy ? <Loader2 className="size-4 animate-spin" /> : <Cable className="size-4" />}Connect</Button>}</CardHeader>
      {info ? <CardContent className="grid gap-3 border-t pt-5 text-sm sm:grid-cols-3"><Info label="Serial number" value={info.serial} mono /><Info label="Flash" value={`${(info.flashBytes / 1024 / 1024).toFixed(0)} MiB`} /><Info label="Status" value={status} /></CardContent> : null}
    </Card>
    {error ? <div className="rounded-md border border-destructive/40 bg-destructive/5 px-4 py-3 text-sm text-destructive">{error}</div> : null}
    <div className="grid gap-6 lg:grid-cols-[minmax(0,1fr)_20rem]">
      <Tabs defaultValue="apps" className="min-w-0"><TabsList><TabsTrigger value="apps">Cartridges</TabsTrigger><TabsTrigger value="appearance">Appearance</TabsTrigger></TabsList>
        <TabsContent value="apps"><Card><CardHeader className="space-y-4"><div className="flex items-center justify-between gap-4"><div><CardTitle>Cartridges</CardTitle><CardDescription>Apps currently installed on this Prism.</CardDescription></div><Button size="icon" variant="ghost" onClick={() => refresh()} disabled={!info}><RefreshCw className="size-4" /></Button></div><div className="flex items-center gap-2">{storage ? <StorageMeter storage={storage} /> : <div className="h-3 min-w-0 flex-1 rounded-full border bg-muted/30" />}<Button size="sm" variant="outline" className="shrink-0" disabled={!storage || storage.deadBlocks === 0 || busy} onClick={() => void compact()}>{compactProgress === undefined ? "Compact" : `Compacting ${Math.round(compactProgress * 100)}%`}</Button></div></CardHeader><CardContent className="space-y-4"><Label className="flex min-h-24 cursor-pointer items-center justify-center rounded-md border border-dashed p-3 transition-colors hover:bg-muted/40">{installingCartridge ? <div className="flex w-full items-center gap-4"><CartridgeIcon icon={installingCartridge.icon} className="size-14 shrink-0" /><div className="min-w-0 flex-1 space-y-2"><div><p className="truncate font-medium">{installingCartridge.name}</p><p className="text-xs text-muted-foreground">Installing · {Math.round((installProgress ?? 0) * 100)}%</p></div><div className="h-1.5 overflow-hidden rounded-full bg-muted"><div className="h-full bg-foreground transition-[width] duration-150" style={{ width: `${Math.round((installProgress ?? 0) * 100)}%` }} /></div></div></div> : <span className="flex items-center gap-2"><Upload className="size-4" />Choose a .prism file</span>}<Input type="file" accept=".prism" className="sr-only" disabled={!info || busy} onChange={event => void install(event.target.files?.[0])} /></Label><div className="divide-y rounded-md border">{cartridges.length ? cartridges.map(app => <div key={app.slug} className="flex items-center justify-between gap-4 p-3"><div className="flex min-w-0 items-center gap-3"><CartridgeIcon icon={app.icon} className="size-12 shrink-0" /><div className="min-w-0"><p className="truncate font-medium">{app.name}</p><p className="truncate text-xs text-muted-foreground">{app.slug}{app.blocks ? ` · ${app.blocks} blocks` : " · bundled"}</p></div></div><Button variant="ghost" size="icon" disabled={busy || Boolean(app.policy & 2)} title={app.policy & 2 ? "Bundled cartridges cannot be deleted" : `Delete ${app.name}`} onClick={() => void remove(app)}><Trash2 className="size-4" /></Button></div>) : <p className="p-4 text-sm text-muted-foreground">{info ? "No cartridges reported." : "Connect to see installed cartridges."}</p>}</div></CardContent></Card></TabsContent>
        <TabsContent value="appearance"><Card><CardHeader><CardTitle>Idle appearance</CardTitle><CardDescription>Changes preview immediately and save automatically on the device.</CardDescription></CardHeader><CardContent className="space-y-7"><div className="flex items-center justify-between"><div><Label>Link LEDs</Label><p className="mt-1 text-sm text-muted-foreground">Share effect, colors, speed, and phase.</p></div><Switch checked={settings.linked} onCheckedChange={linked => setSettings(current => ({ ...current, linked, leds: linked ? [current.leds[0], current.leds[0]] : current.leds }))} /></div><div className="grid gap-5 sm:grid-cols-2"><LedEditor label="Left LED" value={settings.leds[0]} onChange={value => changeLed(0, value)} /><LedEditor label="Right LED" value={settings.leds[1]} disabled={settings.linked} onChange={value => changeLed(1, value)} /></div></CardContent></Card></TabsContent>
      </Tabs>
      <div className="space-y-4"><Card><CardHeader><CardTitle>Screen</CardTitle></CardHeader><CardContent className="space-y-4"><div className="relative overflow-hidden rounded-md"><PrismScreen ref={screenRef} />{sleeping ? <div className="absolute inset-0 grid place-items-center bg-black/75 text-white"><span className="flex items-center gap-2 rounded-full border border-white/20 bg-black/60 px-3 py-1.5 text-sm font-medium"><Moon className="size-4" />Sleeping</span></div> : null}</div><div className="flex justify-center gap-5"><Led color={leds[0]} /><Led color={leds[1]} /></div><div className="grid grid-cols-3 gap-2"><Remote label="L" active={Boolean((buttons | remoteButtons) & 1)} onPress={pressed => remote(1, pressed)} /><Remote label="Menu" active={Boolean((buttons | remoteButtons) & 4)} onPress={pressed => remote(4, pressed)} /><Remote label="R" active={Boolean((buttons | remoteButtons) & 2)} onPress={pressed => remote(2, pressed)} /></div></CardContent></Card></div>
    </div>
  </div>;
}

function StorageMeter({ storage }: { storage: StorageInfo }) {
  const summary = `${storage.liveBlocks} live, ${storage.erasedBlocks} free, ${storage.deadBlocks} reclaimable blocks`;

  return <div className="group relative min-w-0 flex-1">
    <button type="button" className="block w-full rounded-md focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring focus-visible:ring-offset-2" aria-label={summary}>
      <div className="grid h-8 overflow-hidden rounded-md border bg-border" style={{ gridTemplateColumns: `repeat(${storage.totalBlocks}, minmax(0, 1fr))`, gap: "1px" }} role="img" aria-label={summary}>
        {Array.from(storage.blockStates, (state, index) => <span key={index} className={state === 1 ? "bg-foreground" : state === 2 ? "[background-image:repeating-linear-gradient(135deg,var(--muted)_0_3px,var(--border)_3px_5px)]" : "bg-background"} title={`Block ${index + 1}: ${state === 1 ? "live" : state === 2 ? "reclaimable" : "free"}`} />)}
      </div>
    </button>
    <div className="pointer-events-none invisible absolute left-0 top-full z-30 mt-2 w-64 translate-y-1 rounded-lg border bg-card p-3 text-sm text-card-foreground opacity-0 shadow-lg transition-[opacity,transform,visibility] duration-150 group-hover:pointer-events-auto group-hover:visible group-hover:translate-y-0 group-hover:opacity-100 group-focus-within:pointer-events-auto group-focus-within:visible group-focus-within:translate-y-0 group-focus-within:opacity-100">
      <p className="mb-3 font-medium">Storage</p>
      <div className="space-y-2">
        <StorageDetail label="Live" value={storage.liveBlocks} kind="live" />
        <StorageDetail label="Reclaimable" value={storage.deadBlocks} kind="dead" />
        <StorageDetail label="Free" value={storage.erasedBlocks} kind="free" />
        <div className="border-t pt-2"><Info label="Largest free run" value={`${storage.largestFreeRun} blocks`} /></div>
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
  return <span className={`grid place-items-center rounded-md border bg-background p-1 text-foreground ${className}`}><canvas ref={canvasRef} width={36} height={36} className="size-full [image-rendering:pixelated]" aria-label="Cartridge icon" /></span>;
}

function Info({ label, value, mono = false }: { label: string; value: string; mono?: boolean }) { return <div className="flex justify-between gap-4 sm:block"><span className="text-muted-foreground">{label}</span><p className={mono ? "font-mono" : "font-medium"}>{value}</p></div>; }
function Led({ color }: { color: string }) { return <span className="size-4 rounded-full border shadow-sm" style={{ backgroundColor: color, boxShadow: `0 0 12px ${color}` }} />; }
function Remote({ label, active, onPress }: { label: string; active: boolean; onPress(pressed: boolean): void }) { return <Button variant={active ? "default" : "outline"} disabled={false} onPointerDown={event => { event.currentTarget.setPointerCapture(event.pointerId); onPress(true); }} onPointerUp={() => onPress(false)} onPointerCancel={() => onPress(false)}>{label}</Button>; }

function LedEditor({ label, value, disabled, onChange }: { label: string; value: LedSettings; disabled?: boolean; onChange(value: LedSettings): void }) {
  const configurable = value.effect !== 3;
  return <fieldset disabled={disabled} className="space-y-4 rounded-lg border p-4 disabled:opacity-50"><Label>{label}</Label><Select value={String(value.effect)} onValueChange={effect => onChange({ ...value, effect: Number(effect) as LedEffect, colors: Number(effect) === 2 ? value.colors : [value.colors[0] ?? "#ffffff"] })}><SelectTrigger><SelectValue /></SelectTrigger><SelectContent>{effects.map(effect => <SelectItem key={effect.value} value={effect.value}>{effect.label}</SelectItem>)}</SelectContent></Select>{configurable ? <div className="space-y-2"><Label>Color{value.effect === 2 ? "s" : ""}</Label><div className="flex flex-wrap gap-2">{value.colors.map((color, index) => <div key={index} className="flex items-center gap-1"><Input type="color" value={color} onChange={event => onChange({ ...value, colors: value.colors.map((item, itemIndex) => itemIndex === index ? event.target.value : item) })} className="size-9 cursor-pointer p-1" />{value.effect === 2 && value.colors.length > 2 ? <Button type="button" size="icon" variant="ghost" onClick={() => onChange({ ...value, colors: value.colors.filter((_, itemIndex) => itemIndex !== index) })}><Trash2 className="size-3" /></Button> : null}</div>)}{value.effect === 2 && value.colors.length < 16 ? <Button type="button" variant="outline" size="sm" onClick={() => onChange({ ...value, colors: [...value.colors, value.colors.at(-1) ?? "#ffffff"] })}>Add</Button> : null}</div></div> : null}{value.effect === 1 || value.effect === 2 ? <div className="space-y-2"><div className="flex justify-between"><Label>Speed</Label><span className="text-xs text-muted-foreground">{(value.speedMs / 1000).toFixed(1)} s</span></div><Slider min={250} max={10000} step={250} value={[value.speedMs]} onValueChange={speed => onChange({ ...value, speedMs: speed[0] })} /></div> : null}</fieldset>;
}
