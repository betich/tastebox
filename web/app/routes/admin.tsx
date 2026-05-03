import { useCallback, useEffect, useRef, useState } from "react"
import { useFetcher } from "react-router"
import type { ActionFunctionArgs } from "react-router"

// ── Types ─────────────────────────────────────────────────────────────────────

type Device = "cooker" | "plating" | "ingredient" | "cutter"

interface StatusData {
  ok: boolean
  cooker?:     { online: boolean; on: boolean; position: number }
  plating?:    { online: boolean; m1_busy: boolean; m2_busy: boolean; m1_pos: number; arm: number }
  ingredient?: { online: boolean; busy: boolean; remaining_ms: number }
  cutter?:     { online: boolean; lid_busy: boolean; piston1_busy: boolean; piston2_busy: boolean }
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
      if (command === "goto_a")      await post("/plating/arm",  { action: "goto_a" })
      if (command === "goto_b")      await post("/plating/arm",  { action: "goto_b" })
      if (command === "home_pan")    await post("/plating/home")
      if (command === "stop_arm")    await post("/plating/arm",  { action: "stop" })
      if (command === "move_pan")    await post("/plating/move", { m1: Math.round(value), m2: 0 })
      if (command === "arm_dur")     await post("/plating/arm",  { action: "goto_b", duration_ms: Math.round(value) })
    } else if (device === "ingredient") {
      if (command === "dispense")     await post("/ingredient/dispense",
                                        value > 0 ? { duration_ms: Math.round(value) } : undefined)
      if (command === "retract")      await post("/ingredient/retract")
      if (command === "stop")         await post("/ingredient/stop")
      if (command === "set_duration") await post("/ingredient/duration", { ms: Math.round(value) })
    } else if (device === "cutter") {
      if (command === "open_lid")   await post("/cutter/lid",
                                      { action: "open",  ...(value > 0 ? { duration_ms: Math.round(value) } : {}) })
      if (command === "close_lid")  await post("/cutter/lid",
                                      { action: "close", ...(value > 0 ? { duration_ms: Math.round(value) } : {}) })
      if (command === "p1_ext")     await post("/cutter/piston", { which: 1, action: "extend" })
      if (command === "p1_ret")     await post("/cutter/piston", { which: 1, action: "retract" })
      if (command === "p2_ext")     await post("/cutter/piston", { which: 2, action: "extend" })
      if (command === "p2_ret")     await post("/cutter/piston", { which: 2, action: "retract" })
      if (command === "stop_all")   await post("/cutter/stop")
    } else if (device === "system") {
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
  description,
}: {
  label: string
  onValue: (v: number) => void
  description?: string
}) {
  const containerRef = useRef<HTMLDivElement>(null)
  const [knob, setKnob]     = useState({ x: 0, y: 0 })
  const [display, setDisplay] = useState(50)
  const dragging = useRef(false)
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
    return (
      <div className="flex gap-6 text-[18px] text-neutral-300 font-mono">
        <span>pan <strong className="text-white">{d.m1_pos}</strong>{d.m1_busy && " (moving)"}</span>
        <span>arm <strong className="text-white">{ARM_LABELS[d.arm] ?? d.arm}</strong>{d.m2_busy && " (moving)"}</span>
      </div>
    )
  }
  if (device === "ingredient") {
    const d = data as NonNullable<StatusData["ingredient"]>
    return (
      <div className="flex gap-6 text-[18px] text-neutral-300 font-mono">
        <span>busy <strong className={d.busy ? "text-yellow-400" : "text-neutral-500"}>{d.busy ? "yes" : "no"}</strong></span>
        <span>rem <strong className="text-white">{d.remaining_ms}ms</strong></span>
      </div>
    )
  }
  if (device === "cutter") {
    const d = data as NonNullable<StatusData["cutter"]>
    return (
      <div className="flex gap-4 text-[18px] text-neutral-300 font-mono flex-wrap">
        <span>lid <strong className={d.lid_busy ? "text-yellow-400" : "text-neutral-500"}>{d.lid_busy ? "busy" : "idle"}</strong></span>
        <span>p1 <strong className={d.piston1_busy ? "text-yellow-400" : "text-neutral-500"}>{d.piston1_busy ? "busy" : "idle"}</strong></span>
        <span>p2 <strong className={d.piston2_busy ? "text-yellow-400" : "text-neutral-500"}>{d.piston2_busy ? "busy" : "idle"}</strong></span>
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
      <DeviceCard id="cooker"     addr="0x42" ctrlOnline={ctrlOnline} detected={c?.online ?? false}
        detail={c?.online ? `pos ${c.position} · ${c.on ? "ON" : "off"}` : undefined} />
      <DeviceCard id="plating"    addr="0x43" ctrlOnline={ctrlOnline} detected={p?.online ?? false}
        detail={p?.online ? `pan ${p.m1_pos} · arm ${ARM_LABELS[p.arm] ?? p.arm}` : undefined} />
      <DeviceCard id="ingredient" addr="0x44" ctrlOnline={ctrlOnline} detected={i?.online ?? false}
        detail={i?.online ? `${i.busy ? "busy" : "idle"} · ${i.remaining_ms}ms rem` : undefined} />
      <DeviceCard id="cutter"     addr="0x45" ctrlOnline={ctrlOnline} detected={x?.online ?? false}
        detail={x?.online ? `lid:${x.lid_busy?"busy":"idle"} p1:${x.piston1_busy?"busy":"idle"} p2:${x.piston2_busy?"busy":"idle"}` : undefined} />
    </div>
  )
}

