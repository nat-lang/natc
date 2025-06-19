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
export const abs = (path: string) => `/${SRC_DIR}/${path}`;

export enum InterpretationStatus {
  OK = 0,
  COMPILATION_ERROR = 1,
  RUNTIME_ERROR = 2,
}

class Runtime {
  wasmModule?: NatModule;
  stdOutHandlers: OutputHandlerMap;
  stdErrHandlers: OutputHandlerMap;
  errors: string[];

  constructor() {
    this.wasmModule = undefined;
    this.stdOutHandlers = { console: console.log };
    this.stdErrHandlers = { console: console.error, store: this.storeErr };
    this.errors = [];
  }

  handleStdOut = (stdout: string) => Object.values(this.stdOutHandlers).forEach(handler => handler(stdout));
  handleStdErr = (stderr: string) => Object.values(this.stdOutHandlers).forEach(handler => handler(stderr));

  storeErr = (err: string) => this.errors.push(err);

  onStdout = (handler: OutputHandler) => {
    let uid = v4();

    this.stdOutHandlers[uid] = handler;

    return () => {
      delete this.stdOutHandlers[uid];
    };
  }

  loadWasmModule = async (): Promise<NatModule> => {
    if (!this.wasmModule) this.wasmModule = await initialize({
      print: this.handleStdOut.bind(this),
      printErr: this.handleStdErr.bind(this)
    });
    return this.wasmModule as NatModule;
  }

  readStrMem = async (pointer: number) => {
    const mod = await this.loadWasmModule();
    const memBuff = new Uint8Array(mod.wasmMemory.buffer);

    let str = "";

    while (memBuff[pointer] !== 0)
      str += String.fromCharCode(memBuff[pointer++]);

    return str;
  }

  typeset = async (path: string) => {
    const mod = await this.loadWasmModule();
    const interpret = mod.cwrap('vmTypesetModule_wasm', 'number', ['string']);
    const free = mod.cwrap('vmFree_wasm', null, []);

    const respPtr = interpret(path);
    const resp = await this.readStrMem(respPtr);

    free();

    return {
      success: true,
      tex: resp,
      errors: [],
    }
  }

  interpret = async (path: string): Promise<InterpretationStatus> => {
    const mod = await this.loadWasmModule();
    const fn = mod.cwrap('vmInterpretEntrypoint_wasm', 'number', ['string']);
    return fn(path);
  };

  getCoreFiles = async (dir = CORE_DIR) => {
    const mod = await this.loadWasmModule();
    const files: CoreFile[] = [{
      path: dir,
      type: "tree",
      content: ""
    }];

    mod.FS.readdir(abs(dir)).forEach(async file => {
      if ([".", ".."].includes(file)) return;

      let path = `${dir}/${file}`;
      let stat = mod.FS.stat(abs(path));

      if (mod.FS.isDir(stat.mode)) {
        files.push(...await this.getCoreFiles(path));
      } else {
        let content = mod.FS.readFile(abs(path), { encoding: "utf8" });
        files.push({ path, type: "blob", content });
      }
    });

    return files;
  };

  mkDir = async (path: string): Promise<void> => {
    const mod = await this.loadWasmModule();
    const pathStat = mod.FS.analyzePath(abs(path));

    if (!pathStat.exists)
      mod.FS.mkdir(abs(path));
  }

  getFile = async (path: string): Promise<CoreFile> => {
    const mod = await this.loadWasmModule();
    const content = mod.FS.readFile(abs(path), { encoding: "utf8" });

    return { path, content, type: "blob" };
  }

  setFile = async (path: string, content: string) => {
    const mod = await this.loadWasmModule();
    mod.FS.writeFile(abs(path), content, { flags: "w+" });
  }
}

export default Runtime;
