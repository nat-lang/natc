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

export type InterpretResp = {
  success: boolean;
  out: string;
};

type OutputHandler = (stdout: string) => void;
type OutputHandlerMap = { [key: string]: OutputHandler };

const GEN_START = "__generator__";
const GEN_END = "__stop_generation__";

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

  readStrMem = async (ptr: number) => {
    const mod = await this.loadWasmModule();
    const buf = new Uint8Array(mod.wasmMemory.buffer);

    let str = "";

    while (buf[ptr] !== 0)
      str += String.fromCharCode(buf[ptr++]);

    return str;
  }

  typeset = async (path: string): Promise<InterpretResp> => {
    const mod = await this.loadWasmModule();
    const fn = mod.cwrap('vmTypesetModule_wasm', 'number', ['string']);
    const free = mod.cwrap('vmFree_wasm', null, []);

    const retPtr = fn(path);
    const ret = await this.readStrMem(retPtr);

    free();

    return {
      success: true,
      out: ret,
    }
  }

  interpret = async (path: string): Promise<InterpretResp> => {
    const mod = await this.loadWasmModule();
    const fn = mod.cwrap('vmInterpretEntrypoint_wasm', 'number', ['string']);
    // const free = mod.cwrap('vmFree_wasm', null, []);

    const retPtr = fn(path);
    const ret = await this.readStrMem(retPtr);

    // free();

    return {
      success: true,
      out: ret,
    }
  };

  async *generate(path: string) {
    const mod = await this.loadWasmModule();
    const fn = mod.cwrap('vmGenerate_wasm', 'number', ['string']);
    const free = mod.cwrap('vmFree_wasm', null, []);

    let ret: string | null = null;


    let i = 0;
    while (ret !== GEN_END) {

      const retPtr = fn(path);
      ret = await this.readStrMem(retPtr);
      console.log("-->", ret);

      console.log(i);
      i = i + 1;
      yield {

        success: true,
        out: ret,
      };
    }

    free();
  }

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
