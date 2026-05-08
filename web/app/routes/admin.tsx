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

type SubAction    = { label: string; command: string; value?: number }
type FaceMap      = Partial<Record<"a" | "b" | "x" | "y", SubAction>>
type SubComponent = { id: string; label: string; actions: SubAction[]; face: FaceMap }

// ── Device config ─────────────────────────────────────────────────────────────

const DEVICES: Device[] = ["cooker", "plating", "ingredient", "cutter"]

const DEVICE_CONFIG: Record<Device, SubComponent[]> = {
  cooker: [{
    id: "control", label: "Control",
    actions: [
      { label: "Click",  command: "click"           },
      { label: "Reset",  command: "reset"           },
      { label: "+1 pos", command: "position_delta", value:  1 },
      { label: "−1 pos", command: "position_delta", value: -1 },
    ],
    face: {
      a: { label: "Click",  command: "click"           },
      b: { label: "Reset",  command: "reset"           },
      x: { label: "+1 pos", command: "position_delta", value:  1 },
      y: { label: "−1 pos", command: "position_delta", value: -1 },
    },
  }],

  plating: [
    {
      id: "arm", label: "Arm",
      actions: [
        { label: "Retract",  command: "dispense" },
        { label: "Dispense", command: "retract"  },
        { label: "Fwd",      command: "fwd_cont" },
        { label: "Bwd",      command: "bwd_cont" },
        { label: "Stop",     command: "stop_arm" },
      ],
      face: {
        a: { label: "Retract",  command: "dispense" },
        b: { label: "Dispense", command: "retract"  },
        x: { label: "Fwd",      command: "fwd_cont" },
        y: { label: "Stop",     command: "stop_arm" },
      },
    },
    {
      id: "lid", label: "Lid",
      actions: [
        { label: "Open",  command: "lid_open"  },
        { label: "Close", command: "lid_close" },
        { label: "Fwd",   command: "lid_fwd"   },
        { label: "Bwd",   command: "lid_bwd"   },
        { label: "Stop",  command: "stop_lid"  },
      ],
      face: {
        a: { label: "Open",  command: "lid_open"  },
        b: { label: "Close", command: "lid_close" },
        x: { label: "Fwd",   command: "lid_fwd"   },
        y: { label: "Stop",  command: "stop_lid"  },
      },
    },
    {
      id: "pan", label: "Pan",
      actions: [
        { label: "Home", command: "home_pan"                },
        { label: "+10",  command: "move_pan", value:   10   },
        { label: "−10",  command: "move_pan", value:  -10   },
      ],
      face: {
        a: { label: "Home", command: "home_pan"              },
        x: { label: "+10",  command: "move_pan", value:  10  },
        y: { label: "−10",  command: "move_pan", value: -10  },
      },
    },
  ],

  ingredient: [
    {
      id: "a", label: "Motor A",
      actions: [
        { label: "Fwd",      command: "a_fwd"      },
        { label: "Bwd",      command: "a_bwd"      },
        { label: "Dispense", command: "a_dispense" },
        { label: "Retract",  command: "a_retract"  },
        { label: "Stop",     command: "a_stop"     },
      ],
      face: {
        a: { label: "Dispense", command: "a_dispense" },
        b: { label: "Retract",  command: "a_retract"  },
        x: { label: "Fwd",      command: "a_fwd"      },
        y: { label: "Stop",     command: "a_stop"     },
      },
    },
    {
      id: "b", label: "Motor B",
      actions: [
        { label: "Fwd",      command: "b_fwd"      },
        { label: "Bwd",      command: "b_bwd"      },
        { label: "Dispense", command: "b_dispense" },
        { label: "Retract",  command: "b_retract"  },
        { label: "Stop",     command: "b_stop"     },
      ],
      face: {
        a: { label: "Dispense", command: "b_dispense" },
        b: { label: "Retract",  command: "b_retract"  },
        x: { label: "Fwd",      command: "b_fwd"      },
        y: { label: "Stop",     command: "b_stop"     },
      },
    },
    {
      id: "c", label: "Motor C",
      actions: [
        { label: "Fwd",      command: "c_fwd"      },
        { label: "Bwd",      command: "c_bwd"      },
        { label: "Dispense", command: "c_dispense" },
        { label: "Retract",  command: "c_retract"  },
        { label: "Stop",     command: "c_stop"     },
      ],
      face: {
        a: { label: "Dispense", command: "c_dispense" },
        b: { label: "Retract",  command: "c_retract"  },
        x: { label: "Fwd",      command: "c_fwd"      },
        y: { label: "Stop",     command: "c_stop"     },
      },
    },
  ],

  cutter: [
    {
      id: "door", label: "Door / Clamp",
      actions: [
        { label: "Door Open",  command: "door_open"  },
        { label: "Door Close", command: "door_close" },
        { label: "Clamp",      command: "clamp"      },
        { label: "Release",    command: "release"    },
      ],
      face: {
        a: { label: "Door Open",  command: "door_open"  },
        b: { label: "Door Close", command: "door_close" },
        x: { label: "Clamp",      command: "clamp"      },
        y: { label: "Release",    command: "release"    },
      },
    },
    {
      id: "motor", label: "Roller / Scissor",
      actions: [
        { label: "Roller Fwd",   command: "roller_fwd"   },
        { label: "Roller Rev",   command: "roller_rev"   },
        { label: "Roller Stop",  command: "roller_stop"  },
        { label: "Scissor Fwd",  command: "scissor_fwd"  },
        { label: "Scissor Rev",  command: "scissor_rev"  },
        { label: "Scissor Stop", command: "scissor_stop" },
      ],
      face: {
        a: { label: "Roller Fwd",  command: "roller_fwd"  },
        b: { label: "Roller Rev",  command: "roller_rev"  },
        x: { label: "Scissor Fwd", command: "scissor_fwd" },
        y: { label: "Scissor Rev", command: "scissor_rev" },
      },
    },
    {
      id: "dispenser", label: "Dispensers",
      actions: [
        { label: "Pepper",   command: "pepper_dispense" },
        { label: "Pump On",  command: "pump_on"         },
        { label: "Pump Off", command: "pump_off"        },
        { label: "Salt",     command: "salt_dispense"   },
      ],
      face: {
        a: { label: "Pepper",   command: "pepper_dispense" },
        b: { label: "Pump On",  command: "pump_on"         },
        x: { label: "Pump Off", command: "pump_off"        },
        y: { label: "Salt",     command: "salt_dispense"   },
      },
    },
  ],
}

