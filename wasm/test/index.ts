
import Runtime, { GEN_START, InterpretResp } from '../src';

let fail = (msg: string) => { throw new Error(`Interpretation failure: ${msg}.`); };

let checkResp = (resp: InterpretResp) => {
  if (!resp) fail("undefined");
  if (!resp.success) fail("expecting success = true");
}

let checkStrResp = (resp: InterpretResp) => {
  checkResp(resp);
  if (typeof resp.out !== "string") fail("expecting instanceof resp.out = String");
}

let checkGenStartResp = (resp: InterpretResp) => {
  checkResp(resp);
  if (resp.out !== GEN_START) fail(`expecting resp.out = '${GEN_START}'`);
};

(async () => {
  let runtime = new Runtime();
  let resp: InterpretResp;
  let generator: AsyncGenerator<InterpretResp>;

  resp = await runtime.interpret("test/integration/index");
  checkStrResp(resp);

  resp = await runtime.interpret("test/regression/index");
  checkStrResp(resp);

  resp = await runtime.interpret("test/trip/index");
  checkStrResp(resp);

  resp = await runtime.interpret("wasm/test/textEntrypoint");
  checkStrResp(resp);
  if (resp.out !== "success") fail("expecting resp.out = 'success'");

  resp = await runtime.interpret("wasm/test/genEntrypoint");
  checkGenStartResp(resp);
  generator = runtime.generate("wasm/test/genEntrypoint");
  for await (const x of generator)
    checkResp(x);

  resp = await runtime.interpret("test/integration/composition/index");
  checkGenStartResp(resp);
  generator = runtime.generate("test/integration/composition/index");
  for await (const x of generator)
    checkResp(x);

  resp = await runtime.interpret("wasm/test/genCodeblockEntrypoint");
  checkGenStartResp(resp);
  generator = runtime.generate("wasm/test/genCodeblockEntrypoint");
  for await (const x of generator)
    checkResp(x);

  runtime.free();
})();



