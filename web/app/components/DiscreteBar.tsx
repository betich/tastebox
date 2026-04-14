import { SEGMENT_LABELS } from "../types"

interface Props {
  label: string
  value: number
  onChange: (value: number) => void
}

export default function DiscreteBar({ label, value, onChange }: Props) {
  return (
    <div className="w-full">
      {/* Pill + connector */}
      <div className="flex flex-col items-center">
        <div className="border-2 border-black rounded-full bg-white px-12 py-4 text-[38px] font-bold tracking-widest uppercase">
          {label}
        </div>
        <div className="w-[3px] h-12 bg-black" />
      </div>

      {/* Segmented bar */}
      <div className="flex rounded-full border-[2.5px] border-black overflow-hidden h-[120px] cursor-pointer">
        {SEGMENT_LABELS.map((_, i) => (
          <div
            key={i}
            onClick={() => onChange(i)}
            className="flex-1 transition-colors duration-200"
            style={{
              background: i <= value ? "#8B2020" : "#fff",
              borderLeft: i === 0 ? "none" : "2px solid #000",
            }}
          />
        ))}
      </div>

      {/* Labels */}
      <div className="flex mt-3">
        {SEGMENT_LABELS.map((l) => (
          <span key={l} className="flex-1 text-center text-[28px] font-semibold text-neutral-600">
            {l}
          </span>
        ))}
      </div>
    </div>
  )
}