// Joystick usage per device  (null = not used → dimmed)
const STICK_USE: Record<Device, { l: string | null; r: string | null; lDesc: string; rDesc: string }> = {
  cooker:     { l: "Position (0–4)", r: null,           lDesc: "Release → set position", rDesc: "Not used" },
  plating:    { l: "Pan Steps",      r: "Lid Dur ×50ms", lDesc: "Release → move pan",    rDesc: "Release → set lid duration" },
  ingredient: { l: "Steps/rev",      r: null,           lDesc: "Release → set steps/rev", rDesc: "Not used" },
  cutter:     { l: null,             r: null,           lDesc: "Not used",               rDesc: "Not used" },
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

// ── Action ────────────────────────────────────────────────────────────────────

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
      if (command === "dispense")   await post("/plating/arm",  { action: "dispense" })
      if (command === "retract")    await post("/plating/arm",  { action: "retract"  })
      if (command === "fwd_cont")   await post("/plating/arm",  { action: "fwd_cont" })
      if (command === "bwd_cont")   await post("/plating/arm",  { action: "bwd_cont" })
      if (command === "home_pan")   await post("/plating/home")
      if (command === "stop_arm")   await post("/plating/arm",  { action: "stop" })
      if (command === "move_pan")   await post("/plating/move", { m1: Math.round(value), m2: 0 })
      if (command === "arm_dur")    await post("/plating/arm",  { duration_ms: Math.round(value), action: "stop" })
      if (command === "lid_open")   await post("/plating/lid",  { action: "open"     })
      if (command === "lid_close")  await post("/plating/lid",  { action: "close"    })
      if (command === "lid_fwd")    await post("/plating/lid",  { action: "fwd_cont" })
      if (command === "lid_bwd")    await post("/plating/lid",  { action: "bwd_cont" })
      if (command === "stop_lid")   await post("/plating/lid",  { action: "stop"     })
      if (command === "lid_dur")    await post("/plating/lid",  { duration_ms: Math.round(value), action: "stop" })
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
      if (command === "door_open")       await post("/cutter/door",    { action: "open"     })
      if (command === "door_close")      await post("/cutter/door",    { action: "close"    })
      if (command === "clamp")           await post("/cutter/clamp",   { action: "clamp"    })
      if (command === "release")         await post("/cutter/clamp",   { action: "release"  })
      if (command === "roller_fwd")      await post("/cutter/roller",  { action: "fwd"      })
      if (command === "roller_rev")      await post("/cutter/roller",  { action: "rev"      })
      if (command === "roller_stop")     await post("/cutter/roller",  { action: "stop"     })
      if (command === "scissor_fwd")     await post("/cutter/scissor", { action: "fwd"      })
      if (command === "scissor_rev")     await post("/cutter/scissor", { action: "rev"      })
      if (command === "scissor_stop")    await post("/cutter/scissor", { action: "stop"     })
      if (command === "pepper_dispense") await post("/cutter/pepper",  { action: "dispense" })
      if (command === "pump_on")         await post("/cutter/pump",    { action: "on"       })
      if (command === "pump_off")        await post("/cutter/pump",    { action: "off"      })
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

// ── Joystick ──────────────────────────────────────────────────────────────────

function Joystick({
  label, onValue, onCommit, description, dimmed,
}: {
  label: string; onValue: (v: number) => void; onCommit?: (v: number) => void
  description?: string; dimmed?: boolean
}) {
  const containerRef  = useRef<HTMLDivElement>(null)
  const [knob, setKnob]       = useState({ x: 0, y: 0 })
  const [display, setDisplay] = useState(50)
  const dragging = useRef(false)
  const lastVal  = useRef(50)
  const R = 52

  const updateFromPointer = useCallback((e: React.PointerEvent) => {
    if (!containerRef.current) return
    const rect = containerRef.current.getBoundingClientRect()
    const cx = rect.left + rect.width  / 2
    const cy = rect.top  + rect.height / 2
    const dx = e.clientX - cx
    const dy = e.clientY - cy
    const r  = Math.min(Math.hypot(dx, dy), R)
    const ang = Math.atan2(dy, dx)
    const pos = { x: Math.cos(ang) * r, y: Math.sin(ang) * r }
    setKnob(pos)
    const val = Math.round(50 - (pos.y / R) * 50)
    lastVal.current = val
    setDisplay(val)
    onValue(val)
  }, [onValue, R])

  return (
    <div className={`flex flex-col items-center gap-3 transition-opacity ${dimmed ? "opacity-30" : ""}`}>
      <div
        ref={containerRef}
        className="relative rounded-full bg-neutral-800 border-2 border-neutral-600 cursor-pointer select-none"
        style={{ width: R * 2, height: R * 2 }}
        onPointerDown={e => { e.currentTarget.setPointerCapture(e.pointerId); dragging.current = true; updateFromPointer(e) }}
        onPointerMove={e => { if (dragging.current) updateFromPointer(e) }}
        onPointerUp={() => { dragging.current = false; onCommit?.(lastVal.current); lastVal.current = 50; setKnob({ x: 0, y: 0 }); setDisplay(50); onValue(50) }}
        onPointerCancel={() => { dragging.current = false; setKnob({ x: 0, y: 0 }); setDisplay(50); onValue(50) }}
      >
        <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
          <div className="w-full h-px bg-neutral-700" />
        </div>
        <div className="absolute inset-0 flex items-center justify-center pointer-events-none">
          <div className="h-full w-px bg-neutral-700" />
        </div>
        <div
          className="absolute rounded-full bg-neutral-300 pointer-events-none"
          style={{ width: 22, height: 22, left: R - 11 + knob.x, top: R - 11 + knob.y }}
        />
      </div>
      <span className="text-neutral-400 text-[18px]">{label}: <span className="text-white font-mono">{display}</span></span>
      {description && <span className="text-neutral-500 text-[13px] text-center max-w-[140px] leading-tight">{description}</span>}
    </div>
  )
}

// ── Status display ────────────────────────────────────────────────────────────

function StatusRow({ status, device }: { status: StatusData | null; device: Device }) {
  if (!status?.ok) return <span className="text-red-400 text-[18px]">Controller offline</span>
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
    const ARM = ["at A", "at B", "moving"]
    const LID = ["closed", "open", "moving"]
    return (
      <div className="flex gap-6 text-[18px] text-neutral-300 font-mono">
        <span>pan <strong className="text-white">{d.m1_pos}</strong>{d.m1_busy && " (busy)"}</span>
        <span>arm <strong className="text-white">{ARM[d.arm] ?? d.arm}</strong>{d.m2_busy && " (busy)"}</span>
        <span>lid <strong className="text-white">{LID[d.lid] ?? d.lid}</strong>{d.lid_busy && " (busy)"}</span>
      </div>
    )
  }
  if (device === "ingredient") {
    const d = data as NonNullable<StatusData["ingredient"]>
    const st = (b: boolean) => <strong className={b ? "text-yellow-400" : "text-neutral-500"}>{b ? "busy" : "idle"}</strong>
    return (
      <div className="flex gap-6 text-[18px] text-neutral-300 font-mono">
        <span>A {st(d.a_busy)}</span>
        <span>B {st(d.b_busy)}</span>
        <span>C {st(d.c_busy)}</span>
      </div>
    )
  }
  if (device === "cutter") {
    const d = data as NonNullable<StatusData["cutter"]>
    const st = (b: boolean) => <strong className={b ? "text-yellow-400" : "text-neutral-500"}>{b ? "busy" : "idle"}</strong>
    return (
      <div className="flex gap-4 text-[18px] text-neutral-300 font-mono flex-wrap">
        <span>door {st(d.door_busy)}</span>
        <span>clamp {st(d.clamp_busy)}</span>
        <span>roller {st(d.roller_busy)}</span>
        <span>scissor {st(d.scissor_busy)}</span>
      </div>
    )
  }
  return null
}

