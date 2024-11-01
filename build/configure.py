import os

CONFIGURATION = """
  #ifndef nat_config_h
  #define nat_config_h
  #define NAT_BASE_DIR "{base_dir}"
  #define NAT_CORE_LOC "{core_loc}" 
  #define NAT_SYSTEM_LOC "{system_loc}"
  #endif
"""

if __name__ == "__main__":
  base_dir = os.environ["NAT_BASE_DIR"]
  config = os.path.join(base_dir, "src", "config.h")
  src_dir = "src"

  with open(config, "w+") as f:
    configuration = CONFIGURATION.format(
      base_dir=base_dir,
      core_loc=os.path.join(src_dir, "core", "index"),
      system_loc=os.path.join(src_dir, "core", "system")
    )

    f.write(configuration)
