
import Engine, { InterpretationStatus } from './src';

let checkStatus = (status: InterpretationStatus) => {
  if (status !== InterpretationStatus.OK)
    throw new Error(`Expected status: ${InterpretationStatus.OK}`);
}

(async () => {
  let engine = new Engine();
  let status: InterpretationStatus;
  let compilation = await engine.compile("foo", "let z = [1 3 4];");

  if (!compilation)
    throw new Error("Complation failure: undefined.");
  if (!compilation.success)
    throw new Error("Complation failure: expecting success = true.");
  if (typeof compilation.tex !== "string")
    throw new Error("Complation failure: expecting instanceof data = String.");

  engine.onStdout(console.log);

  status = await engine.interpret("test/integration/index", "");
  checkStatus(status);

  status = await engine.interpret("test/regression/index", "");
  checkStatus(status);

  status = await engine.interpret("test/trip/index", "");
  checkStatus(status);
})();



