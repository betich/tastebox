import { useCallback, useEffect, useRef, useState } from "react"
import { useFetcher, useNavigate } from "react-router"
import type { ActionFunctionArgs } from "react-router"
import { useGamepadInput } from "../lib/useGamepad"

// ── Types ─────────────────────────────────────────────────────────────────────

type Device = "cooker" | "plating" | "ingredient" | "cutter"

interface StatusData {
  ok: boolean
  cooker?:     { online: boolean; on: boolean; position: number }
  plating?:    { online: boolean; m1_busy: boolean; m2_busy: boolean; m1_pos: number; arm: number; lid: number; lid_busy: boolean }
  ingredient?: { online: boolean; a_busy: boolean; b_busy: boolean; c_busy: boolean }
  cutter?:     { online: boolean; door_busy: boolean; clamp_busy: boolean; roller_busy: boolean; scissor_busy: boolean }
}

// ── Loader ────────────────────────────────────────────────────────────────────

export async function loader() {
  const API = process.env.CONTROLLER_API_URL ?? "http://localhost:5000"
  try {
    const res = await fetch(`${API}/status`, { signal: AbortSignal.timeout(2000) })
    const data: StatusData = await res.json()
    return { status: data, online: true }
  } catch {
    return { status: null as StatusData | null, online: false }
  }
}

// ── Action — proxy commands to Python controller ──────────────────────────────

export async function action({ request }: ActionFunctionArgs) {
  const API  = process.env.CONTROLLER_API_URL ?? "http://localhost:5000"
  const form = await request.formData()
  const device  = form.get("device")  as Device
  const command = form.get("command") as string
  const value   = parseFloat(form.get("value") as string ?? "0")

  const post = (path: string, body?: object) =>
    fetch(`${API}${path}`, {
      method: "POST",
      ...(body ? { headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) } : {}),
    })

  try {
    if (device === "cooker") {
      if (command === "click")          await post("/cooker/click")
      if (command === "reset")          await post("/cooker/reset")
      if (command === "position_delta") await post("/cooker/position", { delta: value })
      if (command === "set_position")   await post("/cooker/position", { position: Math.round(value) })
    } else if (device === "plating") {
      if (command === "dispense")    await post("/plating/arm",  { action: "dispense" })
      if (command === "retract")     await post("/plating/arm",  { action: "retract" })
      if (command === "fwd_cont")    await post("/plating/arm",  { action: "fwd_cont" })
      if (command === "bwd_cont")    await post("/plating/arm",  { action: "bwd_cont" })
      if (command === "home_pan")    await post("/plating/home")
      if (command === "stop_arm")    await post("/plating/arm",  { action: "stop" })
      if (command === "move_pan")    await post("/plating/move", { m1: Math.round(value), m2: 0 })
      if (command === "arm_dur")     await post("/plating/arm",  { duration_ms: Math.round(value), action: "stop" })
      if (command === "lid_open")    await post("/plating/lid",  { action: "open" })
      if (command === "lid_close")   await post("/plating/lid",  { action: "close" })
      if (command === "lid_fwd")     await post("/plating/lid",  { action: "fwd_cont" })
      if (command === "lid_bwd")     await post("/plating/lid",  { action: "bwd_cont" })
      if (command === "stop_lid")    await post("/plating/lid",  { action: "stop" })
      if (command === "lid_dur")     await post("/plating/lid",  { duration_ms: Math.round(value), action: "stop" })
    } else if (device === "ingredient") {
      if (command === "a_fwd")      await post("/ingredient/a/fwd")
      if (command === "a_bwd")      await post("/ingredient/a/bwd")
      if (command === "a_dispense") await post("/ingredient/a/dispense")
      if (command === "a_retract")  await post("/ingredient/a/retract")
      if (command === "a_stop")     await post("/ingredient/a/stop")
      if (command === "b_fwd")      await post("/ingredient/b/fwd")
      if (command === "b_bwd")      await post("/ingredient/b/bwd")
      if (command === "b_dispense") await post("/ingredient/b/dispense")
      if (command === "b_retract")  await post("/ingredient/b/retract")
      if (command === "b_stop")     await post("/ingredient/b/stop")
      if (command === "c_fwd")      await post("/ingredient/c/fwd")
      if (command === "c_bwd")      await post("/ingredient/c/bwd")
      if (command === "c_dispense") await post("/ingredient/c/dispense")
      if (command === "c_retract")  await post("/ingredient/c/retract")
      if (command === "c_stop")     await post("/ingredient/c/stop")
      if (command === "stop")       await post("/ingredient/stop")
      if (command === "set_rev")    await post("/ingredient/revolutions", { steps: Math.round(value) })
    } else if (device === "cutter") {
      if (command === "door_open")       await post("/cutter/door",    { action: "open" })
      if (command === "door_close")      await post("/cutter/door",    { action: "close" })
      if (command === "clamp")           await post("/cutter/clamp",   { action: "clamp" })
      if (command === "release")         await post("/cutter/clamp",   { action: "release" })
      if (command === "roller_fwd")      await post("/cutter/roller",  { action: "fwd" })
      if (command === "roller_rev")      await post("/cutter/roller",  { action: "rev" })
      if (command === "roller_stop")     await post("/cutter/roller",  { action: "stop" })
      if (command === "scissor_fwd")     await post("/cutter/scissor", { action: "fwd" })
      if (command === "scissor_rev")     await post("/cutter/scissor", { action: "rev" })
      if (command === "scissor_stop")    await post("/cutter/scissor", { action: "stop" })
      if (command === "pepper_dispense") await post("/cutter/pepper",  { action: "dispense" })
      if (command === "pump_on")         await post("/cutter/pump",    { action: "on" })
      if (command === "pump_off")        await post("/cutter/pump",    { action: "off" })
      if (command === "salt_dispense")   await post("/cutter/salt",    { action: "dispense" })
    } else if (device === "system") {
      if (command === "ping") {
        const res  = await fetch(`${API}/ping`, { signal: AbortSignal.timeout(5000) })
        const data = await res.json()
        return { ok: true, ping: data }
      }
      if (command === "set_state")
        await post("/state", { state: form.get("state_name") as string })
    }
    return { ok: true }
  } catch (err) {
    return { ok: false, error: String(err) }
  }
}