// ── Per-device button layout ──────────────────────────────────────────────────

type BtnMap = { a: string | null; b: string | null; x: string | null; y: string | null }

const FACE_LABELS: Record<Device, BtnMap> = {
  cooker:     { a: "Click",    b: "Reset",    x: null,       y: null       },
  plating:    { a: "Goto A",   b: "Goto B",   x: "Home Pan", y: "Stop Arm" },
  ingredient: { a: "Dispense", b: "Retract",  x: "Stop",     y: null       },
  cutter:     { a: "Open Lid", b: "Close Lid", x: "P2 Ext",  y: "P2 Ret"  },
}

// ── Main component ────────────────────────────────────────────────────────────

export default function Admin() {
  const [device, setDevice] = useState<Device>("cooker")
  const [lVal, setLVal] = useState(50)
  const [rVal, setRVal] = useState(50)

  const cmdFetcher    = useFetcher()
  const statusFetcher = useFetcher<typeof loader>()

  // Poll status every 2 s
  useEffect(() => {
    statusFetcher.load("/admin")
    const id = setInterval(() => statusFetcher.load("/admin"), 2000)
    return () => clearInterval(id)
  }, []) // eslint-disable-line react-hooks/exhaustive-deps

  const statusData = statusFetcher.data?.status ?? null

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
        up:    () => send("move_pan", +5),
        down:  () => send("move_pan", -5),
        left:  () => send("move_pan", -15),
        right: () => send("move_pan", +15),
      },
      ingredient: {
        up: () => {}, down: () => {}, left: () => {}, right: () => {},
      },
      cutter: {
        up:    () => send("p1_ext"),
        down:  () => send("p1_ret"),
        left:  () => send("p2_ret"),
        right: () => send("p2_ext"),
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
        a: () => send("goto_a"),
        b: () => send("goto_b"),
        x: () => send("home_pan"),
        y: () => send("stop_arm"),
      },
      ingredient: {
        a: () => send("dispense", lVal * 20),  // 0–100 → 0–2000 ms
        b: () => send("retract"),
        x: () => send("stop"),
        y: () => {},
      },
      cutter: {
        a: () => send("open_lid",  rVal * 20),
        b: () => send("close_lid", rVal * 20),
        x: () => send("p2_ext"),
        y: () => send("p2_ret"),
      },
    }
    map[device][btn]?.()
  }

  const lStickLabel: Record<Device, string> = {
    cooker:     "Position (0–4)",
    plating:    "Pan Steps",
    ingredient: "Duration ×20ms",
    cutter:     "Lid Dur ×20ms",
  }
  const rStickLabel: Record<Device, string> = {
    cooker:     "—",
    plating:    "Arm Dur ×50ms",
    ingredient: "—",
    cutter:     "Piston Dur ×20ms",
  }
  const lStickDesc: Record<Device, string> = {
    cooker:     "Release → set position (0–4)",
    plating:    "Release → move pan by steps",
    ingredient: "Release → set dispense duration",
    cutter:     "Value used for lid cmds (A/B)",
  }
  const rStickDesc: Record<Device, string> = {
    cooker:     "Not used",
    plating:    "Release → set arm travel duration",
    ingredient: "Not used",
    cutter:     "Not used",
  }
  const DPAD_LABELS: Record<Device, Partial<Record<"up"|"down"|"left"|"right", string>>> = {
    cooker:     { up: "+1 pos", down: "−1 pos", left: "home" },
    plating:    { up: "+5 pan", down: "−5 pan", left: "−15 pan", right: "+15 pan" },
    ingredient: {},
    cutter:     { up: "P1 ext", down: "P1 ret", left: "P2 ret", right: "P2 ext" },
  }

  const handleLStickRelease = () => {
    if (device === "cooker")     send("set_position", Math.round(lVal / 100 * 4))
    if (device === "plating")    send("move_pan",     Math.round((lVal - 50) * 4))
    if (device === "ingredient") send("set_duration", lVal * 20)
  }
  const handleRStickRelease = () => {
    if (device === "plating") send("arm_dur", rVal * 50)
  }

  const DEVICES: Device[] = ["cooker", "plating", "ingredient", "cutter"]

  return (
    <div className="screen overflow-y-auto">
      <div className="flex flex-col gap-8 px-10 py-12 min-h-full">

        {/* Header */}
        <div className="flex items-center justify-between">
          <h1 className="text-[60px] font-black tracking-tight">ADMIN DEBUG</h1>
          <div className="flex items-center gap-3">
            <div className={`w-4 h-4 rounded-full ${statusData?.ok ? "bg-green-400" : "bg-red-400"}`} />
            <span className="text-[24px] text-neutral-500">{statusData?.ok ? "online" : "offline"}</span>
          </div>
        </div>

        {/* Device status grid */}
        <DeviceStatusGrid status={statusData} />

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
          <div
            onPointerUp={handleLStickRelease}
            onPointerCancel={handleLStickRelease}
          >
            <Joystick label={lStickLabel[device]} onValue={setLVal} description={lStickDesc[device]} />
          </div>
          <div
            onPointerUp={handleRStickRelease}
            onPointerCancel={handleRStickRelease}
          >
            <Joystick label={rStickLabel[device]} onValue={setRVal} description={rStickDesc[device]} />
          </div>
        </div>

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
