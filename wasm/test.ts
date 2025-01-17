import init, { NatModule } from './lib/nat.js';

let test = (path: string) => init().then(
  (mod: NatModule) => {
    mod.ccall("vmInterpretModule_wasm", "number", ["string"], [path])
  }
);

test("test/integration/index");
test("test/regression/index");
test("test/trip/index");