export function meta() {
  return [{ title: "Tastebox — Admin" }]
}

// ── Joystick component ────────────────────────────────────────────────────────

function Joystick({
  label,
  onValue,
  onCommit,
  description,
}: {
  label: string
  onValue: (v: number) => void
  onCommit?: (v: number) => void
  description?: string
}) {
  const containerRef = useRef<HTMLDivElement>(null)
  const [knob, setKnob]       = useState({ x: 0, y: 0 })
  const [display, setDisplay] = useState(50)
  const dragging = useRef(false)
  const lastVal  = useRef(50)  // sync ref so onCommit always gets the real value
  const R = 52  // container radius

  const updateFromPointer = useCallback((e: React.PointerEvent) => {
    if (!containerRef.current) return
    const rect = containerRef.current.getBoundingClientRect()
    const cx   = rect.left + rect.width  / 2
    const cy   = rect.top  + rect.height / 2
    const dx   = e.clientX - cx
    const dy   = e.clientY - cy
    const dist = Math.hypot(dx, dy)
    const r    = Math.min(dist, R)
    const ang  = Math.atan2(dy, dx)
    const pos  = { x: Math.cos(ang) * r, y: Math.sin(ang) * r }
    setKnob(pos)
    // Y axis: top → 100, center → 50, bottom → 0
    const val = Math.round(50 - (pos.y / R) * 50)
    lastVal.current = val
    setDisplay(val)
    onValue(val)
  }, [onValue, R])

  const handlePointerDown = (e: React.PointerEvent) => {
    e.currentTarget.setPointerCapture(e.pointerId)
    dragging.current = true
    updateFromPointer(e)
  }
  const handlePointerMove = (e: React.PointerEvent) => {
    if (!dragging.current) return
    updateFromPointer(e)
  }
  const handlePointerUp = () => {
    dragging.current = false
    onCommit?.(lastVal.current)
    lastVal.current = 50
    setKnob({ x: 0, y: 0 })
    setDisplay(50)
    onValue(50)
  }

  return (
    <div className="flex flex-col items-center gap-3">
      <div
        ref={containerRef}
        className="relative rounded-full bg-neutral-800 border-2 border-neutral-600 cursor-pointer select-none"
        style={{ width: R * 2, height: R * 2 }}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onPointerCancel={handlePointerUp}
      >
        {/* crosshairs */}
        <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
          <div className="w-full h-px bg-neutral-700" />
        </div>
        <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
          <div className="h-full w-px bg-neutral-700" />
        </div>
        {/* knob */}
        <div
          className="absolute rounded-full bg-neutral-300 pointer-events-none"
          style={{
            width: 22, height: 22,
            left: R - 11 + knob.x,
            top:  R - 11 + knob.y,
          }}
        />
      </div>
      <span className="text-neutral-400 text-[18px]">{label}: <span className="text-white font-mono">{display}</span></span>
      {description && <span className="text-neutral-500 text-[13px] text-center max-w-[140px] leading-tight">{description}</span>}
    </div>
  )
}

