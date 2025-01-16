/// <reference types="emscripten" />

export interface NatModule extends EmscriptenModule {
  cwrap: typeof cwrap;
  FS: typeof FS;
}

export = EmscriptenModuleFactory<NatModule>;
