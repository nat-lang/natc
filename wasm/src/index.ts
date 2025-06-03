import { v4 } from 'uuid';
import initialize, { NatModule } from "../lib/nat";

export type Compilation = {
  success: boolean;
  tex: string;
}

export type CoreFile = {
  path: string;
  content: string;
  type: "tree" | "blob";
};

type OutputHandler = (stdout: string) => void;
type OutputHandlerMap = { [key: string]: OutputHandler };

export const CORE_DIR = "core", SRC_DIR = "src";
const abs = (path: string) => `/${SRC_DIR}/${path}`;

export enum InterpretationStatus {
  OK = 0,
  COMPILATION_ERROR = 1,
  RUNTIME_ERROR = 2,
}

class Engine {
  runtime?: NatModule;
  stdOutHandlers: OutputHandlerMap;
  stdErrHandlers: OutputHandlerMap;

  constructor() {
    this.runtime = undefined;
    this.stdOutHandlers = { console: console.log };
    this.stdErrHandlers = { console: console.error };
  }

  print = (stdout: string) => Object.values(this.stdOutHandlers).forEach(handler => handler(stdout));
  printErr = (stderr: string) => Object.values(this.stdOutHandlers).forEach(handler => handler(stderr));

  onStdout = (handler: OutputHandler) => {
    let uid = v4();

    this.stdOutHandlers[uid] = handler;

    return () => {
      delete this.stdOutHandlers[uid];
    };
  }

  loadRuntime = async (): Promise<NatModule> => {
    if (!this.runtime) this.runtime = await initialize({
      print: this.print.bind(this),
      printErr: this.printErr.bind(this)
    });
    return this.runtime as NatModule;
  }

  readStrMem = async (pointer: number) => {
    const runtime = await this.loadRuntime();
    const memBuff = new Uint8Array(runtime.wasmMemory.buffer);

    let str = "";

    while (memBuff[pointer] !== 0)
      str += String.fromCharCode(memBuff[pointer++]);

    return str;
  }

  typeset = async (path: string) => {
    const runtime = await this.loadRuntime();
    const interpret = runtime.cwrap('vmTypesetModule_wasm', 'number', ['string']);
    const free = runtime.cwrap('vmFree_wasm', null, []);

    const respPtr = interpret(abs(path));
    const resp = await this.readStrMem(respPtr);

    free();

    return {
      success: true,
      tex: resp
    }
  }

  interpret = async (path: string): Promise<InterpretationStatus> => {
    const runtime = await this.loadRuntime();
    const fn = runtime.cwrap('vmInterpretEntrypoint_wasm', 'number', ['string']);
    return fn(abs(path));
  };

  getCoreFiles = async (dir = CORE_DIR) => {
    const runtime = await this.loadRuntime();
    const files: CoreFile[] = [{
      path: dir,
      type: "tree",
      content: ""
    }];

    runtime.FS.readdir(abs(dir)).forEach(async file => {
      if ([".", ".."].includes(file)) return;

      let path = `${dir}/${file}`;
      let stat = runtime.FS.stat(abs(path));

      if (runtime.FS.isDir(stat.mode)) {
        files.push(...await this.getCoreFiles(path));
      } else {
        let content = runtime.FS.readFile(abs(path), { encoding: "utf8" });
        files.push({ path, type: "blob", content });
      }
    });

    return files;
  };

  mkDir = async (path: string): Promise<void> => {
    const runtime = await this.loadRuntime();
    const pathStat = runtime.FS.analyzePath(abs(path));

    if (!pathStat.exists)
      runtime.FS.mkdir(abs(path));
  }

  getFile = async (path: string): Promise<CoreFile> => {
    const runtime = await this.loadRuntime();
    const content = runtime.FS.readFile(abs(path), { encoding: "utf8" });

    return { path, content, type: "blob" };
  }

  setFile = async (path: string, content: string) => {
    const runtime = await this.loadRuntime();
    runtime.FS.writeFile(abs(path), content, { flags: "w+" });
  }
}

export default Engine;
