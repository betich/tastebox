export interface MenuItem {
  name: string
  img: string
}

export const MENU: MenuItem[] = [
  { name: "FRIED RICE WITH PORK",   img: "/fried_rice_pork.png" },
  { name: "KETCHUP RICE WITH PORK", img: "/ketchup_rice_pork.png" },
  { name: "CURRY INSTANT NOODLES",  img: "/curry_noodles.png" },
  { name: "NOODLE SAUSAGE",         img: "/sausage_noodles.png" },
]

export const SEGMENT_LABELS = ["0%", "50%", "80%", "100%", "125%"] as const
