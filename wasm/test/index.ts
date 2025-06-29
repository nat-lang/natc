
import Runtime, { InterpretResp } from '../src';

let fail = (msg: string) => { throw new Error(`Interpretation failure: ${msg}.`); };

let checkResp = (resp: InterpretResp) => {
  if (!resp) fail("undefined");
  if (!resp.success) fail("expecting success = true");
  if (typeof resp.out !== "string") fail("expecting instanceof resp.out = String");
}

(async () => {
  let runtime = new Runtime();

  let z = await runtime.interpret("wasm/test/gen");
  console.log(z);

  let r = runtime.generate("wasm/test/gen");
  for await (let x of r)
    console.log(x);
})();



