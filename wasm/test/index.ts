
import Runtime, { GEN_START, NatResp } from '../src';

let fail = (msg: string) => { throw new Error(`Interpretation failure: ${msg}.`); };

let checkResp = (resp: NatResp) => {
  if (!resp) fail("undefined");
  if (!resp.success) fail("expecting success = true");
}

let checkStrResp = (resp: NatResp) => {
  checkResp(resp);
  if (typeof resp.out !== "string") fail("expecting instanceof resp.out = String");
}

let checkGenStartResp = (resp: NatResp) => {
  checkResp(resp);
  if (resp.out !== GEN_START) fail(`expecting resp.out = '${GEN_START}'`);
};

(async () => {
  let runtime = new Runtime();
  let resp: NatResp;
  let generator: AsyncGenerator<NatResp>;

  await runtime.init();

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