// ── D-pad ─────────────────────────────────────────────────────────────────────

function DPad({
  onPress,
  labels,
}: {
  onPress: (dir: "up" | "down" | "left" | "right") => void
  labels?: Partial<Record<"up" | "down" | "left" | "right", string>>
}) {
  const btn = (dir: "up" | "down" | "left" | "right", arrow: string, cls: string) => (
    <button
      onClick={() => onPress(dir)}
      className={`w-16 h-16 rounded-lg bg-neutral-700 active:bg-neutral-500 flex flex-col items-center justify-center text-white select-none ${cls}`}
    >
      <span className="text-[22px] font-bold">{arrow}</span>
      {labels?.[dir] && <span className="text-[9px] leading-none text-center px-0.5 opacity-80">{labels[dir]}</span>}
    </button>
  )
  return (
    <div className="relative" style={{ width: 192, height: 192 }}>
      <div className="absolute inset-0 flex items-center justify-center">
        <div className="w-16 h-16 rounded-lg bg-neutral-800 border border-neutral-600" />
      </div>
      {/* up */}
      <div className="absolute top-0 left-0 right-0 flex justify-center">
        {btn("up", "▲", "")}
      </div>
      {/* down */}
      <div className="absolute bottom-0 left-0 right-0 flex justify-center">
        {btn("down", "▼", "")}
      </div>
      {/* left */}
      <div className="absolute top-0 bottom-0 left-0 flex items-center">
        {btn("left", "◀", "")}
      </div>
      {/* right */}
      <div className="absolute top-0 bottom-0 right-0 flex items-center">
        {btn("right", "▶", "")}
      </div>
    </div>
  )
}

// ── Face buttons (ABXY) ───────────────────────────────────────────────────────

