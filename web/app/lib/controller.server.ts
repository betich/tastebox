const api = () => process.env.CONTROLLER_API_URL ?? "http://localhost:5000"

async function post(path: string, body?: object): Promise<void> {
  await fetch(`${api()}${path}`, {
    method: "POST",
    signal: AbortSignal.timeout(2000),
    ...(body && { headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) }),
  })
}

export async function setDisplayState(state: string, subtitle = "") {
  await post("/state", { state, subtitle }).catch(() => {})
}

export async function cookerClick() {
  await post("/cooker/click")
}

export async function cookerSetPosition(position: number) {
  await post("/cooker/position", { position })
}

export async function platingMove(m1: number, m2: number) {
  await post("/plating/move", { m1, m2 })
}
