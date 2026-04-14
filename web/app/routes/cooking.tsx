import { useEffect } from "react"
import { useNavigate, useSearchParams } from "react-router"
import { MENU } from "../types"

const COOK_MS = 5000

export function meta() {
  return [{ title: "Tastebox — Cooking…" }]
}

export default function Cooking() {
  const navigate = useNavigate()
  const [params] = useSearchParams()
  const menuIndex = parseInt(params.get("menu") ?? "0")
  const item = MENU[menuIndex] ?? MENU[0]

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