function FaceButtons({ labels, onPress }: {
  labels: Record<"a" | "b" | "x" | "y", string | null>
  onPress: (btn: "a" | "b" | "x" | "y") => void
}) {
  const COLORS = { a: "#22c55e", b: "#ef4444", x: "#3b82f6", y: "#eab308" }
  const btn = (id: "a" | "b" | "x" | "y", pos: string) => {
    const label = labels[id]
    return (
      <button
        key={id}
        onClick={() => label && onPress(id)}
        disabled={!label}
        className={`w-16 h-16 rounded-full flex flex-col items-center justify-center text-white text-[11px] font-bold gap-0.5 select-none ${pos} ${label ? "active:opacity-60" : "opacity-20"}`}
        style={{ background: COLORS[id] }}
      >
        <span className="text-[18px] font-black">{id.toUpperCase()}</span>
        {label && <span className="text-[10px] leading-tight text-center px-1 opacity-90">{label}</span>}
      </button>
    )
  }
  return (
    <div className="relative" style={{ width: 192, height: 192 }}>
      <div className="absolute top-0 left-0 right-0 flex justify-center">{btn("y", "")}</div>
      <div className="absolute bottom-0 left-0 right-0 flex justify-center">{btn("a", "")}</div>
      <div className="absolute top-0 bottom-0 left-0 flex items-center">{btn("x", "")}</div>
      <div className="absolute top-0 bottom-0 right-0 flex items-center">{btn("b", "")}</div>
    </div>
  )
}

// ── Status display ────────────────────────────────────────────────────────────

function StatusRow({ status, device }: { status: StatusData | null; device: Device }) {
  if (!status?.ok) return <span className="text-red-400 text-[18px]">Controller offline</span>
  const ARM_LABELS = ["at A", "at B", "moving"]
  const data = status[device]
  if (!data) return <span className="text-neutral-500 text-[18px]">—</span>
  if (!data.online) return <span className="text-yellow-400 text-[18px]">device offline</span>

  if (device === "cooker") {
    const d = data as NonNullable<StatusData["cooker"]>
    return (
      <div className="flex gap-6 text-[18px] text-neutral-300 font-mono">
        <span>pos <strong className="text-white">{d.position}</strong></span>
        <span>power <strong className={d.on ? "text-green-400" : "text-neutral-500"}>{d.on ? "ON" : "off"}</strong></span>
      </div>
    )
  }
  if (device === "plating") {
    const d = data as NonNullable<StatusData["plating"]>
    const LID_LABELS = ["closed", "open", "moving"]
    return (
      <div className="flex gap-6 text-[18px] text-neutral-300 font-mono">
        <span>pan <strong className="text-white">{d.m1_pos}</strong>{d.m1_busy && " (busy)"}</span>
        <span>arm <strong className="text-white">{ARM_LABELS[d.arm] ?? d.arm}</strong>{d.m2_busy && " (busy)"}</span>
        <span>lid <strong className="text-white">{LID_LABELS[d.lid] ?? d.lid}</strong>{d.lid_busy && " (busy)"}</span>
      </div>
    )
  }
  if (device === "ingredient") {
    const d = data as NonNullable<StatusData["ingredient"]>
    return (
      <div className="flex gap-6 text-[18px] text-neutral-300 font-mono">
        <span>A <strong className={d.a_busy ? "text-yellow-400" : "text-neutral-500"}>{d.a_busy ? "busy" : "idle"}</strong></span>
        <span>B <strong className={d.b_busy ? "text-yellow-400" : "text-neutral-500"}>{d.b_busy ? "busy" : "idle"}</strong></span>
        <span>C <strong className={d.c_busy ? "text-yellow-400" : "text-neutral-500"}>{d.c_busy ? "busy" : "idle"}</strong></span>
      </div>
    )
  }
  if (device === "cutter") {
    const d = data as NonNullable<StatusData["cutter"]>
    return (
      <div className="flex gap-4 text-[18px] text-neutral-300 font-mono flex-wrap">
        <span>door <strong className={d.door_busy ? "text-yellow-400" : "text-neutral-500"}>{d.door_busy ? "busy" : "idle"}</strong></span>
        <span>clamp <strong className={d.clamp_busy ? "text-yellow-400" : "text-neutral-500"}>{d.clamp_busy ? "busy" : "idle"}</strong></span>
        <span>roller <strong className={d.roller_busy ? "text-yellow-400" : "text-neutral-500"}>{d.roller_busy ? "busy" : "idle"}</strong></span>
        <span>scissor <strong className={d.scissor_busy ? "text-yellow-400" : "text-neutral-500"}>{d.scissor_busy ? "busy" : "idle"}</strong></span>
      </div>
    )
  }
  return null
}

