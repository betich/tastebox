export interface MenuItem {
  name: string
  img: string
}

export const MENU: MenuItem[] = [
  { name: "PAD KAPRAO", img: "/pad_kaprao.png" },
  { name: "MASSAMAN",   img: "/massaman.png" },
  { name: "FRIED RICE", img: "/fried_rice.png" },
]

export const SEGMENT_LABELS = ["0%", "50%", "80%", "100%", "125%"] as const