// ── Device status grid ────────────────────────────────────────────────────────

function DeviceCard({ id, addr, ctrlOnline, detected, detail }: {
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
      {detected && detail && <span className="text-[13px] text-neutral-600 font-mono leading-snug">{detail}</span>}
    </div>
  )
}

function DeviceStatusGrid({ status }: { status: StatusData | null }) {
  const ok = status?.ok ?? false
  const c = status?.cooker;     const p = status?.plating
  const i = status?.ingredient; const x = status?.cutter
  const ARM = ["at A", "at B", "moving"]
  return (
    <div className="grid grid-cols-4 gap-4">
      <DeviceCard id="cooker"     addr="RS485 #01" ctrlOnline={ok} detected={c?.online ?? false}
        detail={c?.online ? `pos ${c.position} · ${c.on ? "ON" : "off"}` : undefined} />
      <DeviceCard id="plating"    addr="RS485 #02" ctrlOnline={ok} detected={p?.online ?? false}
        detail={p?.online ? `pan ${p.m1_pos} · arm ${ARM[p.arm] ?? p.arm} · lid ${["closed","open","moving"][p.lid] ?? p.lid}` : undefined} />
      <DeviceCard id="ingredient" addr="RS485 #03" ctrlOnline={ok} detected={i?.online ?? false}
        detail={i?.online ? `A:${i.a_busy?"busy":"idle"} B:${i.b_busy?"busy":"idle"} C:${i.c_busy?"busy":"idle"}` : undefined} />
      <DeviceCard id="cutter"     addr="RS485 #04" ctrlOnline={ok} detected={x?.online ?? false}
        detail={x?.online ? `door:${x.door_busy?"busy":"idle"} clamp:${x.clamp_busy?"busy":"idle"}` : undefined} />
    </div>
  )
}

