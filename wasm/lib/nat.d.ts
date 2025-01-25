/// <reference types="emscripten" />

export interface NatModule extends EmscriptenModule {
  cwrap: typeof cwrap;
  ccall: typeof ccall;
  writeStringToMemory: typeof writeStringToMemory;
  stringToUTF8: typeof stringToUTF8;
  setValue: typeof setValue;
  getValue: typeof getValue;
  FS: typeof FS;
}

export = EmscriptenModuleFactory<NatModule>;