// ── Device status grid ────────────────────────────────────────────────────────

function DeviceCard({
  id, addr, ctrlOnline, detected, detail,
}: {
  id: Device; addr: string; ctrlOnline: boolean; detected: boolean; detail?: string
}) {
  return (
    <div className="bg-neutral-100 rounded-2xl p-5 flex flex-col gap-2">
      <div className="flex items-center justify-between">
        <span className="text-[20px] font-bold capitalize">{id}</span>
        <div className={`w-3 h-3 rounded-full ${detected ? "bg-green-400" : ctrlOnline ? "bg-red-400" : "bg-neutral-400"}`} />
      </div>
      <span className="text-[13px] text-neutral-500 font-mono">{addr}</span>
      <span className={`text-[15px] font-semibold ${detected ? "text-green-600" : "text-red-400"}`}>
        {!ctrlOnline ? "ctrl offline" : detected ? "detected" : "not found"}
      </span>
      {detected && detail && (
        <span className="text-[13px] text-neutral-600 font-mono leading-snug">{detail}</span>
      )}
    </div>
  )
}

function DeviceStatusGrid({ status }: { status: StatusData | null }) {
  const ctrlOnline = status?.ok ?? false
  const ARM_LABELS = ["at A", "at B", "moving"]
  const c = status?.cooker
  const p = status?.plating
  const i = status?.ingredient
  const x = status?.cutter
  return (
    <div className="grid grid-cols-4 gap-4">
      <DeviceCard id="cooker"     addr="RS485 #01" ctrlOnline={ctrlOnline} detected={c?.online ?? false}
        detail={c?.online ? `pos ${c.position} · ${c.on ? "ON" : "off"}` : undefined} />
      <DeviceCard id="plating"    addr="RS485 #02" ctrlOnline={ctrlOnline} detected={p?.online ?? false}
        detail={p?.online ? `pan ${p.m1_pos} · arm ${ARM_LABELS[p.arm] ?? p.arm} · lid ${["closed","open","moving"][p.lid] ?? p.lid}` : undefined} />
      <DeviceCard id="ingredient" addr="RS485 #03" ctrlOnline={ctrlOnline} detected={i?.online ?? false}
        detail={i?.online ? `A:${i.a_busy?"busy":"idle"} B:${i.b_busy?"busy":"idle"} C:${i.c_busy?"busy":"idle"}` : undefined} />
      <DeviceCard id="cutter"     addr="RS485 #04" ctrlOnline={ctrlOnline} detected={x?.online ?? false}
        detail={x?.online ? `door:${x.door_busy?"busy":"idle"} clamp:${x.clamp_busy?"busy":"idle"} roller:${x.roller_busy?"busy":"idle"}` : undefined} />
    </div>
  )
}

// ── Per-device button layout ──────────────────────────────────────────────────

type BtnMap = { a: string | null; b: string | null; x: string | null; y: string | null }

const FACE_LABELS: Record<Device, BtnMap> = {
  cooker:     { a: "Click",     b: "Reset",     x: null,        y: null        },
  plating:    { a: "Arm Disp",  b: "Arm Ret",   x: "Lid Open",  y: "Lid Close" },
  ingredient: { a: "A Dispense", b: "B Dispense", x: "Stop A",  y: "Stop B"   },
  cutter:     { a: "Door Open", b: "Door Close", x: "Clamp",    y: "Release"  },
}

// ── Main component ────────────────────────────────────────────────────────────

