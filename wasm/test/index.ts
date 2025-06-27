
import Engine, { InterpretResp } from '../src';

let fail = (msg: string) => { throw new Error(`Interpretation failure: ${msg}.`); };

let checkResp = (resp: InterpretResp) => {
  if (!resp) fail("undefined");
  if (!resp.success) fail("expecting success = true");
  if (typeof resp.out !== "string") fail("expecting instanceof resp.out = String");
}

(async () => {
  let engine = new Engine();
  let resp: InterpretResp;

  resp = await engine.interpret("test/integration/index");
  checkResp(resp);

  resp = await engine.interpret("test/regression/index");
  checkResp(resp);

  resp = await engine.interpret("test/trip/index");
  checkResp(resp);

  resp = await engine.interpret("wasm/test/entry.nat");
  checkResp(resp);
  if (resp.out !== "success") fail("expecting resp.out = 'success'");

  resp = await engine.typeset("test/integration/composition/index");
  checkResp(resp);
})();



