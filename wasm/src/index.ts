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

export type RespType = "string" | "tex" | "flag";

type BaseResp = {
  success: boolean;
  out: string;
}
export type FlagResp = BaseResp & { type: "flag"; }
export type TexResp = BaseResp & { type: "tex"; }
export type TextResp = BaseResp & { type: "string"; }
export type CodeblockResp = BaseResp & {
  type: "codeblock";
  out: { text: string };
}
export type AnchorResp = BaseResp & {
  type: "anchor";
  out: { title: string; tex: string; path: string };
}

export type NatResp = FlagResp | TexResp | TextResp | CodeblockResp | AnchorResp;

type OutputHandler = (stdout: string) => void;
type OutputHandlerMap = { [key: string]: OutputHandler };

export const EXT = "nat";
export const GEN_START = "__start_generation__";
const GEN_END = "__stop_generation__";

export const CORE_DIR = "core", SRC_DIR = "src";

export enum InterpretationStatus {
  OK = 0,
  COMPILATION_ERROR = 1,
  RUNTIME_ERROR = 2,
}

class Lock {
  locked: boolean;
  queue: (() => void)[];
  constructor() {
    this.locked = false;
    this.queue = [];
  }

  acquire() {
    return new Promise<void>(resolve => {
      if (!this.locked) {
        this.locked = true;
        resolve();
      } else {
        this.queue.push(resolve);
      }
    });
  }

  release() {
    this.locked = false;
    if (this.queue.length > 0) {
      const next = this.queue.shift();
      this.locked = true;
      next && next();
    }
  }
}

class Runtime {
  wasmModule?: Promise<NatModule>;
  stdOutHandlers: OutputHandlerMap;
  stdErrHandlers: OutputHandlerMap;
  errors: string[];
  lock: Lock;
  rootDir: string;

  constructor({ rootDir = "." } = {}) {
    this.wasmModule = undefined;
    this.stdOutHandlers = { console: console.log };
    this.stdErrHandlers = { console: console.error, store: this.storeErr };
    this.errors = [];
    this.lock = new Lock();
    this.rootDir = rootDir;
  }

  abs = (path: string) => `${this.rootDir}/${path}`;

  handleStdOut = (out: string) => Object.values(this.stdOutHandlers).forEach(handler => handler(out));
  handleStdErr = (err: string) => Object.values(this.stdErrHandlers).forEach(handler => handler(err));

  storeErr = (err: string) => this.errors.push(err);

  onStdout = (handler: OutputHandler) => {
    let uid = v4();

    this.stdOutHandlers[uid] = handler;

    return () => {
      delete this.stdOutHandlers[uid];
    };
  }

  onStderr = (handler: OutputHandler) => {
    let uid = v4();

    this.stdErrHandlers[uid] = handler;

    return () => {
      delete this.stdErrHandlers[uid];
    };
  }

  loadWasmModule = async (): Promise<NatModule> => {
    if (!this.wasmModule) {
      this.wasmModule = initialize({
        print: this.handleStdOut.bind(this),
        printErr: this.handleStdErr.bind(this)
      });
    }

    return await this.wasmModule as NatModule;
  }

  readStrMem = async (ptr: number) => {
    const mod = await this.loadWasmModule();
    const buf = new Uint8Array(mod.wasmMemory.buffer);

    let str = "";

    while (buf[ptr] !== 0)
      str += String.fromCharCode(buf[ptr++]);

    return str;
  }

  interpret = async (path: string): Promise<NatResp> => {
    const mod = await this.loadWasmModule();
    const fn = mod.cwrap('vmInterpretEntrypoint_wasm', 'number', ['string']);
    const retPtr = fn(this.abs(path));
    const out = await this.readStrMem(retPtr);

    return JSON.parse(out);
  };

  async *generate(path: string): AsyncGenerator<NatResp> {
    const mod = await this.loadWasmModule();
    const fn = mod.cwrap('vmGenerate_wasm', 'number', ['string']);
    const next = async () => {
      let out = await this.readStrMem(fn(this.abs(path)));
      let resp: NatResp = JSON.parse(out);
      return resp;
    };

    let resp: NatResp | null = null;

    while ((resp = await next())?.out != GEN_END)
      yield resp;
  }

  free = async () => {
    const mod = await this.loadWasmModule();
    const free = mod.cwrap('vmFree_wasm', null, []);
    free();
  }

  init = async () => {
    const mod = await this.loadWasmModule();
    const init = mod.cwrap('vmInit_wasm', null, []);
    init();
  }

  getCoreFiles = async (dir = "/" + CORE_DIR) => {
    const mod = await this.loadWasmModule();
    const files: CoreFile[] = [];

    await Promise.all(
      mod.FS.readdir(this.abs(dir)).map(async file => {
        if ([".", ".."].includes(file)) return;

        let path = `${dir}/${file}`;
        let stat = mod.FS.stat(this.abs(path));

        if (mod.FS.isDir(stat.mode)) {
          files.push({
            path,
            type: "tree",
            content: ""
          });

          files.push(...await this.getCoreFiles(path));
        } else {
          let content = mod.FS.readFile(this.abs(path), { encoding: "utf8" });
          files.push({
            path,
            type: "blob",
            content
          });
        }
      })
    );

    return files;
  };

  mkDir = async (path: string): Promise<void> => {
    const mod = await this.loadWasmModule();
    const pathStat = mod.FS.analyzePath(this.abs(path));

    if (!pathStat.exists)
      mod.FS.mkdir(this.abs(path));
  }

  getFile = async (path: string): Promise<CoreFile> => {
    const mod = await this.loadWasmModule();
    const content = mod.FS.readFile(this.abs(path), { encoding: "utf8" });

    return { path, content, type: "blob" };
  }

  setFile = async (path: string, content: string) => {
    const mod = await this.loadWasmModule();
    mod.FS.writeFile(this.abs(path), content, { flags: "w+" });
  }

  rmFile = async (path: string) => {
    const mod = await this.loadWasmModule();
    mod.FS.unlink(this.abs(path));
  }

  ls = async (path: string) => {
    const mod = await this.loadWasmModule();
    const files = [];

    const contents = mod.FS.readdir(path);

    for (const item of contents) {
      if (item !== '.' && item !== '..') {
        files.push(item);
      }
    }
    return files;
  }
}

export default Runtime;