export default function Admin() {
  const [device, setDevice] = useState<Device>("cooker")
  const [lVal, setLVal] = useState(50)
  const [rVal, setRVal] = useState(50)

  const navigate      = useNavigate()
  const cmdFetcher    = useFetcher()
  const statusFetcher = useFetcher<typeof loader>()
  const pingFetcher   = useFetcher<typeof action>()

  // Poll status every 2 s
  useEffect(() => {
    statusFetcher.load("/admin")
    const id = setInterval(() => statusFetcher.load("/admin"), 2000)
    return () => clearInterval(id)
  }, []) // eslint-disable-line react-hooks/exhaustive-deps

  const statusData = statusFetcher.data?.status ?? null
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const pingNodes  = (pingFetcher.data as any)?.ping?.nodes ?? null

  const send = useCallback((command: string, value: number = 0) => {
    cmdFetcher.submit(
      { device, command, value: String(value) },
      { method: "POST", action: "/admin" },
    )
  }, [device, cmdFetcher])

  const handleDpad = (dir: "up" | "down" | "left" | "right") => {
    const map: Record<Device, Record<string, () => void>> = {
      cooker: {
        up:    () => send("position_delta", +1),
        down:  () => send("position_delta", -1),
        left:  () => send("set_position", 0),
        right: () => {},
      },
      plating: {
        up:    () => send("fwd_cont"),
        down:  () => send("bwd_cont"),
        left:  () => send("lid_bwd"),
        right: () => send("lid_fwd"),
      },
      ingredient: {
        up:    () => send("a_fwd"),
        down:  () => send("a_bwd"),
        left:  () => send("b_fwd"),
        right: () => send("b_bwd"),
      },
      cutter: {
        up:    () => send("roller_fwd"),
        down:  () => send("roller_rev"),
        left:  () => send("scissor_rev"),
        right: () => send("scissor_fwd"),
      },
    }
    map[device][dir]?.()
  }

  const handleFace = (btn: "a" | "b" | "x" | "y") => {
    const map: Record<Device, Record<string, () => void>> = {
      cooker: {
        a: () => send("click"),
        b: () => send("reset"),
        x: () => {},
        y: () => {},
      },
      plating: {
        a: () => send("dispense"),
        b: () => send("retract"),
        x: () => send("lid_open"),
        y: () => send("lid_close"),
      },
      ingredient: {
        a: () => send("a_dispense"),  // A one revolution
        b: () => send("b_dispense"),  // B one revolution
        x: () => send("a_stop"),
        y: () => send("b_stop"),
      },
      cutter: {
        a: () => send("door_open"),
        b: () => send("door_close"),
        x: () => send("clamp"),
        y: () => send("release"),
      },
    }
    map[device][btn]?.()
  }

  const lStickLabel: Record<Device, string> = {
    cooker:     "Position (0–4)",
    plating:    "Pan Steps",
    ingredient: "Steps/rev (0–400)",
    cutter:     "—",
  }
  const rStickLabel: Record<Device, string> = {
    cooker:     "—",
    plating:    "Lid Dur ×50ms",
    ingredient: "—",
    cutter:     "—",
  }
  const lStickDesc: Record<Device, string> = {
    cooker:     "Release → set position (0–4)",
    plating:    "Release → move pan by steps",
    ingredient: "Release → set steps per revolution",
    cutter:     "Not used",
  }
  const rStickDesc: Record<Device, string> = {
    cooker:     "Not used",
    plating:    "Release → set lid duration (no move)",
    ingredient: "Not used",
    cutter:     "Not used",
  }
  const DPAD_LABELS: Record<Device, Partial<Record<"up"|"down"|"left"|"right", string>>> = {
    cooker:     { up: "+1 pos", down: "−1 pos", left: "home" },
    plating:    { up: "arm fwd", down: "arm bwd", left: "lid bwd", right: "lid fwd" },
    ingredient: { up: "A fwd", down: "A bwd", left: "B fwd", right: "B bwd" },
    cutter:     { up: "Roller Fwd", down: "Roller Rev", left: "Scissor Rev", right: "Scissor Fwd" },
  }

  const handleLCommit = useCallback((v: number) => {
    if (device === "cooker")     send("set_position", Math.round(v / 100 * 4))
    if (device === "plating")    send("move_pan",     Math.round((v - 50) * 4))
    if (device === "ingredient") send("set_rev", Math.round(v / 100 * 400))  // 0–400 steps
  }, [device, send])
  const handleRCommit = useCallback((v: number) => {
    if (device === "plating") send("lid_dur", v * 50)
  }, [device, send])

  // Physical gamepad support
  // D-pad: 12=↑ 13=↓ 14=← 15=→  |  Face: 0=A 1=B 2=X 3=Y  |  L3=10 R3=11
  const gpConnected = useGamepadInput(
    (btn) => {
      if (btn === 12) handleDpad("up")
      if (btn === 13) handleDpad("down")
      if (btn === 14) handleDpad("left")
      if (btn === 15) handleDpad("right")
      if (btn === 0)  handleFace("a")
      if (btn === 1)  handleFace("b")
      if (btn === 2)  handleFace("x")
      if (btn === 3)  handleFace("y")
      if (btn === 10) handleLCommit(lVal)   // L3 → commit left stick
      if (btn === 11) handleRCommit(rVal)   // R3 → commit right stick
    },
    (axes) => {
      // Left stick Y (axis 1): -1=up → 100, +1=down → 0
      const ly = axes[1]
      if (ly !== 0) setLVal(Math.round(50 - ly * 50))
      // Right stick Y (axis 3)
      const ry = axes[3]
      if (ry !== 0) setRVal(Math.round(50 - ry * 50))
    },
  )

  const DEVICES: Device[] = ["cooker", "plating", "ingredient", "cutter"]

  return (
    <div className="screen overflow-y-auto">
      <div className="flex flex-col gap-8 px-10 py-12 min-h-full">

        {/* Header */}
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-6">
            <button
              onClick={() => navigate("/")}
              className="text-[28px] text-neutral-400 active:text-neutral-600 select-none"
            >
              ← Back
            </button>
            <h1 className="text-[60px] font-black tracking-tight">ADMIN DEBUG</h1>
          </div>
          <div className="flex items-center gap-6">
            <div className="flex items-center gap-2">
              <div className={`w-3 h-3 rounded-full ${gpConnected ? "bg-blue-400" : "bg-neutral-400"}`} />
              <span className="text-[20px] text-neutral-500">{gpConnected ? "gamepad" : "no gamepad"}</span>
            </div>
            <button
              onClick={() => pingFetcher.submit(
                { device: "system", command: "ping", value: "0" },
                { method: "POST", action: "/admin" },
              )}
              disabled={pingFetcher.state !== "idle"}
              className="px-6 py-2 rounded-xl bg-neutral-800 text-white text-[20px] font-semibold active:bg-neutral-600 disabled:opacity-50"
            >
              {pingFetcher.state !== "idle" ? "Pinging…" : "Ping All"}
            </button>
            <div className="flex items-center gap-3">
              <div className={`w-4 h-4 rounded-full ${statusData?.ok ? "bg-green-400" : "bg-red-400"}`} />
              <span className="text-[24px] text-neutral-500">{statusData?.ok ? "online" : "offline"}</span>
            </div>
          </div>
        </div>

        {/* Device status grid */}
        <DeviceStatusGrid status={statusData} />

        {/* Ping results */}
        {pingNodes && (
          <div className="bg-neutral-800 rounded-2xl px-8 py-4 flex gap-10">
            {Object.entries(pingNodes).map(([name, r]: [string, unknown]) => {
              const node = r as { online: boolean; ms: number | null }
              return (
                <span key={name} className="font-mono text-[18px]">
                  <span className="text-neutral-400">{name}</span>{" "}
                  <strong className={node.online ? "text-green-400" : "text-red-400"}>
                    {node.online ? `${node.ms}ms` : "✗"}
                  </strong>
                </span>
              )
            })}
          </div>
        )}

        {/* Device selector */}
        <div className="flex gap-4">
          {DEVICES.map((d) => (
            <button
              key={d}
              onClick={() => setDevice(d)}
              className={`px-8 py-4 rounded-2xl text-[24px] font-bold capitalize transition-colors ${
                device === d
                  ? "text-white"
                  : "bg-neutral-200 text-neutral-600"
              }`}
              style={device === d ? { background: "#8B2020" } : {}}
            >
              {d}
            </button>
          ))}
        </div>

        {/* Status row */}
        <div className="bg-neutral-100 rounded-2xl px-8 py-5 min-h-[64px] flex items-center">
          <StatusRow status={statusData} device={device} />
        </div>

        {/* Gamepad area */}
        <div className="flex justify-between items-center px-4">
          <DPad onPress={handleDpad} labels={DPAD_LABELS[device]} />
          <FaceButtons labels={FACE_LABELS[device]} onPress={handleFace} />
        </div>

        {/* Sticks */}
        <div className="flex justify-between items-start px-4 pt-4">
          <Joystick label={lStickLabel[device]} onValue={setLVal} onCommit={handleLCommit} description={lStickDesc[device]} />
          <Joystick label={rStickLabel[device]} onValue={setRVal} onCommit={handleRCommit} description={rStickDesc[device]} />
        </div>

        {/* Ingredient C motor controls */}
        {device === "ingredient" && (
          <div className="px-4">
            <p className="text-[20px] text-neutral-500 mb-4">Motor C</p>
            <div className="flex gap-4 flex-wrap">
              {(["c_fwd", "c_bwd", "c_dispense", "c_retract", "c_stop"] as const).map((cmd) => (
                <button
                  key={cmd}
                  onClick={() => send(cmd)}
                  className="px-6 py-3 rounded-xl bg-neutral-200 text-neutral-700 text-[20px] font-semibold active:bg-neutral-400"
                >
                  {cmd === "c_fwd" ? "C Fwd" : cmd === "c_bwd" ? "C Bwd" : cmd === "c_dispense" ? "C Dispense" : cmd === "c_retract" ? "C Retract" : "C Stop"}
                </button>
              ))}
            </div>
          </div>
        )}

        {/* Cutter dispenser buttons */}
        {device === "cutter" && (
          <div className="px-4">
            <p className="text-[20px] text-neutral-500 mb-4">Dispensers</p>
            <div className="flex gap-4 flex-wrap">
              {(["pepper_dispense", "pump_on", "pump_off", "salt_dispense"] as const).map((cmd) => (
                <button
                  key={cmd}
                  onClick={() => send(cmd)}
                  className="px-6 py-3 rounded-xl bg-neutral-200 text-neutral-700 text-[20px] font-semibold active:bg-neutral-400"
                >
                  {cmd === "pepper_dispense" ? "Pepper" : cmd === "pump_on" ? "Pump On" : cmd === "pump_off" ? "Pump Off" : "Salt"}
                </button>
              ))}
            </div>
          </div>
        )}

        {/* System state buttons */}
        <div className="mt-auto pt-8 border-t border-neutral-200">
          <p className="text-[20px] text-neutral-500 mb-4">LCD State</p>
          <div className="flex gap-4 flex-wrap">
            {(["idle", "personalizing", "cooking", "finished"] as const).map((s) => (
              <button
                key={s}
                onClick={() =>
                  cmdFetcher.submit(
                    { device: "system", command: "set_state", state_name: s, value: "0" },
                    { method: "POST", action: "/admin" },
                  )
                }
                className="px-6 py-3 rounded-xl bg-neutral-200 text-neutral-700 text-[20px] font-semibold capitalize active:bg-neutral-400"
              >
                {s}
              </button>
            ))}
          </div>
        </div>

      </div>
    </div>
  )
}
