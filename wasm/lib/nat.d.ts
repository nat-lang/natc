/// <reference types="emscripten" />

export interface NatModule extends EmscriptenModule {
  cwrap: typeof cwrap;
  ccall: typeof ccall;
  FS: typeof FS;
}

export = EmscriptenModuleFactory<NatModule>;
