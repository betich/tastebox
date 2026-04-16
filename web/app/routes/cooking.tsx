import { useEffect, useRef, useState } from "react"
import { useNavigate, useSearchParams, useFetcher, useLoaderData } from "react-router"
import type { ActionFunctionArgs } from "react-router"
import { MENU } from "../types"

const COOK_MS = 60_000

// ── Hardware mapping ──────────────────────────────────────────────────────────
// Cooker encoder position per umami level (index 0-4)
const UMAMI_TO_POSITION = [0, 1, 2, 3, 4] as const

// Plating arm M1 steps per menu item (FRIED RICE WITH PORK, KETCHUP RICE WITH PORK)
const MENU_TO_M1_STEPS = [0, 100] as const

// Plating arm M2 steps per oiliness level (index 0-4)
const OILINESS_TO_M2_STEPS = [0, 25, 50, 75, 100] as const

// ── Status types ──────────────────────────────────────────────────────────────
interface ControllerStatus {
  ok: boolean
  cooker?: { online: boolean; on: boolean; position: number }
  plating?: { online: boolean; m1_busy: boolean; m2_busy: boolean; m1_pos: number; m2_pos: number }
}

interface LoaderData {
  controllerStatus: ControllerStatus | null
  online: boolean
}

// ── Loader — polls hardware status (called by fetcher.load too) ───────────────
export async function loader(): Promise<LoaderData> {
  const API = process.env.CONTROLLER_API_URL ?? "http://localhost:5000"
  try {
    const res = await fetch(`${API}/status`, { signal: AbortSignal.timeout(2000) })
    const data: ControllerStatus = await res.json()
    return { controllerStatus: data, online: true }
  } catch {
    return { controllerStatus: null, online: false }
  }
}

// ── Server action ─────────────────────────────────────────────────────────────
export async function action({ request }: ActionFunctionArgs) {
  const API = process.env.CONTROLLER_API_URL ?? "http://localhost:5000"

  const form     = await request.formData()
  const menu     = parseInt(form.get("menu")     as string ?? "0")
  const umami    = parseInt(form.get("umami")    as string ?? "2")
  const oiliness = parseInt(form.get("oiliness") as string ?? "3")

  const cookerPos = UMAMI_TO_POSITION[umami]    ?? 2
  const m1Steps   = MENU_TO_M1_STEPS[menu]      ?? 0
  const m2Steps   = OILINESS_TO_M2_STEPS[oiliness] ?? 50

  try {
    await Promise.all([
      fetch(`${API}/cooker/click`, { method: "POST" }),
      fetch(`${API}/cooker/position`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ position: cookerPos }),
      }),
      fetch(`${API}/plating/move`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ m1: m1Steps, m2: m2Steps }),
      }),
    ])
    return { ok: true }
  } catch (err) {
    console.error("[cooking] controller API unreachable:", err)
    return { ok: false, error: String(err) }
  }
}

export function meta() {
  return [{ title: "Tastebox — Cooking…" }]
}

// ── Derive human-readable machine status ──────────────────────────────────────
function getMachineStatus(data: LoaderData | null): string {
  if (!data || !data.online) return "Connecting to machine..."
  const { cooker, plating } = data.controllerStatus ?? {}
  if (!cooker?.online && !plating?.online) return "Hardware offline"
  if (cooker?.on && (plating?.m1_busy || plating?.m2_busy))
    return "Cooking & plating arm moving..."
  if (cooker?.on) return "Cooker is active..."
  if (plating?.m1_busy || plating?.m2_busy) return "Plating arm moving..."
  return "Finishing up..."
}

export default function Cooking() {
  const navigate = useNavigate()
  const [params] = useSearchParams()
  const menuIndex = parseInt(params.get("menu")     ?? "0")
  const umami     = params.get("umami")     ?? "2"
  const oiliness  = params.get("oiliness")  ?? "3"
  const item = MENU[menuIndex] ?? MENU[0]

  // Hardware command fetcher
  const cmdFetcher    = useFetcher()
  const submitted     = useRef(false)

  // Status polling fetcher
  const statusFetcher = useFetcher<typeof loader>()
  const initialData   = useLoaderData<typeof loader>()
  const statusData    = (statusFetcher.data ?? initialData) as LoaderData | null

  // Countdown
  const totalSecs = Math.round(COOK_MS / 1000)
  const [secsLeft, setSecsLeft] = useState(totalSecs)

  // Fire hardware commands once on mount
  useEffect(() => {
    if (submitted.current) return
    submitted.current = true
    cmdFetcher.submit(
      { menu: menuIndex, umami, oiliness },
      { method: "POST", action: "/cooking" },
    )
  }, []) // eslint-disable-line react-hooks/exhaustive-deps

  // Poll status every 2 s
  useEffect(() => {
    const id = setInterval(() => statusFetcher.load("/cooking"), 2000)
    return () => clearInterval(id)
  }, []) // eslint-disable-line react-hooks/exhaustive-deps

  // Countdown + navigate when done
  useEffect(() => {
    if (secsLeft <= 0) { navigate(`/done?menu=${menuIndex}`); return }
    const t = setTimeout(() => setSecsLeft((s) => s - 1), 1000)
    return () => clearTimeout(t)
  }, [secsLeft, navigate, menuIndex])

  const machineStatus = getMachineStatus(statusData)

  // Circular countdown progress (0→1)
  const progress = secsLeft / totalSecs
  const radius = 54
  const circumference = 2 * Math.PI * radius
  const dashOffset = circumference * (1 - progress)

  return (
    <div className="screen">
      <div className="flex flex-col items-center justify-between h-full py-48 px-20">

        <h1 className="text-[96px] font-black uppercase leading-tight tracking-tight text-center">
          COOKING<br />IN PROGRESS
        </h1>

        {/* Food image */}
        <div
          className="overflow-hidden"
          style={{ width: 780, height: 460, borderRadius: 40, boxShadow: "0 12px 48px rgba(0,0,0,0.18)" }}
        >
          <img src={item.img} alt={item.name} className="w-full h-full object-cover" />
        </div>

        {/* Countdown + status */}
        <div className="flex flex-col items-center gap-6">

          {/* Circular countdown */}
          <div className="relative flex items-center justify-center" style={{ width: 140, height: 140 }}>
            <svg width="140" height="140" style={{ transform: "rotate(-90deg)" }}>
              {/* Track */}
              <circle cx="70" cy="70" r={radius} fill="none" stroke="#e5e7eb" strokeWidth="8" />
              {/* Progress */}
              <circle
                cx="70" cy="70" r={radius}
                fill="none"
                stroke="#8B2020"
                strokeWidth="8"
                strokeLinecap="round"
                strokeDasharray={circumference}
                strokeDashoffset={dashOffset}
                style={{ transition: "stroke-dashoffset 0.9s linear" }}
              />
            </svg>
            <span
              className="absolute text-[40px] font-black"
              style={{ color: "#8B2020" }}
            >
              {secsLeft}
            </span>
          </div>

          {/* Machine status */}
          <div className="flex items-center gap-3">
            <div className="w-4 h-4 rounded-full dot-pulse" style={{ background: "#8B2020" }} />
            <span className="text-[28px] font-semibold text-neutral-600">{machineStatus}</span>
          </div>
        </div>

        <span className="text-[72px] font-black uppercase tracking-[4px]">TASTEBOX</span>
      </div>
    </div>
  )
}
