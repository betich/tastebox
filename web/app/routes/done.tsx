import { useEffect, useState } from "react"
import { useNavigate, useSearchParams } from "react-router"
import { MENU } from "../types"
import { setDisplayState } from "../lib/controller.server"
import { useGamepadInput } from "../lib/useGamepad"

export async function loader() {
  await setDisplayState("finished")
  return {}
}

const CLOSE_SECS = 5

export function meta() {
  return [{ title: "Tastebox — Enjoy!" }]
}

export default function Done() {
  const navigate = useNavigate()
  const [params] = useSearchParams()
  const menuIndex = parseInt(params.get("menu") ?? "0")
  const item = MENU[menuIndex] ?? MENU[0]

  const [secs, setSecs] = useState(CLOSE_SECS)
  const [pressing, setPressing] = useState(false)

  useGamepadInput((btn) => { if (btn === 0) navigate("/") })

  useEffect(() => {
    if (secs <= 0) { navigate("/"); return }
    const t = setTimeout(() => setSecs((s) => s - 1), 1000)
    return () => clearTimeout(t)
  }, [secs, navigate])

  return (
    <div className="screen">
      <div className="flex flex-col items-center justify-between h-full py-48 px-20">

        <h1 className="text-[120px] font-black uppercase leading-tight tracking-tight text-center">
          ENJOY!
        </h1>

        {/* Food image */}
        <div
          className="overflow-hidden"
          style={{ width: 780, height: 560, borderRadius: 40, boxShadow: "0 12px 48px rgba(0,0,0,0.18)" }}
        >
          <img src={item.img} alt={item.name} className="w-full h-full object-cover" />
        </div>

        {/* CTA + countdown */}
        <div className="flex flex-col items-center gap-7">
          <button
            onPointerDown={() => setPressing(true)}
            onPointerUp={() => { setPressing(false); navigate("/") }}
            onPointerLeave={() => setPressing(false)}
            className="bg-neutral-900 text-white rounded-full text-[44px] font-bold px-20 py-7 cursor-pointer border-none"
            style={{
              transform: pressing ? "scale(0.93)" : "scale(1)",
              transition: "transform 0.15s cubic-bezier(0.34,1.56,0.64,1)",
            }}
          >
            Order Again →
          </button>
          <span className="text-[30px] text-neutral-500 font-medium">
            Closing in {secs} second{secs !== 1 ? "s" : ""} …
          </span>
        </div>

        <span className="text-[72px] font-black uppercase tracking-[4px]">TASTEBOX</span>
      </div>
    </div>
  )
}
