import { useEffect, useRef } from "react"
import { useNavigate, useSearchParams, useFetcher } from "react-router"
import type { ActionFunctionArgs } from "react-router"
import { MENU } from "../types"

const COOK_MS = 5000

// ── Hardware mapping ──────────────────────────────────────────────────────────
// Cooker encoder position per umami level (index 0-4)
const UMAMI_TO_POSITION = [0, 1, 2, 3, 4] as const

// Plating arm M1 steps per menu item (PAD KAPRAO, MASSAMAN, FRIED RICE)
const MENU_TO_M1_STEPS = [0, 100, 200] as const

// Plating arm M2 steps per spiciness level (index 0-4)
const SPICINESS_TO_M2_STEPS = [0, 25, 50, 75, 100] as const

// ── Server action ─────────────────────────────────────────────────────────────
export async function action({ request }: ActionFunctionArgs) {
  const API = process.env.CONTROLLER_API_URL ?? "http://localhost:5000"

  const form    = await request.formData()
  const menu     = parseInt(form.get("menu")     as string ?? "0")
  const umami    = parseInt(form.get("umami")    as string ?? "2")
  const spiciness = parseInt(form.get("spiciness") as string ?? "3")

  const cookerPos = UMAMI_TO_POSITION[umami]    ?? 2
  const m1Steps   = MENU_TO_M1_STEPS[menu]      ?? 0
  const m2Steps   = SPICINESS_TO_M2_STEPS[spiciness] ?? 50

  try {
    await Promise.all([
      // Turn cooker on
      fetch(`${API}/cooker/click`, { method: "POST" }),
      // Set cooker knob position based on umami
      fetch(`${API}/cooker/position`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ position: cookerPos }),
      }),
      // Move plating arm to the correct slot and spiciness position
      fetch(`${API}/plating/move`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ m1: m1Steps, m2: m2Steps }),
      }),
    ])
    return { ok: true }
  } catch (err) {
    // Hardware unreachable — let the cooking animation continue anyway
    console.error("[cooking] controller API unreachable:", err)
    return { ok: false, error: String(err) }
  }
}

export function meta() {
  return [{ title: "Tastebox — Cooking…" }]
}

export default function Cooking() {
  const navigate = useNavigate()
  const [params] = useSearchParams()
  const menuIndex  = parseInt(params.get("menu")      ?? "0")
  const umami      = params.get("umami")      ?? "2"
  const spiciness  = params.get("spiciness")  ?? "3"
  const item = MENU[menuIndex] ?? MENU[0]

  const fetcher   = useFetcher()
  const submitted = useRef(false)

  // Fire hardware commands once on mount
  useEffect(() => {
    if (submitted.current) return
    submitted.current = true
    fetcher.submit(
      { menu: menuIndex, umami, spiciness },
      { method: "POST", action: "/cooking" },
    )
  }, []) // eslint-disable-line react-hooks/exhaustive-deps

  // Navigate to done after cook timer
  useEffect(() => {
    const t = setTimeout(() => navigate(`/done?menu=${menuIndex}`), COOK_MS)
    return () => clearTimeout(t)
  }, [navigate, menuIndex])

  return (
    <div className="screen">
      <div className="flex flex-col items-center justify-between h-full py-48 px-20">

        <h1 className="text-[96px] font-black uppercase leading-tight tracking-tight text-center">
          COOKING<br />IN PROGRESS
        </h1>

        {/* Food image */}
        <div
          className="overflow-hidden"
          style={{ width: 780, height: 560, borderRadius: 40, boxShadow: "0 12px 48px rgba(0,0,0,0.18)" }}
        >
          <img src={item.img} alt={item.name} className="w-full h-full object-cover" />
        </div>

        {/* Pulsing dots */}
        <div className="flex gap-9 items-center justify-center">
          {[0, 1, 2].map((i) => (
            <div
              key={i}
              className="w-16 h-16 rounded-full dot-pulse"
              style={{ background: "#8B2020", animationDelay: `${i * 0.2}s` }}
            />
          ))}
        </div>

        <span className="text-[72px] font-black uppercase tracking-[4px]">TASTEBOX</span>
      </div>
    </div>
  )
}
