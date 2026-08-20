import "react-native-get-random-values"; // entropy polyfill (Hermes) — must load first
import { registerRootComponent } from "expo";
import App from "./App";
registerRootComponent(App);
