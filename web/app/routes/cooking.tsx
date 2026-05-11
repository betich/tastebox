import { useEffect, useRef, useState } from "react"
import { useNavigate, useSearchParams, useFetcher, useLoaderData } from "react-router"
import type { ActionFunctionArgs } from "react-router"
import { MENU } from "../types"

const COOK_TOTAL_S = 240

// Cooker encoder position per umami level (index 0–4)
const UMAMI_TO_POSITION = [0, 1, 2, 3, 4] as const

interface CookStatus {
  ok: boolean
  running: boolean
  step: number
  step_label: string
  done: boolean
  error: string | null
  cook_end_ms: number
}

interface LoaderData {
  cookStatus: CookStatus | null
}

export async function loader(): Promise<LoaderData> {
  const API = process.env.CONTROLLER_API_URL ?? "http://localhost:5000"
  try {
    const res = await fetch(`${API}/cook/status`, { signal: AbortSignal.timeout(2000) })
    const data: CookStatus = await res.json()
    return { cookStatus: data }
  } catch {
    return { cookStatus: null }
  }
}

export async function action({ request }: ActionFunctionArgs) {
  const API  = process.env.CONTROLLER_API_URL ?? "http://localhost:5000"
  const form = await request.formData()

  if (form.get("_action") === "estop") {
    try {
      await fetch(`${API}/cook/estop`, { method: "POST", signal: AbortSignal.timeout(3000) })
    } catch {}
    return { ok: true }
  }

  const umami    = parseInt(form.get("umami") as string ?? "2")
  const menu     = parseInt(form.get("menu")  as string ?? "0")
  const umamiPos = UMAMI_TO_POSITION[umami] ?? 2

  try {
    const res = await fetch(`${API}/cook/start`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ umami_pos: umamiPos, menu_index: menu }),
      signal: AbortSignal.timeout(5000),
    })
    const data = await res.json()
    return { ok: data.ok, error: data.error ?? null }
  } catch (err) {
    return { ok: false, error: String(err) }
  }
}

export function meta() {
  return [{ title: "Tastebox — Cooking…" }]
}

const TOTAL_STEPS = 7

export default function Cooking() {
  const navigate      = useNavigate()
  const [params]      = useSearchParams()
  const menuIndex     = parseInt(params.get("menu")  ?? "0")
  const umami         = params.get("umami")           ?? "2"
  const item          = MENU[menuIndex] ?? MENU[0]

  const cmdFetcher    = useFetcher()
  const estopFetcher  = useFetcher()
  const statusFetcher = useFetcher<typeof loader>()
  const initialData   = useLoaderData<typeof loader>()
  const submitted     = useRef(false)
  const cookStarted   = useRef(false)

  const cookStatus = ((statusFetcher.data ?? initialData) as LoaderData | null)?.cookStatus

  // Fire cook start once on mount
  useEffect(() => {
    if (submitted.current) return
    submitted.current = true
    cookStarted.current = true
    cmdFetcher.submit(
      { menu: menuIndex, umami },
      { method: "POST", action: "/cooking" },
    )
  }, []) // eslint-disable-line react-hooks/exhaustive-deps

  // Poll cook/status every 2 s
  useEffect(() => {
    const id = setInterval(() => statusFetcher.load("/cooking"), 2000)
    return () => clearInterval(id)
  }, []) // eslint-disable-line react-hooks/exhaustive-deps

  // Navigate to done when this session's cook finishes
  useEffect(() => {
    if (cookStarted.current && cookStatus?.done) {
      navigate(`/done?menu=${menuIndex}`)
    }
  }, [cookStatus?.done, navigate, menuIndex])

  // Clock for countdown (ticks every 500 ms)
  const [nowMs, setNowMs] = useState(Date.now())
  useEffect(() => {
    const id = setInterval(() => setNowMs(Date.now()), 500)
    return () => clearInterval(id)
  }, [])

  const isCooking  = cookStatus?.step === 5
  const cookEndMs  = cookStatus?.cook_end_ms ?? 0
  const remainMs   = Math.max(0, cookEndMs - nowMs)
  const remainS    = Math.ceil(remainMs / 1000)
  const progress   = isCooking && cookEndMs > 0 ? remainMs / (COOK_TOTAL_S * 1000) : 0

  const radius       = 54
  const circumference = 2 * Math.PI * radius
  const dashOffset   = circumference * (1 - Math.min(1, Math.max(0, progress)))

  const currentStep = cookStatus?.step    ?? 0
  const stepLabel   = cookStatus?.step_label ?? (cookStatus?.running ? "Starting…" : "Preparing…")
  const hasError    = !!cookStatus?.error && !cookStatus?.done

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

        {/* Progress area */}
        <div className="flex flex-col items-center gap-6">

          {isCooking ? (
            /* Circular countdown — only during step 5 */
            <div className="relative flex items-center justify-center" style={{ width: 140, height: 140 }}>
              <svg width="140" height="140" style={{ transform: "rotate(-90deg)" }}>
                <circle cx="70" cy="70" r={radius} fill="none" stroke="#e5e7eb" strokeWidth="8" />
                <circle
                  cx="70" cy="70" r={radius}
                  fill="none"
                  stroke="#8B2020"
                  strokeWidth="8"
                  strokeLinecap="round"
                  strokeDasharray={circumference}
                  strokeDashoffset={dashOffset}
                  style={{ transition: "stroke-dashoffset 0.4s linear" }}
                />
              </svg>
              <span className="absolute text-[40px] font-black" style={{ color: "#8B2020" }}>
                {remainS}
              </span>
            </div>
          ) : (
            /* Step dots — all other steps */
            <div className="flex flex-col items-center gap-3">
              {currentStep > 0 && (
                <span className="text-[22px] font-semibold text-neutral-400">
                  Step {currentStep} of {TOTAL_STEPS}
                </span>
              )}
              <div className="flex gap-3">
                {Array.from({ length: TOTAL_STEPS }, (_, i) => (
                  <div
                    key={i}
                    className="rounded-full transition-all duration-300"
                    style={{
                      width:      i + 1 === currentStep ? 16 : 12,
                      height:     i + 1 === currentStep ? 16 : 12,
                      background: i + 1 <= currentStep ? "#8B2020" : "#e5e7eb",
                      opacity:    i + 1 < currentStep ? 0.45 : 1,
                    }}
                  />
                ))}
              </div>
            </div>
          )}

          {/* Status line */}
          <div className="flex items-center gap-3">
            <div
              className={`w-4 h-4 rounded-full ${hasError ? "" : "dot-pulse"}`}
              style={{ background: hasError ? "#dc2626" : "#8B2020" }}
            />
            <span className="text-[28px] font-semibold text-neutral-600">
              {hasError ? `Error: ${cookStatus?.error}` : stepLabel}
            </span>
          </div>
        </div>

        <span className="text-[72px] font-black uppercase tracking-[4px]">TASTEBOX</span>
      </div>

      {/* E-STOP */}
      <button
        onClick={() =>
          estopFetcher.submit({ _action: "estop" }, { method: "POST", action: "/cooking" })
        }
        disabled={estopFetcher.state === "submitting" || !cookStatus?.running}
        className="fixed bottom-8 right-8 px-5 py-3 rounded-xl font-bold text-white text-[18px] transition-all"
        style={{
          background: cookStatus?.running ? "#dc2626" : "#9ca3af",
          boxShadow:  cookStatus?.running ? "0 4px 14px rgba(220,38,38,0.45)" : "none",
        }}
      >
        E-STOP
      </button>
    </div>
  )
}
