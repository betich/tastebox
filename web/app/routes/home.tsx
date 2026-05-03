import { useRef, useState } from "react";
import { useNavigate } from "react-router";
import { MENU } from "../types";
import { setDisplayState } from "../lib/controller.server"
import { useGamepadInput } from "../lib/useGamepad"

export async function loader() {
  await setDisplayState("idle")
  return {}
}

const DRAG_THRESHOLD = 60;
const MOVE_THRESHOLD = 8;

export function meta() {
  return [{ title: "Tastebox — Choose Your Menu" }];
}

export default function Home() {
  const navigate = useNavigate();
  const [index, setIndex] = useState(0);
  const [pressing, setPressing] = useState(false);
  const [dragOffset, setDragOffset] = useState(0);
  const [snapping, setSnapping] = useState(false);

  const startX      = useRef(0);
  const didDrag     = useRef(false);
  const axisLatched = useRef(false);

  useGamepadInput(
    (btn) => {
      if (btn === 14) advance(-1)
      if (btn === 15) advance(1)
      if (btn === 0)  navigate(`/personalize?menu=${index}`)
    },
    (axes) => {
      const x = axes[0]
      if (x < -0.5 && !axisLatched.current) { axisLatched.current = true; advance(-1) }
      else if (x > 0.5 && !axisLatched.current) { axisLatched.current = true; advance(1) }
      else if (x === 0) axisLatched.current = false
    },
  );

  const prev = (index - 1 + MENU.length) % MENU.length;
  const next = (index + 1) % MENU.length;

  function advance(dir: 1 | -1) {
    setIndex((i) => (i + dir + MENU.length) % MENU.length);
  }

  function onPointerDown(e: React.PointerEvent) {
    startX.current = e.clientX;
    didDrag.current = false;
    setSnapping(false);
  }

  function onPointerMove(e: React.PointerEvent) {
    const dx = e.clientX - startX.current;
    if (Math.abs(dx) >= MOVE_THRESHOLD) {
      didDrag.current = true;
      setDragOffset(dx);
    }
  }

  function onPointerUp(e: React.PointerEvent) {
    setSnapping(true);
    setDragOffset(0);
    if (didDrag.current) {
      const dx = e.clientX - startX.current;
      if (dx < -DRAG_THRESHOLD) advance(1);
      else if (dx > DRAG_THRESHOLD) advance(-1);
    }
    // didDrag intentionally not reset here — click fires after pointerup
  }

  return (
    <div className="screen">
      <div className="flex flex-col items-center justify-between h-full py-36 px-10">
        <h1 className="text-[96px] font-black uppercase leading-tight tracking-tight text-center">
          CHOOSE YOUR MENU
        </h1>

        {/* Pill + connector + Carousel */}
        <div className="flex flex-col items-center">
          <div className="flex flex-col items-center">
            <div className="border-[2.5px] border-black rounded-full bg-white px-14 py-5 text-[38px] font-bold tracking-widest uppercase min-w-[340px] text-center">
              <span key={index} className="label-fly-in inline-block">
                {MENU[index].name}
              </span>
            </div>
            <div className="w-[3px] h-14 bg-black" />
          </div>

          {/* Carousel */}
          <div
            className="flex items-center justify-center w-full select-none overflow-hidden"
            style={{ touchAction: "pan-y" }}
            onPointerDown={onPointerDown}
            onPointerMove={onPointerMove}
            onPointerUp={onPointerUp}
            onPointerLeave={(e) => {
              if (didDrag.current) onPointerUp(e);
            }}
          >
            {/* Inner row — translates as one unit during drag */}
            <div
              className="flex items-center justify-center"
              style={{
                transform: `translateX(${dragOffset}px)`,
                transition: snapping
                  ? "transform 0.45s cubic-bezier(0.25,1,0.5,1)"
                  : "none",
              }}
            >
              {/* Left */}
              <div
                onClick={() => { if (!didDrag.current) advance(-1); }}
                className="rounded-[28px] overflow-hidden shrink-0 cursor-pointer"
                style={{
                  width: 300,
                  height: 360,
                  opacity: 0.5,
                  transform: "scale(0.88)",
                  transition: "opacity 0.3s, transform 0.3s",
                }}
              >
                <img
                  src={MENU[prev].img}
                  className="w-full h-full object-cover"
                  draggable={false}
                  alt={MENU[prev].name}
                />
              </div>

              {/* Center */}
              <div
                onClick={() => { if (!didDrag.current) navigate(`/personalize?menu=${index}`); }}
                onPointerDown={() => setPressing(true)}
                onPointerUp={() => setPressing(false)}
                onPointerLeave={() => setPressing(false)}
                className="rounded-[36px] overflow-hidden shrink-0 cursor-pointer z-10"
                style={{
                  width: 640,
                  height: 520,
                  boxShadow: "0 16px 64px rgba(0,0,0,0.22)",
                  transform: pressing ? "scale(0.94)" : "scale(1)",
                  transition: "transform 0.18s cubic-bezier(0.34,1.56,0.64,1)",
                }}
              >
                <img
                  src={MENU[index].img}
                  className="w-full h-full object-cover"
                  draggable={false}
                  alt={MENU[index].name}
                />
              </div>

              {/* Right */}
              <div
                onClick={() => { if (!didDrag.current) advance(1); }}
                className="rounded-[28px] overflow-hidden shrink-0 cursor-pointer"
                style={{
                  width: 300,
                  height: 360,
                  opacity: 0.5,
                  transform: "scale(0.88)",
                  transition: "opacity 0.3s, transform 0.3s",
                }}
              >
                <img
                  src={MENU[next].img}
                  className="w-full h-full object-cover"
                  draggable={false}
                  alt={MENU[next].name}
                />
              </div>
            </div>
          </div>
        </div>

        <p className="text-[32px] text-neutral-500 font-semibold">
          Tap center to select
        </p>

        <span
          className="text-[72px] font-black uppercase tracking-[4px] cursor-pointer select-none active:opacity-50"
          onClick={() => navigate("/admin")}
        >
          TASTEBOX
        </span>
      </div>
    </div>
  );
}