// ── Action rows (the main control surface) ────────────────────────────────────

const FACE_COLORS = { a: "#22c55e", b: "#ef4444", x: "#3b82f6", y: "#eab308" }
const FACE_KEYS   = ["a", "b", "x", "y"] as const

function ActionRows({
  subs, activeIdx, onAction,
}: {
  subs: SubComponent[]; activeIdx: number; onAction: (cmd: string, val?: number) => void
}) {
  return (
    <div className="flex flex-col gap-4">
      {subs.map((sub, idx) => {
        const active = idx === activeIdx
        return (
          <div key={sub.id} className={`flex items-start gap-4 transition-opacity ${active ? "opacity-100" : "opacity-30 pointer-events-none"}`}>
            <span className={`text-[17px] font-bold w-32 text-right pt-2 shrink-0 ${active ? "text-white" : "text-neutral-400"}`}>
              {sub.label}
            </span>
            <div className="flex gap-2 flex-wrap">
              {sub.actions.map((a) => {
                const fk = FACE_KEYS.find(k => {
                  const f = sub.face[k]
                  return f && f.command === a.command && (f.value ?? null) === (a.value ?? null)
                })
                return (
                  <button
                    key={`${a.command}-${a.value ?? ""}`}
                    onClick={() => onAction(a.command, a.value)}
                    className="relative px-5 py-2.5 rounded-xl bg-neutral-200 text-neutral-800 text-[18px] font-semibold active:bg-neutral-400"
                  >
                    {a.label}
                    {fk && (
                      <span
                        className="absolute -top-1.5 -right-1.5 w-5 h-5 rounded-full text-[10px] font-black text-white flex items-center justify-center"
                        style={{ background: FACE_COLORS[fk] }}
                      >
                        {fk.toUpperCase()}
                      </span>
                    )}
                  </button>
                )
              })}
            </div>
          </div>
        )
      })}
    </div>
  )
}

