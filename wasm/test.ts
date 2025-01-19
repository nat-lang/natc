
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

  let compilation = await engine.compile("foo", "let z = [1 3 4];");

  if (!compilation)
    throw new Error("Complation failure: undefined.");
  if (!compilation.success)
    throw new Error("Complation failure: expecting success=true.");
  if (!(compilation.data instanceof Array))
    throw new Error("Complation failure: expecting typeof data=Array.");

})();



