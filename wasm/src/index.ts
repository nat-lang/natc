import { v4 } from 'uuid';
import initialize, { NatModule } from "../lib/nat";

export type CompilationNode = {
  children: CompilationNode[];
  name: string;
  type: string;
  tex: string;
  html?: string;
}

export type Compilation = {
  success: boolean;
  data: CompilationNode[];
}

export type CoreFile = {
  path: string;
  content: string;
  type: "tree" | "blob";
};

type OutputHandler = (stdout: string) => void;
type OutputHandlerMap = { [key: string]: OutputHandler };

export const CORE_DIR = "core", SRC_DIR = "src";

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

  print = (stdout: string) => {
    process.stdout.write(`calling print handlers\n`);
    Object.values(this.stdOutHandlers).forEach(handler => handler(stdout));
  }
  printErr = (stderr: string) => Object.values(this.stdOutHandlers).forEach(handler => handler(stderr));

  onStdout = (handler: OutputHandler) => {
    let uid = v4();

    this.stdOutHandlers[uid] = handler;

    process.stdout.write(`adding handler: ${uid}\n`);

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

  compile = async (path: string, source: string) => {
    const runtime = await this.loadRuntime();
    const strings = [path, source];
    const fn = runtime.cwrap('vmInterpretEntrypoint_wasm', 'number', ['string', 'number', 'number']);

    let out = "";
    const unregister = this.onStdout(stdout => {
      process.stdout.write(`on stdout: ${stdout}`);
      out += stdout;
    });

    // allocate memory for the array of pointers.
    const ptrArray = runtime._malloc(strings.length * 4); // 4 bytes for each pointer.

    // populate the array of pointers.
    for (let i = 0; i < strings.length; ++i) {
      const length = strings[i].length + 1;
      const strPtr = runtime._malloc(length); // +1 for null terminator.
      runtime.stringToUTF8(strings[i], strPtr, length);
      runtime.setValue(ptrArray + i * 4, strPtr, '*');
    }

    const status = fn("src/core/nls", strings.length, ptrArray);

    // free the allocated memory.
    for (let i = 0; i < strings.length; ++i) {
      runtime._free(runtime.getValue(ptrArray + i * 4, '*'));
    }
    runtime._free(ptrArray);

    // release the stdout handler.
    unregister();
    return status === InterpretationStatus.OK ? JSON.parse(out) as Compilation : undefined;
  }

  interpret = async (path: string): Promise<InterpretationStatus> => {
    const runtime = await this.loadRuntime();
    const fn = runtime.cwrap('vmInterpretModule_wasm', 'number', ['string']);
    return fn(path);
  };

  getCoreFiles = async (dir = CORE_DIR) => {
    const runtime = await this.loadRuntime();
    const files: CoreFile[] = [{
      path: dir,
      type: "tree",
      content: ""
    }];

    const abs = (path: string) => `/${SRC_DIR}/${path}`;

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
    const absPath = `/${SRC_DIR}/${path}`;

    runtime.FS.mkdir(absPath);
  }

  getFile = async (path: string): Promise<CoreFile> => {
    const runtime = await this.loadRuntime();
    const absPath = `/${SRC_DIR}/${path}`;
    const content = runtime.FS.readFile(absPath, { encoding: "utf8" });

    return { path, content, type: "blob" };
  }

  setFile = async (path: string, content: string) => {
    const runtime = await this.loadRuntime();
    const absPath = `/${SRC_DIR}/${path}`;

    runtime.FS.writeFile(absPath, content, { flags: "w+" });
  }
}

export default Engine;