// ── Face button cluster (visual reference) ────────────────────────────────────

function FaceCluster({ face }: { face: FaceMap }) {
  const btn = (id: "a" | "b" | "x" | "y", pos: string) => {
    const mapped = face[id]
    return (
      <div
        key={id}
        className={`w-14 h-14 rounded-full flex flex-col items-center justify-center text-white gap-0.5 select-none ${pos} transition-opacity ${mapped ? "opacity-100" : "opacity-20"}`}
        style={{ background: FACE_COLORS[id] }}
      >
        <span className="text-[16px] font-black">{id.toUpperCase()}</span>
        {mapped && <span className="text-[9px] leading-none text-center px-1 opacity-90 font-semibold">{mapped.label}</span>}
      </div>
    )
  }
  return (
    <div className="relative shrink-0" style={{ width: 168, height: 168 }}>
      <div className="absolute top-0 left-0 right-0 flex justify-center">{btn("y", "")}</div>
      <div className="absolute bottom-0 left-0 right-0 flex justify-center">{btn("a", "")}</div>
      <div className="absolute top-0 bottom-0 left-0 flex items-center">{btn("x", "")}</div>
      <div className="absolute top-0 bottom-0 right-0 flex items-center">{btn("b", "")}</div>
    </div>
  )
}

// ── Sub-component selector tabs ───────────────────────────────────────────────

