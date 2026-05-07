import { useState, useEffect } from "react"
import { useNavigate, useSearchParams } from "react-router"
import DiscreteBar from "../components/DiscreteBar"
import { useGamepadInput } from "../lib/useGamepad"

export function meta() {
  return [{ title: "Tastebox — Personalize" }]
}

export default function Personalize() {
  const navigate = useNavigate()
  const [params] = useSearchParams()
  const menu = params.get("menu") ?? "0"

  const [umami,    setUmami]    = useState(2)
  const [oiliness, setOiliness] = useState(3)
  const [pressing, setPressing]  = useState(false)
  const [backEnabled, setBackEnabled] = useState(false)

  const MAX = 4  // SEGMENT_LABELS.length - 1

  useEffect(() => {
    const t = setTimeout(() => setBackEnabled(true), 5000)
    return () => clearTimeout(t)
  }, [])

  useGamepadInput((btn) => {
    if (btn === 14) setUmami(u => Math.max(0, u - 1))
    if (btn === 15) setUmami(u => Math.min(MAX, u + 1))
    if (btn === 12) setOiliness(o => Math.min(MAX, o + 1))
    if (btn === 13) setOiliness(o => Math.max(0, o - 1))
    if (btn === 0)  handleNext()
    if (btn === 1 && backEnabled) navigate("/")
  })

  function handleNext() {
    navigate(`/cooking?menu=${menu}&umami=${umami}&oiliness=${oiliness}`)
  }

  return (
    <div className="screen">
      <button
        onClick={() => navigate("/")}
        disabled={!backEnabled}
        className="absolute top-8 left-10 text-[28px] text-neutral-400 bg-transparent border-none cursor-pointer"
        style={{ opacity: backEnabled ? 1 : 0.3 }}
      >
        ← Back
      </button>
      <div className="flex flex-col items-center justify-between h-full py-36 px-20">

        <h1 className="text-[96px] font-black uppercase leading-tight tracking-tight text-center">
          HOW STRONG<br />WOULD YOU LIKE IT?
        </h1>

        <div className="w-full flex flex-col gap-20">
          <DiscreteBar label="UMAMI"    value={umami}    onChange={setUmami} />
          <DiscreteBar label="OILINESS" value={oiliness} onChange={setOiliness} />
        </div>

        {/* Next button */}
        <button
          onPointerDown={() => setPressing(true)}
          onPointerUp={() => { setPressing(false); handleNext() }}
          onPointerLeave={() => setPressing(false)}
          className="bg-neutral-900 text-white rounded-full text-[44px] font-bold px-20 py-7 cursor-pointer border-none"
          style={{
            transform: pressing ? "scale(0.93)" : "scale(1)",
            transition: "transform 0.15s cubic-bezier(0.34,1.56,0.64,1)",
          }}
        >
          Next →
        </button>

        <span className="text-[72px] font-black uppercase tracking-[4px]">TASTEBOX</span>
      </div>
    </div>
  )
}
