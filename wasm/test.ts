
import Engine, { InterpretationStatus } from './src';

let checkStatus = (status: InterpretationStatus) => {
  if (status !== InterpretationStatus.OK)
    throw new Error(`Expected status: ${InterpretationStatus.OK}`);
}

(async () => {
  let engine = new Engine();
  let status: InterpretationStatus;

  status = await engine.interpret("test/integration/index");
  checkStatus(status);

  status = await engine.interpret("test/regression/index");
  checkStatus(status);

  status = await engine.interpret("test/trip/index");
  checkStatus(status);

  let resp = await engine.typeset("test/integration/composition/index");

  if (!resp)
    throw new Error("Complation failure: undefined.");
  if (!resp.success)
    throw new Error("Complation failure: expecting success = true.");
  if (typeof resp.tex !== "string")
    throw new Error("Complation failure: expecting instanceof resp.tex = String.");
})();



