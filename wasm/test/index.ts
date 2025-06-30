
import Runtime, { GEN_START, InterpretResp } from '../src';

let fail = (msg: string) => { throw new Error(`Interpretation failure: ${msg}.`); };

let checkResp = (resp: InterpretResp) => {
  if (!resp) fail("undefined");
  if (!resp.success) fail("expecting success = true");
  if (typeof resp.out !== "string") fail("expecting instanceof resp.out = String");
}

(async () => {
  let runtime = new Runtime();
  let resp: InterpretResp;

  resp = await runtime.interpret("test/integration/index");
  checkResp(resp);

  resp = await runtime.interpret("test/regression/index");
  checkResp(resp);

  resp = await runtime.interpret("test/trip/index");
  checkResp(resp);

  resp = await runtime.interpret("wasm/test/entry");
  checkResp(resp);
  if (resp.out !== "success") fail("expecting resp.out = 'success'");

  resp = await runtime.interpret("wasm/test/gen");
  checkResp(resp);
  if (resp.out !== GEN_START) fail(`expecting resp.out = '${GEN_START}'`);

  let generator = runtime.generate("wasm/test/gen");
  for await (const x of generator)
    checkResp(x);

  resp = await runtime.interpret("test/integration/composition/index");
  checkResp(resp);

  generator = runtime.generate("test/integration/composition/index");
  for await (const x of generator)
    checkResp(x);

  runtime.free();
})();



