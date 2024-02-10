
class Map < __map__ {
  set(key, val) => { return this[key] = val; }
  get(key) => { return this[key]; }

  __len__() => { return len(this.entries()); }

  __curr__(idx) => {
    return this.entries()[idx];
  }

  keys() => { return fmap(this, fst); }
  values() => { return fmap(this, snd); }
}