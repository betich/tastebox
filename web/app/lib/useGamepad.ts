import { useEffect, useRef, useState } from "react"

/**
 * Polls the browser Gamepad API each animation frame.
 * - onButtonPress fires once per button-down transition
 * - onAxes fires every frame with dead-zone-filtered axis values
 * Returns true when at least one gamepad is connected.
 *
 * Standard mapping (Xbox / PS / most USB gamepads):
 *   buttons: 0=A  1=B  2=X  3=Y  10=L3  11=R3  12=↑  13=↓  14=←  15=→
 *   axes:    0=LX  1=LY  2=RX  3=RY  (-1=left/up, +1=right/down)
 */
export function useGamepadInput(
  onButtonPress: ((index: number) => void) | null | undefined,
  onAxes?: ((axes: readonly number[]) => void) | null,
  deadZone = 0.15,
): boolean {
  const [connected, setConnected] = useState(false)

  // Keep callbacks in refs so the RAF loop always calls the latest version
  const cbButton = useRef(onButtonPress)
  const cbAxes   = useRef(onAxes)
  cbButton.current = onButtonPress
  cbAxes.current   = onAxes

  const prevPressed  = useRef<boolean[]>([])
  const connectedRef = useRef(false)
  const rafId        = useRef(0)

  useEffect(() => {
    if (typeof window === "undefined") return

    function poll() {
      const gp = Array.from(navigator.getGamepads()).find(Boolean) ?? null

      if (gp && !connectedRef.current) {
        connectedRef.current = true
        setConnected(true)
      } else if (!gp && connectedRef.current) {
        connectedRef.current = false
        setConnected(false)
      }

      if (gp) {
        gp.buttons.forEach((btn, i) => {
          if (btn.pressed && !prevPressed.current[i]) cbButton.current?.(i)
          prevPressed.current[i] = btn.pressed
        })
        cbAxes.current?.(gp.axes.map(a => (Math.abs(a) < deadZone ? 0 : a)))
      }

      rafId.current = requestAnimationFrame(poll)
    }

    rafId.current = requestAnimationFrame(poll)
    return () => cancelAnimationFrame(rafId.current)
  }, []) // eslint-disable-line react-hooks/exhaustive-deps

  return connected
}
