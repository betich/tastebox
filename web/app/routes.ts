import { type RouteConfig, index, route } from "@react-router/dev/routes";

export default [
  index("routes/home.tsx"),
  route("personalize", "routes/personalize.tsx"),
  route("cooking",     "routes/cooking.tsx"),
  route("done",        "routes/done.tsx"),
  route("admin",       "routes/admin.tsx"),
] satisfies RouteConfig;