function SubTabs({
  subs, activeIdx, onChange,
}: {
  subs: SubComponent[]; activeIdx: number; onChange: (i: number) => void
}) {
  if (subs.length <= 1) return null
  return (
    <div className="flex items-center gap-3">
      <span className="text-[14px] text-neutral-500 font-mono shrink-0">D-pad ← →</span>
      <div className="flex gap-2">
        {subs.map((s, i) => (
          <button
            key={s.id}
            onClick={() => onChange(i)}
            className={`px-5 py-2 rounded-xl text-[18px] font-bold transition-colors ${
              i === activeIdx ? "bg-neutral-800 text-white" : "bg-neutral-200 text-neutral-500 hover:bg-neutral-300"
            }`}
          >
            {s.label}
          </button>
        ))}
      </div>
    </div>
  )
}

// ── Main component ────────────────────────────────────────────────────────────

export default function Admin() {
  const [deviceIdx, setDeviceIdx] = useState(0)
  const [subIdxMap, setSubIdxMap] = useState<Record<Device, number>>(
    { cooker: 0, plating: 0, ingredient: 0, cutter: 0 },
  )
  const [lVal, setLVal] = useState(50)
  const [rVal, setRVal] = useState(50)

  const navigate      = useNavigate()
  const cmdFetcher    = useFetcher()
  const statusFetcher = useFetcher<typeof loader>()
  const pingFetcher   = useFetcher<typeof action>()

  useEffect(() => {
    statusFetcher.load("/admin")
    const id = setInterval(() => statusFetcher.load("/admin"), 2000)
    return () => clearInterval(id)
  }, []) // eslint-disable-line react-hooks/exhaustive-deps

  const statusData = statusFetcher.data?.status ?? null
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  const pingNodes  = (pingFetcher.data as any)?.ping?.nodes ?? null

  const device = DEVICES[deviceIdx]
  const subs   = DEVICE_CONFIG[device]
  const subIdx = subIdxMap[device]
  const sub    = subs[subIdx]
  const sticks = STICK_USE[device]

  const lastSendMs = useRef(0)
  const send = useCallback((command: string, value: number = 0) => {
    const now = Date.now()
    if (now - lastSendMs.current < 250) return
    lastSendMs.current = now
    cmdFetcher.submit(
      { device, command, value: String(value) },
      { method: "POST", action: "/admin" },
    )
  }, [device, cmdFetcher])

  const prevDevice = useCallback(() => setDeviceIdx(i => (i - 1 + DEVICES.length) % DEVICES.length), [])
  const nextDevice = useCallback(() => setDeviceIdx(i => (i + 1) % DEVICES.length), [])

  const prevSub = useCallback(() =>
    setSubIdxMap(m => ({ ...m, [device]: (m[device] - 1 + subs.length) % subs.length })),
    [device, subs.length])
  const nextSub = useCallback(() =>
    setSubIdxMap(m => ({ ...m, [device]: (m[device] + 1) % subs.length })),
    [device, subs.length])

  const handleLCommit = useCallback((v: number) => {
    if (device === "cooker")     send("set_position", Math.round(v / 100 * 4))
    if (device === "plating")    send("move_pan",     Math.round((v - 50) * 4))
    if (device === "ingredient") send("set_rev",      Math.round(v / 100 * 400))
  }, [device, send])
  const handleRCommit = useCallback((v: number) => {
    if (device === "plating") send("lid_dur", v * 50)
  }, [device, send])

  const gpConnected = useGamepadInput(
    (btn) => {
      // LB / RB — cycle devices
      if (btn === 4) prevDevice()
      if (btn === 5) nextDevice()
      // D-pad ← → ↑ ↓ — cycle sub-components
      if (btn === 14 || btn === 12) prevSub()
      if (btn === 15 || btn === 13) nextSub()
      // Face — current sub actions
      if (btn === 0 && sub.face.a) send(sub.face.a.command, sub.face.a.value ?? 0)
      if (btn === 1 && sub.face.b) send(sub.face.b.command, sub.face.b.value ?? 0)
      if (btn === 2 && sub.face.x) send(sub.face.x.command, sub.face.x.value ?? 0)
      if (btn === 3 && sub.face.y) send(sub.face.y.command, sub.face.y.value ?? 0)
      // L3/R3 — commit sticks
      if (btn === 10) handleLCommit(lVal)
      if (btn === 11) handleRCommit(rVal)
    },
    (axes) => {
      const ly = axes[1]; if (ly !== 0) setLVal(Math.round(50 - ly * 50))
      const ry = axes[3]; if (ry !== 0) setRVal(Math.round(50 - ry * 50))
    },
  )

  const prevDeviceName = DEVICES[(deviceIdx - 1 + DEVICES.length) % DEVICES.length]
  const nextDeviceName = DEVICES[(deviceIdx + 1) % DEVICES.length]

  return (
    <div className="screen overflow-y-auto">
      <div className="flex flex-col gap-6 px-10 py-10 min-h-full">

        {/* Header */}
        <div className="flex items-center justify-between">
          <div className="flex items-center gap-6">
            <button onClick={() => navigate("/")}
              className="text-[28px] text-neutral-400 active:text-neutral-600 select-none">
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

        {/* Device selector — D-pad ← → */}
        <div className="flex items-center gap-4">
          <span className="text-[14px] text-neutral-500 font-mono shrink-0">LB / RB</span>
          <div className="flex gap-3">
            {DEVICES.map((d, i) => (
              <button
                key={d}
                onClick={() => setDeviceIdx(i)}
                className={`px-8 py-4 rounded-2xl text-[24px] font-bold capitalize transition-colors ${
                  i === deviceIdx ? "text-white" : "bg-neutral-200 text-neutral-600"
                }`}
                style={i === deviceIdx ? { background: "#8B2020" } : {}}
              >
                {d}
              </button>
            ))}
          </div>
          {/* D-pad nav hint */}
          <div className="ml-auto flex items-center gap-3 text-[16px] text-neutral-500 font-mono">
            <span>◄ {prevDeviceName}</span>
            <span className="text-neutral-300 font-bold capitalize">{device}</span>
            <span>{nextDeviceName} ►</span>
          </div>
        </div>

        {/* Status row */}
        <div className="bg-neutral-100 rounded-2xl px-8 py-5 min-h-[64px] flex items-center">
          <StatusRow status={statusData} device={device} />
        </div>

        {/* Main control area */}
        <div className="flex gap-10 items-start">

          {/* Left: sub-component tabs + action rows */}
          <div className="flex flex-col gap-5 flex-1">
            <SubTabs subs={subs} activeIdx={subIdx} onChange={i => setSubIdxMap(m => ({ ...m, [device]: i }))} />
            <ActionRows subs={subs} activeIdx={subIdx} onAction={(cmd, val) => send(cmd, val ?? 0)} />
          </div>

          {/* Right: face button cluster */}
          <div className="flex flex-col items-center gap-4 shrink-0">
            <span className="text-[14px] text-neutral-500 font-mono">face buttons</span>
            <FaceCluster face={sub.face} />
          </div>
        </div>

        {/* Joysticks — only for devices that use them */}
        {(sticks.l || sticks.r) && (
          <div className="flex justify-between items-start px-4 pt-2">
            <Joystick
              label={sticks.l ?? "—"}
              onValue={setLVal}
              onCommit={handleLCommit}
              description={sticks.lDesc}
              dimmed={!sticks.l}
            />
            <Joystick
              label={sticks.r ?? "—"}
              onValue={setRVal}
              onCommit={handleRCommit}
              description={sticks.rDesc}
              dimmed={!sticks.r}
            />
          </div>
        )}

        {/* LCD State */}
        <div className="mt-auto pt-6 border-t border-neutral-200">
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
